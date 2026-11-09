#include "DxLibBackendSubsystem.h"


#include "Library/Core/Collection/Set.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/CString.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/Render/DxLibLighting.h"
#include "Library/Platform/DxLib/Render/DxLibRenderState3D.h"
#include "VendorModules/ImGui/imgui_impl_dxlib.hpp"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/DxLib/Render/DxLibImGuiTexture.h"

DEFINE_LOG_CATEGORY( LogPlatform )

REGISTER_SUBSYSTEM( CDxLibBackendSubsystem )

namespace
{
	// ImGui にもウィンドウメッセージを渡す（DxLib 側の処理も継続させる）
	LRESULT CALLBACK ImGuiWndProcHook( HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam )
	{
		SetUseHookWinProcReturnValue( FALSE );
		return ImGui_ImplWin32_WndProcHandler( hWnd, Msg, wParam, lParam );
	}

	int ToDxScalingMode( EScalingFilter Filter )
	{
		return Filter == EScalingFilter::Nearest ? DX_FSSCALINGMODE_NEAREST : DX_FSSCALINGMODE_BILINEAR;
	}

	// ウィンドウモードに対応する DxLib のフルスクリーン解像度モード
	int ToDxResolutionMode( EWindowMode Mode )
	{
		return Mode == EWindowMode::Borderless ? DX_FSRESOLUTIONMODE_BORDERLESS_WINDOW : DX_FSRESOLUTIONMODE_DESKTOP;
	}
}

#pragma region Lifecycle

bool CDxLibBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DxLib;
}

void CDxLibBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	const TResult<void> InitResult = Init( GetEngine()->GetStartupSettings() );
	if ( InitResult.IsErr() )
	{
		// 失敗は IsInitialized() == false として CEngine が検出する
		LOG( LogPlatform, Error, "%s", *InitResult.UnwrapErr() );
	}
}

void CDxLibBackendSubsystem::Deinitialize()
{
	Shutdown();
}

TResult<void> CDxLibBackendSubsystem::Init( const FWindowSettings& InSettings )
{
	if ( bInitialized )
	{
		return TResult<void>::Err( "CDxLibBackendSubsystem is already initialized" );
	}

	Settings       = InSettings;
	bExitRequested = false;
	if ( Settings.WindowMode != EWindowMode::Windowed )
	{
		LastFullscreenMode = Settings.WindowMode;
	}

	ApplyPreInitSettings();

	if ( DxLib_Init() == -1 )
	{
		return TResult<void>::Err( "DxLib_Init failed" );
	}
	bInitialized = true;
	NDebugUI::SetImGuiTextureCallbacks( NDxLibImGuiTexture::MakeCallbacks() );
	LOG( LogPlatform, Verbose, "DxLib initialized (Direct3D version %d, working directory %s)", GetUseDxLibDirect3DVersion(), *NFileSystem::GetWorkingDirectory() );

	ApplyRenderState();
	if ( Settings.WindowMode == EWindowMode::Windowed && Settings.PositionX >= 0 && Settings.PositionY >= 0 )
	{
		SetWindowPosition( Settings.PositionX, Settings.PositionY );
	}

	if ( Settings.bEnableImGui )
	{
		InitImGui();
	}
	ApplyMouseCursor();

	// 起動引数 --screenshot=パス.png [--screenshot-frame=N] [--screenshot-exit]
	{
		const FString CommandLine = NCommandLine::Get();
		if ( const TOptional<FString> Path = NCommandLine::GetValue( CommandLine, "screenshot" ) )
		{
			ScreenshotPath = Path.GetValue();
			if ( const TOptional<FString> Frame = NCommandLine::GetValue( CommandLine, "screenshot-frame" ) )
			{
				ScreenshotFrame = Frame.GetValue().ToInt();
			}
			bScreenshotExit = NCommandLine::HasFlag( CommandLine, "screenshot-exit" );
			bScreenshotDone = false;
		}
	}

	LastCounter   = GetNowHiPerformanceCount();
	FrameStart    = LastCounter;
	TotalTime     = 0.0;
	FrameCount    = 0;
	FPSAccumTime  = 0.0;
	FPSAccumCount = 0;

	LOG( LogPlatform, Log, "Initialized %dx%d (%s), vsync %s, max fps %d, window scale %.2f", Settings.Width, Settings.Height,
		 WindowModeToString( Settings.WindowMode ), Settings.bVSync ? "on" : "off", Settings.MaxFPS, Settings.WindowExtendRate );
	LOG( LogPlatform, Log, "ImGui %s (docking %s, external windows %s, window update limit %d fps / %d per frame)",
		 bImGuiActive ? IMGUI_VERSION : "disabled", Settings.bImGuiDocking ? "on" : "off", Settings.bImGuiViewports ? "on" : "off",
		 Settings.ImGuiViewportMaxFPS, Settings.ImGuiViewportMaxPresentsPerFrame );
	return TResult<void>::Ok();
}

