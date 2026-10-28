#include "DxLibBackend.h"


#include "Library/Core/Collection/Set.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/CString.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/CommandLine.h"
#include "Library/Platform/FileSystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "VendorModules/ImGui/imgui_impl_dxlib.hpp"

DEFINE_LOG_CATEGORY( LogPlatform )

#ifdef _DEBUG
// ImGui の IM_ASSERT から呼ばれる（VendorModules/ImGui/imconfig.h）。原因をログに残してから止める
void CoGImGuiAssertFailed( const char* Expression, const char* File, int Line )
{
	LOG( LogPlatform, Fatal, "ImGui assertion failed: %s (%s:%d)", Expression, File, Line );
}
#endif

REGISTER_SUBSYSTEM( CDxLibBackend )

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

void CDxLibBackend::Initialize( FSubsystemCollection& Collection )
{
	const TResult<void> InitResult = Init( GetEngine()->GetStartupSettings() );
	if ( InitResult.IsErr() )
	{
		// 失敗は IsInitialized() == false として CEngine が検出する
		LOG( LogPlatform, Error, "%s", *InitResult.UnwrapErr() );
	}
}

void CDxLibBackend::Deinitialize()
{
	Shutdown();
}

TResult<void> CDxLibBackend::Init( const FWindowSettings& InSettings )
{
	if ( bInitialized ) return TResult<void>::Err( "CDxLibBackend is already initialized" );

	Settings       = InSettings;
	bExitRequested = false;
	if ( Settings.WindowMode != EWindowMode::Windowed ) LastFullscreenMode = Settings.WindowMode;

	ApplyPreInitSettings();

	if ( DxLib_Init() == -1 ) return TResult<void>::Err( "DxLib_Init failed" );
	bInitialized = true;
	LOG( LogPlatform, Verbose, "DxLib initialized (Direct3D version %d, working directory %s)", GetUseDxLibDirect3DVersion(), *NFileSystem::GetWorkingDirectory() );

	ApplyRenderState();
	if ( Settings.WindowMode == EWindowMode::Windowed && Settings.PositionX >= 0 && Settings.PositionY >= 0 )
	{
		SetWindowPosition( Settings.PositionX, Settings.PositionY );
	}

	if ( Settings.bEnableImGui ) InitImGui();
	ApplyMouseCursor();

	// 起動引数 --screenshot=パス.png [--screenshot-frame=N] [--screenshot-exit]
	{
		const FString CommandLine = NCommandLine::Get();
		if ( const TOptional<FString> Path = NCommandLine::GetValue( CommandLine, "screenshot" ) )
		{
			ScreenshotPath = Path.GetValue();
			if ( const TOptional<FString> Frame = NCommandLine::GetValue( CommandLine, "screenshot-frame" ) ) ScreenshotFrame = Frame.GetValue().ToInt();
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

void CDxLibBackend::Shutdown()
{
	if ( !bInitialized ) return;

	LOG( LogPlatform, Log, "Shutting down (%llu frame(s) rendered)", static_cast<unsigned long long>( FrameCount ) );
	ShutdownImGui();
	DxLib_End();
	bInitialized = false;
	PendingWindowMode.Reset();

	LOG( LogPlatform, Log, "Shutdown" );
}

void CDxLibBackend::ApplyPreInitSettings()
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

	if ( Settings.WindowMode != EWindowMode::Windowed ) SetFullScreenResolutionMode( ToDxResolutionMode( Settings.WindowMode ) );
	ChangeWindowMode( Settings.WindowMode == EWindowMode::Windowed ? TRUE : FALSE );

	if ( Settings.bEnableImGui ) SetHookWinProc( ImGuiWndProcHook );
}

void CDxLibBackend::ApplyRenderState()
{
	// SetDrawScreen はカメラ設定をリセットするので先に呼ぶ
	SetDrawScreen( DX_SCREEN_BACK );
	SetUseZBuffer3D( Settings.bUseZBuffer3D ? TRUE : FALSE );
	SetWriteZBuffer3D( Settings.bUseZBuffer3D ? TRUE : FALSE );
	SetCameraNearFar( Settings.CameraNear, Settings.CameraFar );
}

#pragma endregion

#pragma region Frame

bool CDxLibBackend::BeginFrame()
{
	if ( !bInitialized ) return false;

	{
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "ProcessMessage" );
		if ( ProcessMessage() == -1 )
		{
			LOG( LogPlatform, Log, "Window closed by the user (ProcessMessage returned -1)" );
			return false;
		}
	}
	if ( bExitRequested ) return false;

	// ウィンドウモードの変更はフレームの外（ImGui のフレーム開始前）で反映する
	if ( PendingWindowMode.IsSet() )
	{
		const EWindowMode Mode = PendingWindowMode.GetValue();
		PendingWindowMode.Reset();
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "ApplyWindowMode" );
		ApplyWindowMode( Mode );
	}

	{
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "UpdateTime" );
		UpdateTime();
	}

	if ( bImGuiActive )
	{
		PROFILE_SCOPE_OWNER( "Platform", "ImGui", "NewFrame" );
		ImGui_ImplDXlib_NewFrame();
		ImGui::NewFrame();
	}

	{
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "ClearDrawScreen" );
		ClearDrawScreen();
	}
	bInFrame = true;
	return true;
}