void CDxLibBackendSubsystem::Shutdown()
{
	if ( !bInitialized )
	{
		return;
	}

	LOG( LogPlatform, Log, "Shutting down (%llu frame(s) rendered)", static_cast<unsigned long long>( FrameCount ) );
	ShutdownImGui();
	NDebugUI::ClearImGuiTextureCache();
	NDebugUI::SetImGuiTextureCallbacks( NDebugUI::FImGuiTextureCallbacks() );
	if ( CaptureSoftImage >= 0 )
	{
		DeleteSoftImage( CaptureSoftImage );
		CaptureSoftImage = -1;
	}
	FrameCaptureSink = nullptr;
	DxLib_End();
	Viewports.Reset();
	bInitialized = false;
	PendingWindowMode.Reset();

	LOG( LogPlatform, Log, "Shutdown" );
}

void CDxLibBackendSubsystem::ApplyPreInitSettings()
{
	// DxLib に渡す文字列（ファイル名・描画する文字・フォント名・ウィンドウの題など）を UTF-8 として読ませる。
	// DxLib_Init の前に呼ぶ必要がある（ほかの設定より先にしておく）
	SetUseCharCodeFormat( DX_CHARCODEFORMAT_UTF8 );

	SetOutApplicationLogValidFlag( Settings.bOutputDxLibLog ? TRUE : FALSE );
	SetDoubleStartValidFlag( Settings.bAllowMultipleInstances ? TRUE : FALSE );
	SetMainWindowText( *Settings.Title );
	SetGraphMode( Settings.Width, Settings.Height, Settings.ColorBitDepth );
	SetZBufferBitDepth( Settings.ZBufferBitDepth );
	SetWaitVSyncFlag( Settings.bVSync ? TRUE : FALSE );
	SetAlwaysRunFlag( Settings.bAlwaysRun ? TRUE : FALSE );
	SetWindowSizeChangeEnableFlag( Settings.bResizable ? TRUE : FALSE, TRUE );
	SetWindowSizeExtendRate( Settings.WindowExtendRate );
	SetFullScreenScalingMode( ToDxScalingMode( Settings.ScalingFilter ) );

	// 実行中にウィンドウモードを変えてもグラフィックハンドルや Direct3D デバイスを作り直さない
	SetChangeScreenModeGraphicsSystemResetFlag( FALSE );

	if ( Settings.WindowMode != EWindowMode::Windowed )
	{
		SetFullScreenResolutionMode( ToDxResolutionMode( Settings.WindowMode ) );
	}
	ChangeWindowMode( Settings.WindowMode == EWindowMode::Windowed ? TRUE : FALSE );

	if ( Settings.bEnableImGui )
	{
		SetHookWinProc( ImGuiWndProcHook );
	}
}

void CDxLibBackendSubsystem::ApplyRenderState()
{
	// SetDrawScreen はカメラ設定をリセットするので先に呼ぶ
	SetDrawScreen( DX_SCREEN_BACK );
	// 3D の描画状態の初期値（描画のたびに、終わりで戻す先）。深度バッファの使い方・画像の貼り方は、DxLib から読めないので、ここで決めて覚えておく
	NDxLibRenderState3D::ApplyDefaults( Settings.bUseZBuffer3D );
	// ライティングの初期値（光の計算をする・全体の環境光は 0・材質と光の計算の仕方は DxLib の既定）。描画の終わりに戻す先として覚える
	NDxLibLighting::ApplyDefaults();
	SetCameraNearFar( Settings.CameraNear, Settings.CameraFar );
}

#pragma endregion

#pragma region Frame

bool CDxLibBackendSubsystem::BeginFrame()
{
	if ( !bInitialized )
	{
		return false;
	}

	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "ProcessMessage" );
		if ( ProcessMessage() == -1 )
		{
			LOG( LogPlatform, Log, "Window closed by the user (ProcessMessage returned -1)" );
			return false;
		}
	}
	if ( bExitRequested )
	{
		return false;
	}

	// ウィンドウモードの変更はフレームの外（ImGui のフレーム開始前）で反映する
	if ( PendingWindowMode.IsSet() )
	{
		const EWindowMode Mode = PendingWindowMode.GetValue();
		PendingWindowMode.Reset();
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "ApplyWindowMode" );
		ApplyWindowMode( Mode );
	}

	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "UpdateTime" );
		UpdateTime();
	}

	if ( bImGuiActive )
	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "ImGui", "NewFrame" );
		ImGui_ImplDXlib_NewFrame();
		ImGui::NewFrame();
	}

	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "ClearDrawScreen" );
		ClearDrawScreen();
	}
	bInFrame = true;
	return true;
}

bool CDxLibBackendSubsystem::RequestScreenshot( const FString& Path, int32 DelayFrames )
{
	if ( !bInitialized || Path.IsEmpty() )
	{
		return false;
	}
	// 今のフレームの EndFrame で保存する（起動引数の --screenshot と同じ仕組み）
	ScreenshotPath = Path;
	ScreenshotFrame = static_cast<int32>( FrameCount ) + 1 + ( DelayFrames > 0 ? DelayFrames : 0 );
	bScreenshotExit = false;
	bScreenshotDone = false;
	return true;
}

void CDxLibBackendSubsystem::SetFrameCaptureSink( IFrameCaptureSink* Sink )
{
	FrameCaptureSink = Sink;
}

void CDxLibBackendSubsystem::DeliverFrameToSink( EFrameCaptureStage Stage )
{
	const uint64 FrameIndex = FrameCount + 1;
	if ( FrameCaptureSink == nullptr || !FrameCaptureSink->WantsFrame( FrameIndex, Stage ) )
	{
		return;
	}
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "FrameCapture" );
	const int32 Width = GetScreenWidth();
	const int32 Height = GetScreenHeight();
	if ( CaptureSoftImage < 0 || CaptureWidth != Width || CaptureHeight != Height )
	{
		if ( CaptureSoftImage >= 0 )
		{
			DeleteSoftImage( CaptureSoftImage );
		}
		CaptureSoftImage = MakeARGB8ColorSoftImage( Width, Height );
		CaptureWidth = Width;
		CaptureHeight = Height;
	}
	// 溜まっている 2D の描画を先に描き切ってから読む
	RenderVertex();
	if ( CaptureSoftImage < 0 || GetDrawScreenSoftImage( 0, 0, Width, Height, CaptureSoftImage ) != 0 )
	{
		return;
	}
	// ARGB8 の CPU 側の画像は、1 画素 4 バイト（B, G, R, A の順）。行の間隔は GetPitchSoftImage
	const uint8* Bytes = static_cast<const uint8*>( GetImageAddressSoftImage( CaptureSoftImage ) );
	const int32 Pitch = GetPitchSoftImage( CaptureSoftImage );
	if ( Bytes == nullptr || Pitch < Width * 4 )
	{
		return;
	}
	CapturePixels.SetNum( Width * Height );
	for ( int32 Y = 0; Y < Height; ++Y )
	{
		const uint8* Row = Bytes + static_cast<usize>( Y ) * static_cast<usize>( Pitch );
		for ( int32 X = 0; X < Width; ++X )
		{
			CapturePixels[Y * Width + X] = FColor32( Row[X * 4 + 2], Row[X * 4 + 1], Row[X * 4 + 0], 255 );
		}
	}
	FFramePixels Frame;
	Frame.Width = Width;
	Frame.Height = Height;
	Frame.Pixels = CapturePixels.GetData();
	Frame.FrameIndex = FrameIndex;
	Frame.Stage = Stage;
	FrameCaptureSink->OnFrame( Frame );
}