bool CDxLibBackend::RequestScreenshot( const FString& Path, int32 DelayFrames )
{
	if ( !bInitialized || Path.IsEmpty() ) return false;
	// 今のフレームの EndFrame で保存する（起動引数の --screenshot と同じ仕組み）
	ScreenshotPath = Path;
	ScreenshotFrame = static_cast<int32>( FrameCount ) + 1 + ( DelayFrames > 0 ? DelayFrames : 0 );
	bScreenshotExit = false;
	bScreenshotDone = false;
	return true;
}

void CDxLibBackend::CaptureScreenshotIfRequested()
{
	if ( bScreenshotDone || ScreenshotPath.IsEmpty() ) return;
	if ( static_cast<int32>( FrameCount ) + 1 < ScreenshotFrame ) return;

	bScreenshotDone = true;
	const int Result = SaveDrawScreenToPNG( 0, 0, GetScreenWidth(), GetScreenHeight(), *ScreenshotPath );
	if ( Result == 0 ) LOG( LogPlatform, Log, "Saved a screenshot of frame %d to %s", static_cast<int32>( FrameCount ) + 1, *ScreenshotPath );
	else LOG( LogPlatform, Warning, "Failed to save the screenshot %s", *ScreenshotPath );
	if ( bScreenshotExit ) bExitRequested = true;
}

void CDxLibBackend::EndFrame()
{
	if ( !bInFrame ) return;

	if ( bImGuiActive )
	{
		// DxLib は 2D 描画の頂点をまとめて溜めておき、後でまとめて描く。
		// ImGui（特にメインウィンドウ外のウィンドウ）は Direct3D の描画先を切り替えるので、
		// 溜まったままだと DxLib の描画が別ウィンドウに描かれたり消えたりしてちらつく。先に吐き出しておく
		{
			PROFILE_SCOPE_OWNER( "Platform", "DxLib", "RenderVertex" );
			RenderVertex();
		}

		{
			PROFILE_SCOPE_OWNER( "Platform", "ImGui", "Render" );
			ImGui::Render();
			ImGui_ImplDXlib_RenderDrawData();
		}
		if ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			{
				PROFILE_SCOPE_OWNER( "Platform", "ImGui", "UpdatePlatformWindows" );
				ImGui::UpdatePlatformWindows();
			}
			RenderImGuiViewports();
		}
		// ImGui が変更した Direct3D の状態を DxLib 用に戻す
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "RefreshDirect3DSetting" );
		RefreshDxLibDirect3DSetting();
	}

	CaptureScreenshotIfRequested();

	{
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "ScreenFlip" );
		ScreenFlip();
	}
	{
		PROFILE_SCOPE_OWNER( "Platform", "DxLib", "FrameLimiter" );
		WaitForFrameLimit();
	}

	++FrameCount;
	bInFrame = false;
}