void CDxLibBackendSubsystem::CaptureScreenshotIfRequested()
{
	if ( bScreenshotDone || ScreenshotPath.IsEmpty() )
	{
		return;
	}
	if ( static_cast<int32>( FrameCount ) + 1 < ScreenshotFrame )
	{
		return;
	}

	bScreenshotDone = true;
	const int Result = SaveDrawScreenToPNG( 0, 0, GetScreenWidth(), GetScreenHeight(), *ScreenshotPath );
	if ( Result == 0 )
	{
		LOG( LogPlatform, Log, "Saved a screenshot of frame %d to %s", static_cast<int32>( FrameCount ) + 1, *ScreenshotPath );
	}
	else
	{
		LOG( LogPlatform, Warning, "Failed to save the screenshot %s", *ScreenshotPath );
	}
	if ( bScreenshotExit )
	{
		bExitRequested = true;
	}
}

void CDxLibBackendSubsystem::EndFrame()
{
	if ( !bInFrame )
	{
		return;
	}

	DeliverFrameToSink( EFrameCaptureStage::Game );

	if ( bImGuiActive )
	{
		// DxLib は 2D 描画の頂点をまとめて溜めておき、後でまとめて描く。
		// ImGui（特にメインウィンドウ外のウィンドウ）は Direct3D の描画先を切り替えるので、
		// 溜まったままだと DxLib の描画が別ウィンドウに描かれたり消えたりしてちらつく。先に吐き出しておく
		{
			PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "RenderVertex" );
			RenderVertex();
		}

		{
			PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "ImGui", "Render" );
			ImGui::Render();
			ImGui_ImplDXlib_RenderDrawData();
		}
		if ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			{
				PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "ImGui", "UpdatePlatformWindows" );
				ImGui::UpdatePlatformWindows();
			}
			RenderImGuiViewports();
		}
		// ImGui が変更した Direct3D の状態を DxLib 用に戻す
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "RefreshDirect3DSetting" );
		RefreshDxLibDirect3DSetting();
	}

	CaptureScreenshotIfRequested();
	DeliverFrameToSink( EFrameCaptureStage::WithDebugUI );

	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "ScreenFlip" );
		ScreenFlip();
	}
	{
		PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "DxLib", "FrameLimiter" );
		WaitForFrameLimit();
	}

	++FrameCount;
	bInFrame = false;
}

void CDxLibBackendSubsystem::RenderImGuiViewports()
{
	Viewports.Render( Settings.ImGuiViewportMaxFPS, Settings.ImGuiViewportMaxPresentsPerFrame );
}

void CDxLibBackendSubsystem::UpdateTime()
{
	const int64 Now = GetNowHiPerformanceCount();
	float64 Delta = static_cast<float64>( Now - LastCounter ) / 1000000.0;
	LastCounter = Now;
	FrameStart  = Now;

	// ブレークポイントなどで止まった後に巨大な値が入らないようにする
	if ( Delta > 0.25 )
	{
		Delta = 0.25;
	}
	if ( Delta < 0.0 )
	{
		Delta = 0.0;
	}

	DeltaTime  = static_cast<float32>( Delta );
	TotalTime += Delta;

	FPSAccumTime += Delta;
	++FPSAccumCount;
	if ( FPSAccumTime >= 0.5 )
	{
		FPS           = static_cast<float32>( FPSAccumCount / FPSAccumTime );
		FPSAccumTime  = 0.0;
		FPSAccumCount = 0;
	}
}

void CDxLibBackendSubsystem::WaitForFrameLimit()
{
	// bVSync が有効でも、ディスプレイが省電力（画面オフ）のときは DWM が垂直同期の待ちを省いて 800 FPS 以上で空回りするため、既定で 60 FPS を上限にする
	const int32 TargetFPS = Settings.MaxFPS > 0 ? Settings.MaxFPS : ( Settings.bVSync ? 60 : 0 );
	if ( TargetFPS <= 0 )
	{
		return;
	}

	const int64 FrameMicroseconds = 1000000 / TargetFPS;
	while ( true )
	{
		const int64 Remaining = FrameMicroseconds - ( GetNowHiPerformanceCount() - FrameStart );
		if ( Remaining <= 0 )
		{
			break;
		}
		// 1ms 以上残っていれば眠り、最後は細かく待つ
		if ( Remaining > 1500 )
		{
			Sleep( static_cast<DWORD>( ( Remaining - 1000 ) / 1000 ) );
		}
		else
		{
			Sleep( 0 );
		}
	}
}

#pragma endregion

#pragma region Window

void CDxLibBackendSubsystem::SetWindowMode( EWindowMode Mode )
{
	if ( !bInitialized )
	{
		Settings.WindowMode = Mode;
		if ( Mode != EWindowMode::Windowed )
		{
			LastFullscreenMode = Mode;
		}
		LOG( LogPlatform, Verbose, "Window mode set before initialization: %s", WindowModeToString( Mode ) );
		return;
	}
	PendingWindowMode = Mode;
	LOG( LogPlatform, Verbose, "Window mode change requested: %s (applied at the start of the next frame)", WindowModeToString( Mode ) );
}

void CDxLibBackendSubsystem::ToggleFullscreen()
{
	const EWindowMode Current = PendingWindowMode.Get( Settings.WindowMode );
	SetWindowMode( Current == EWindowMode::Windowed ? LastFullscreenMode : EWindowMode::Windowed );
}

bool CDxLibBackendSubsystem::ApplyWindowMode( EWindowMode Mode )
{
	if ( Mode == Settings.WindowMode )
	{
		LOG( LogPlatform, Verbose, "Window mode is already %s", WindowModeToString( Mode ) );
		return true;
	}

	// Borderless <-> Fullscreen は一度ウィンドウに戻してから切り替える
	if ( Settings.WindowMode != EWindowMode::Windowed && Mode != EWindowMode::Windowed )
	{
		ChangeWindowMode( TRUE );
	}

	if ( Mode != EWindowMode::Windowed )
	{
		SetFullScreenResolutionMode( ToDxResolutionMode( Mode ) );
	}
	const int Result = ChangeWindowMode( Mode == EWindowMode::Windowed ? TRUE : FALSE );

	if ( Result != DX_CHANGESCREEN_OK && Result != DX_CHANGESCREEN_REFRESHNORMAL )
	{
		LOG( LogPlatform, Warning, "Failed to change window mode to %s (DxLib result %d)", WindowModeToString( Mode ), Result );
		Settings.WindowMode = GetWindowModeFlag() ? EWindowMode::Windowed : Settings.WindowMode;
		ApplyRenderState();
		ApplyMouseCursor();
		return false;
	}

	Settings.WindowMode = Mode;
	if ( Mode != EWindowMode::Windowed )
	{
		LastFullscreenMode = Mode;
	}
	else
	{
		SetWindowSizeExtendRate( Settings.WindowExtendRate );
	}

	ApplyRenderState();
	ApplyMouseCursor();
	LOG( LogPlatform, Log, "Window mode: %s", WindowModeToString( Mode ) );
	return true;
}

void CDxLibBackendSubsystem::SetWindowTitle( const FString& Title )
{
	Settings.Title = Title;
	if ( bInitialized )
	{
		SetMainWindowText( *Settings.Title );
	}
	LOG( LogPlatform, Verbose, "Window title: %s", *Title );
}

void CDxLibBackendSubsystem::SetMouseCursorVisible( bool bVisible )
{
	if ( Settings.bShowMouseCursor == bVisible )
	{
		return;
	}
	Settings.bShowMouseCursor = bVisible;
	if ( bInitialized )
	{
		ApplyMouseCursor();
	}
	LOG( LogPlatform, Verbose, "Mouse cursor %s", bVisible ? "shown" : "hidden" );
}

void CDxLibBackendSubsystem::ApplyMouseCursor()
{
	SetMouseDispFlag( Settings.bShowMouseCursor ? TRUE : FALSE );

	// ImGui の Win32 バックエンドは毎フレーム / WM_SETCURSOR でカーソルを設定し直すので、
	// 非表示にしたいときは ImGui にカーソルを触らせない
	if ( bImGuiActive )
	{
		ImGuiIO& IO = ImGui::GetIO();
		if ( Settings.bShowMouseCursor )
		{
			IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}
		else
		{
			IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		}
	}
}