void CDxLibBackend::RenderImGuiViewports()
{
	// ImGui::RenderPlatformWindowsDefault と同じ手順を、表示する外部ウィンドウを選んだ上で行う
	ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
	const int64 Now = GetNowHiPerformanceCount();
	const int64 MinInterval = Settings.ImGuiViewportMaxFPS > 0 ? 1000000 / Settings.ImGuiViewportMaxFPS : 0;

	struct FCandidate
	{
		ImGuiViewport* Viewport = nullptr;
		int64 LastPresentTime = 0;
	};
	TArray<FCandidate> Candidates;
	TSet<uint32> AliveIds;

	// index 0 はメインウィンドウ（アプリ側で描画済み）
	for ( int32 i = 1; i < PlatformIO.Viewports.Size; ++i )
	{
		ImGuiViewport* Viewport = PlatformIO.Viewports[i];
		AliveIds.Add( Viewport->ID );
		if ( Viewport->Flags & ImGuiViewportFlags_IsMinimized ) continue;

		// 初めて表示するウィンドウは時刻 0（= 一番古い）として最優先で表示する
		const int64* Last = ViewportLastPresentTime.Find( Viewport->ID );
		const int64 LastPresentTime = Last ? *Last : 0;
		if ( Last == nullptr || Now - LastPresentTime >= MinInterval )
		{
			Candidates.Add( { Viewport, LastPresentTime } );
		}
	}

	// 一番長く更新していないウィンドウから表示する（回数に上限があっても全ウィンドウが順番に更新される）
	Candidates.StableSort( []( const FCandidate& A, const FCandidate& B ) { return A.LastPresentTime < B.LastPresentTime; } );
	const int32 MaxPresents = Settings.ImGuiViewportMaxPresentsPerFrame;
	if ( MaxPresents > 0 && Candidates.Num() > MaxPresents ) Candidates.RemoveAt( MaxPresents, Candidates.Num() - MaxPresents );

	for ( const FCandidate& Candidate : Candidates )
	{
		FProfileScope Scope( "Platform", "ImGui", "Viewport.Render", Candidate.Viewport->ID );
		if ( PlatformIO.Platform_RenderWindow ) PlatformIO.Platform_RenderWindow( Candidate.Viewport, nullptr );
		if ( PlatformIO.Renderer_RenderWindow ) PlatformIO.Renderer_RenderWindow( Candidate.Viewport, nullptr );
	}
	for ( const FCandidate& Candidate : Candidates )
	{
		// 環境によっては 1 回の Present が垂直同期 1 回分かかる。どのウィンドウが何 ms かかったかをここで見る
		FProfileScope Scope( "Platform", "ImGui", "Viewport.Present", Candidate.Viewport->ID );
		if ( PlatformIO.Platform_SwapBuffers ) PlatformIO.Platform_SwapBuffers( Candidate.Viewport, nullptr );
		if ( PlatformIO.Renderer_SwapBuffers ) PlatformIO.Renderer_SwapBuffers( Candidate.Viewport, nullptr );
		ViewportLastPresentTime.Add( Candidate.Viewport->ID, Now );
	}

	// 閉じられたウィンドウの記録を消す
	ViewportLastPresentTime.RemoveAll( [&AliveIds]( uint32 Id, int64 ) { return !AliveIds.Contains( Id ); } );
}

void CDxLibBackend::UpdateTime()
{
	const int64 Now = GetNowHiPerformanceCount();
	float64 Delta = static_cast<float64>( Now - LastCounter ) / 1000000.0;
	LastCounter = Now;
	FrameStart  = Now;

	// ブレークポイントなどで止まった後に巨大な値が入らないようにする
	if ( Delta > 0.25 ) Delta = 0.25;
	if ( Delta < 0.0 ) Delta = 0.0;

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

void CDxLibBackend::WaitForFrameLimit()
{
	if ( Settings.MaxFPS <= 0 ) return;

	const int64 FrameMicroseconds = 1000000 / Settings.MaxFPS;
	while ( true )
	{
		const int64 Remaining = FrameMicroseconds - ( GetNowHiPerformanceCount() - FrameStart );
		if ( Remaining <= 0 ) break;
		// 1ms 以上残っていれば眠り、最後は細かく待つ
		if ( Remaining > 1500 ) Sleep( static_cast<DWORD>( ( Remaining - 1000 ) / 1000 ) );
		else Sleep( 0 );
	}
}

#pragma endregion

#pragma region Window

void CDxLibBackend::SetWindowMode( EWindowMode Mode )
{
	if ( !bInitialized )
	{
		Settings.WindowMode = Mode;
		if ( Mode != EWindowMode::Windowed ) LastFullscreenMode = Mode;
		LOG( LogPlatform, Verbose, "Window mode set before initialization: %s", WindowModeToString( Mode ) );
		return;
	}
	PendingWindowMode = Mode;
	LOG( LogPlatform, Verbose, "Window mode change requested: %s (applied at the start of the next frame)", WindowModeToString( Mode ) );
}

void CDxLibBackend::ToggleFullscreen()
{
	const EWindowMode Current = PendingWindowMode.Get( Settings.WindowMode );
	SetWindowMode( Current == EWindowMode::Windowed ? LastFullscreenMode : EWindowMode::Windowed );
}

bool CDxLibBackend::ApplyWindowMode( EWindowMode Mode )
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

	if ( Mode != EWindowMode::Windowed ) SetFullScreenResolutionMode( ToDxResolutionMode( Mode ) );
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
	if ( Mode != EWindowMode::Windowed ) LastFullscreenMode = Mode;
	else SetWindowSizeExtendRate( Settings.WindowExtendRate );

	ApplyRenderState();
	ApplyMouseCursor();
	LOG( LogPlatform, Log, "Window mode: %s", WindowModeToString( Mode ) );
	return true;
}