void CDxLibBackendSubsystem::SetWindowExtendRate( float64 Rate )
{
	Settings.WindowExtendRate = Rate;
	if ( bInitialized && Settings.WindowMode == EWindowMode::Windowed )
	{
		SetWindowSizeExtendRate( Rate );
	}
	LOG( LogPlatform, Verbose, "Window scale: %.2f%s", Rate, Settings.WindowMode == EWindowMode::Windowed ? "" : " (applied when back to windowed)" );
}

void CDxLibBackendSubsystem::GetDesktopSize( int32& OutWidth, int32& OutHeight ) const
{
	int Width = 0, Height = 0, ColorBitDepth = 0;
	GetDefaultState( &Width, &Height, &ColorBitDepth );
	OutWidth  = Width;
	OutHeight = Height;
}

#pragma endregion

#pragma region ImGui

void CDxLibBackendSubsystem::InitImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	if ( Settings.bImGuiDocking )
	{
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}
	if ( Settings.bImGuiViewports )
	{
		IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	}
	if ( !Settings.bImGuiSaveIni )
	{
		IO.IniFilename = nullptr;
	}

	if ( NFileSystem::FileExists( Settings.ImGuiFontPath ) )
	{
		// ImGui 1.92 以降は使われた文字だけ必要になったときに読み込むので、日本語の文字範囲の指定は要らない
		IO.Fonts->AddFontFromFileTTF( *Settings.ImGuiFontPath, Settings.ImGuiFontSize );
		LOG( LogPlatform, Verbose, "ImGui font: %s (%.0f px)", *Settings.ImGuiFontPath, Settings.ImGuiFontSize );
	}
	else
	{
		LOG( LogPlatform, Warning, "ImGui font not found: %s (using default font)", *Settings.ImGuiFontPath );
	}

	if ( !ImGui_ImplDXlib_Init() )
	{
		LOG( LogPlatform, Error, "ImGui_ImplDXlib_Init failed" );
		ImGui::DestroyContext();
		return;
	}
	bImGuiActive = true;
	LOG( LogPlatform, Verbose, "ImGui %s context created (ini: %s)", IMGUI_VERSION, IO.IniFilename ? IO.IniFilename : "(not saved)" );
}

void CDxLibBackendSubsystem::ShutdownImGui()
{
	if ( !bImGuiActive )
	{
		return;
	}

	ImGui_ImplDXlib_Shutdown();
	ImGui::DestroyContext();
	bImGuiActive = false;
	LOG( LogPlatform, Verbose, "ImGui shut down" );
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

bool CDxLibBackendSubsystem::IsInitialized() const
{
	return bInitialized;
}

void CDxLibBackendSubsystem::RequestExit()
{
	if ( !bExitRequested )
	{
		LOG( LogPlatform, Log, "Exit requested" );
	}
	bExitRequested = true;
}

bool CDxLibBackendSubsystem::IsExitRequested() const
{
	return bExitRequested;
}

float32 CDxLibBackendSubsystem::GetDeltaTime() const
{
	return DeltaTime;
}

float64 CDxLibBackendSubsystem::GetTime() const
{
	return TotalTime;
}

uint64 CDxLibBackendSubsystem::GetFrameCount() const
{
	return FrameCount;
}

float32 CDxLibBackendSubsystem::GetFPS() const
{
	return FPS;
}

int32 CDxLibBackendSubsystem::GetScreenWidth() const
{
	return Settings.Width;
}

int32 CDxLibBackendSubsystem::GetScreenHeight() const
{
	return Settings.Height;
}

EWindowMode CDxLibBackendSubsystem::GetWindowMode() const
{
	return Settings.WindowMode;
}

const FString& CDxLibBackendSubsystem::GetWindowTitle() const
{
	return Settings.Title;
}

bool CDxLibBackendSubsystem::IsMouseCursorVisible() const
{
	return Settings.bShowMouseCursor;
}

#pragma endregion