void CDxLibBackend::SetWindowTitle( const FString& Title )
{
	Settings.Title = Title;
	if ( bInitialized ) SetMainWindowText( *Settings.Title );
	LOG( LogPlatform, Verbose, "Window title: %s", *Title );
}

void CDxLibBackend::SetMouseCursorVisible( bool bVisible )
{
	if ( Settings.bShowMouseCursor == bVisible ) return;
	Settings.bShowMouseCursor = bVisible;
	if ( bInitialized ) ApplyMouseCursor();
	LOG( LogPlatform, Verbose, "Mouse cursor %s", bVisible ? "shown" : "hidden" );
}

void CDxLibBackend::ApplyMouseCursor()
{
	SetMouseDispFlag( Settings.bShowMouseCursor ? TRUE : FALSE );

	// ImGui の Win32 バックエンドは毎フレーム / WM_SETCURSOR でカーソルを設定し直すので、
	// 非表示にしたいときは ImGui にカーソルを触らせない
	if ( bImGuiActive )
	{
		ImGuiIO& IO = ImGui::GetIO();
		if ( Settings.bShowMouseCursor ) IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		else IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	}
}

void CDxLibBackend::SetWindowExtendRate( float64 Rate )
{
	Settings.WindowExtendRate = Rate;
	if ( bInitialized && Settings.WindowMode == EWindowMode::Windowed ) SetWindowSizeExtendRate( Rate );
	LOG( LogPlatform, Verbose, "Window scale: %.2f%s", Rate, Settings.WindowMode == EWindowMode::Windowed ? "" : " (applied when back to windowed)" );
}

void CDxLibBackend::GetDesktopSize( int32& OutWidth, int32& OutHeight ) const
{
	int Width = 0, Height = 0, ColorBitDepth = 0;
	GetDefaultState( &Width, &Height, &ColorBitDepth );
	OutWidth  = Width;
	OutHeight = Height;
}

#pragma endregion

#pragma region ImGui

void CDxLibBackend::InitImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	if ( Settings.bImGuiDocking ) IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	if ( Settings.bImGuiViewports ) IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	if ( !Settings.bImGuiSaveIni ) IO.IniFilename = nullptr;

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

void CDxLibBackend::ShutdownImGui()
{
	if ( !bImGuiActive ) return;

	ImGui_ImplDXlib_Shutdown();
	ImGui::DestroyContext();
	bImGuiActive = false;
	LOG( LogPlatform, Verbose, "ImGui shut down" );
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

bool CDxLibBackend::IsInitialized() const
{
	return bInitialized;
}

void CDxLibBackend::RequestExit()
{
	if ( !bExitRequested ) LOG( LogPlatform, Log, "Exit requested" );
	bExitRequested = true;
}

bool CDxLibBackend::IsExitRequested() const
{
	return bExitRequested;
}

float32 CDxLibBackend::GetDeltaTime() const
{
	return DeltaTime;
}

float64 CDxLibBackend::GetTime() const
{
	return TotalTime;
}

uint64 CDxLibBackend::GetFrameCount() const
{
	return FrameCount;
}

float32 CDxLibBackend::GetFPS() const
{
	return FPS;
}

int32 CDxLibBackend::GetScreenWidth() const
{
	return Settings.Width;
}

int32 CDxLibBackend::GetScreenHeight() const
{
	return Settings.Height;
}

EWindowMode CDxLibBackend::GetWindowMode() const
{
	return Settings.WindowMode;
}

const FString& CDxLibBackend::GetWindowTitle() const
{
	return Settings.Title;
}

bool CDxLibBackend::IsMouseCursorVisible() const
{
	return Settings.bShowMouseCursor;
}

#pragma endregion
