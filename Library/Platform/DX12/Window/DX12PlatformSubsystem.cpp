// SPDX-License-Identifier: MIT
#include "DX12PlatformSubsystem.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Framework/Render/Thread/RenderCommandThread.h"
#include "Library/Framework/Render/Thread/ThreadedRenderSurface.h"
#include "Library/Platform/DX12/ImGui/Frame/DX12ImGuiFrameRenderer.h"
#include "Library/Platform/DX12/Render/Game/DX12GameRenderer.h"
#include "Library/Platform/DX12/Render/Game/DX12GameCapture.h"
#include "Library/Platform/DX12/Render/Game/DX12GameSurface.h"
#include "Library/Platform/DX12/Render/Target/DX12RenderTargetPass.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "VendorModules/ImGui/imgui_impl_win32.h"

REGISTER_SUBSYSTEM( CDX12PlatformSubsystem )

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND Window, UINT Message, WPARAM WParam, LPARAM LParam );

namespace
{
	// この実装の窓だけが受け取るイベント。
	LRESULT CALLBACK WindowProc( HWND Window, UINT Message, WPARAM WParam, LPARAM LParam )
	{
		auto *Owner = reinterpret_cast<CDX12PlatformSubsystem *>( GetWindowLongPtrW( Window, GWLP_USERDATA ) ); // 窓の所有者。
		if ( Message == WM_NCCREATE )
		{
			Owner = static_cast<CDX12PlatformSubsystem *>( reinterpret_cast<CREATESTRUCTW *>( LParam )->lpCreateParams );
			SetWindowLongPtrW( Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( Owner ) );
		}
		if ( Owner )
		{
			if ( Message == WM_CLOSE )
			{
				Owner->RequestExit();
				return 0;
			}
			if ( Message == WM_SIZE )
			{
				Owner->HandleWindowResize( static_cast<uint32>( WParam ), static_cast<int32>( LOWORD( LParam ) ), static_cast<int32>( HIWORD( LParam ) ) );
			}
			if ( Message == WM_MOUSEWHEEL || Message == WM_MOUSEHWHEEL )
			{
				const float32 Amount = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( WParam ) ) / WHEEL_DELTA; // 1段を1とする。
				Owner->AddWheel( Message == WM_MOUSEHWHEEL ? Amount : 0, Message == WM_MOUSEWHEEL ? Amount : 0 );
			}
			if ( Message == WM_SETCURSOR && LOWORD( LParam ) == HTCLIENT && !Owner->IsMouseCursorVisible() )
			{
				SetCursor( nullptr );
				return TRUE;
			}
		}
		if ( ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler( Window, Message, WParam, LParam ) )
		{
			return TRUE;
		}
		return DefWindowProcW( Window, Message, WParam, LParam );
	}
} // namespace

CDX12PlatformSubsystem::~CDX12PlatformSubsystem()
{
	Deinitialize();
}

bool CDX12PlatformSubsystem::ShouldCreateSubsystem( CEngine & ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CDX12PlatformSubsystem::Initialize( FSubsystemCollection &Collection )
{
	GAME_THREAD_ONLY();
	// 終了時は専用スレッドを止めてから、計測先の所有者を解放する。
	Collection.InitializeDependency<IProfiler>();
	Settings = GetEngine()->GetStartupSettings();
	if ( Settings.Width <= 0 || Settings.Height <= 0 || Settings.Width > 16384 || Settings.Height > 16384 || !NMath::IsFinite( Settings.WindowExtendRate ) || Settings.WindowExtendRate <= 0 || Settings.WindowExtendRate > 16 )
	{
		LOG( LogDX12, Error, "Invalid DX12 window size" );
		return;
	}
	const auto Limit = NCommandLine::GetValue( "max-frames" ); // 検査用のフレーム上限。
	MaxFrames = Limit.IsSet() ? NMath::Max( 0, Limit.GetValue().ToInt() ) : 0;
	WNDCLASSEXW Class = {}; // 同一プロセス内の専用クラス。
	Class.cbSize = sizeof( Class );
	Class.lpfnWndProc = WindowProc;
	Class.hInstance = GetModuleHandleW( nullptr );
	Class.hCursor = LoadCursorW( nullptr, MAKEINTRESOURCEW( 32512 ) );
	Class.lpszClassName = L"MazigatiDX12";
	if ( !RegisterClassExW( &Class ) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
	{
		return;
	}
	const auto Resizable = NCommandLine::GetValue( "dx12-resizable" ); // 論理・SwapChain追従リサイズの起動スイッチ。
	if ( Resizable.IsSet() )
	{
		Settings.bResizable = !Resizable.GetValue().Equals( "false", ESearchCase::IgnoreCase );
	}
	else if ( NCommandLine::HasFlag( "dx12-resizable" ) )
	{
		Settings.bResizable = true;
	}
	const DWORD Style = Settings.bResizable ? WS_OVERLAPPEDWINDOW : WS_OVERLAPPEDWINDOW & ~( WS_THICKFRAME | WS_MAXIMIZEBOX ); // 窓の枠。
	RECT Rect = { 0, 0, static_cast<LONG>( Settings.Width * Settings.WindowExtendRate ), static_cast<LONG>( Settings.Height * Settings.WindowExtendRate ) }; // クライアント領域。
	AdjustWindowRect( &Rect, Style, FALSE );
	Window = CreateWindowExW( 0, Class.lpszClassName, Settings.Title.ToWide().GetData(), Style,
							  Settings.PositionX < 0 ? CW_USEDEFAULT : Settings.PositionX, Settings.PositionY < 0 ? CW_USEDEFAULT : Settings.PositionY,
							  Rect.right - Rect.left, Rect.bottom - Rect.top, nullptr, nullptr, Class.hInstance, this );
	if ( !Window )
	{
		Deinitialize();
		return;
	}
	Device = new CDX12Device();
	const auto Warp = NCommandLine::GetValue( "dx12-warp" ); // UIの有無とは独立したGPU選択。
	if ( !Device->Initialize( static_cast<HWND>( Window ), Settings.Width, Settings.Height, Warp.IsSet() && Warp.GetValue().Equals( "true", ESearchCase::IgnoreCase ) ) || !GameText.Initialize() )
	{
		LOG( LogDX12, Error, "DX12 game resources could not initialize" );
		Deinitialize();
		return;
	}
	GameRenderer = new CDX12GameRenderer();
	if ( !GameRenderer->Initialize( Device->GetNativeDevice(), Settings.Width, Settings.Height, *Device->GetTextureResources(), GameText, nullptr, nullptr, Device->GetBufferResources(), Device->GetNativeQueue(), Device->GetTextureResources(), Device->GetGraphicsPipelineStore(), Device->GetUploadArena() ) )
	{
		Deinitialize();
		return;
	}
	if ( Settings.bEnableImGui )
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		bContext = true;
		ImGuiIO &IO = ImGui::GetIO(); // UIの設定はゲーム文字や描画先へ適用しない。
		IO.IniFilename = Settings.bImGuiSaveIni ? "imgui.ini" : nullptr;
		if ( Settings.bImGuiDocking )
		{
			IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		}
		if ( Settings.bImGuiViewports )
		{
			IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		}
		if ( !Settings.ImGuiFontPath.IsEmpty() && NFileSystem::FileExists( Settings.ImGuiFontPath ) )
		{
			IO.Fonts->AddFontFromFileTTF( *Settings.ImGuiFontPath, Settings.ImGuiFontSize );
		}
		bWin32 = ImGui_ImplWin32_Init( Window );
		ImGuiFrameRenderer = new CDX12ImGuiFrameRenderer();
		if ( !bWin32 || !ImGuiFrameRenderer->Initialize( *Device ) )
		{
			Deinitialize();
			return;
		}
	}
	const EWindowMode InitialMode = Settings.WindowMode; // 窓の作成後に起動モードへ移す。
	CaptureRenderer = new CDX12GameCapture( *Device, GameText );
	TargetPassRenderer = new CDX12RenderTargetPass();
	if ( !TargetPassRenderer->Initialize( Device->GetNativeDevice(), Device->GetNativeQueue(), *Device->GetTextureResources(), GameText, Device->GetImageDescriptorHeap(), Device->GetBufferResources(), Device->GetGraphicsPipelineStore(), Device->GetTimestampStore(), Device->GetUploadArena() ) )
	{
		Deinitialize();
		return;
	}
	NativeSurface = new CDX12GameSurface( *Device, *GameRenderer, *CaptureRenderer, TargetPassRenderer );
	RenderCommands = new CRenderCommandThread();
	if ( !RenderCommands->IsReady() )
	{
		LOG( LogDX12, Error, "DX12 render thread could not bind" );
		Deinitialize();
		return;
	}
	ThreadedSurface = new CThreadedRenderSurface( *NativeSurface, *RenderCommands );
	Settings.WindowMode = EWindowMode::Windowed;
	SetWindowMode( InitialMode );
	ShowWindow( static_cast<HWND>( Window ), SW_SHOW );
	LastTick = NPlatformTime::GetTicks();
	bInitialized = true;
	LOG( LogDX12, Log, "DX12 initialized (%dx%d). Static FBX/OBJ models supported; lighting, animated models, audio and general render target assets are not supported yet.", Settings.Width, Settings.Height );
}

void CDX12PlatformSubsystem::Deinitialize()
{
	GAME_THREAD_ONLY();
	// 窓側の再入でも、新しい作成や非同期受付を開始させない。
	bInitialized = false;
	// 受付済みの命令を残さず実行し、Targetへの借用を戻す。
	delete ThreadedSurface;
	ThreadedSurface = nullptr;
	FrameCaptureSink = nullptr;
	if ( bInFrame && bContext )
	{
		ImGui::EndFrame();
	}
	bInFrame = false;
	bMinimized = false;
	bPendingResize = false;
	PendingResizeWidth = 0;
	PendingResizeHeight = 0;
	auto StopGame = [this]()
	{
		delete NativeSurface;
		NativeSurface = nullptr;
		delete TargetPassRenderer;
		TargetPassRenderer = nullptr;
		delete CaptureRenderer;
		CaptureRenderer = nullptr;
		while ( Device && !Device->PrepareShutdown( 1000 ) )
		{
			Sleep( 1 );
		}
		delete GameRenderer;
		GameRenderer = nullptr;
	};
	ExecuteRender( MoveTemp( StopGame ) );
	delete RenderCommands;
	RenderCommands = nullptr;
	// UIの外部窓と交換用画面は、作成した窓スレッドで解放する。
	delete ImGuiFrameRenderer;
	ImGuiFrameRenderer = nullptr;
	delete Device;
	Device = nullptr;
	CapturePixels.Reset();
	Viewports.Reset();
	if ( bWin32 )
	{
		ImGui_ImplWin32_Shutdown();
		bWin32 = false;
	}
	if ( bContext )
	{
		ImGui::DestroyContext();
		bContext = false;
	}
	if ( Window )
	{
		DestroyWindow( static_cast<HWND>( Window ) );
		Window = nullptr;
	}
	UnregisterClassW( L"MazigatiDX12", GetModuleHandleW( nullptr ) );
	bInitialized = false;
	bExit = false;
	FrameCount = 0;
	TotalTime = 0;
	DeltaTime = 0;
}

bool CDX12PlatformSubsystem::BeginFrame()
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || bInFrame || bExit || ( MaxFrames > 0 && FrameCount >= static_cast<uint64>( MaxFrames ) ) )
	{
		return false;
	}
	MSG Message = {}; // OSに届いた通知。
	while ( PeekMessageW( &Message, nullptr, 0, 0, PM_REMOVE ) )
	{
		if ( Message.message == WM_QUIT )
		{
			bExit = true;
		}
		TranslateMessage( &Message );
		DispatchMessageW( &Message );
	}
	if ( bExit )
	{
		return false;
	}
	if ( bPendingResize && !bMinimized )
	{
		const int32 NewWidth = PendingResizeWidth;
		const int32 NewHeight = PendingResizeHeight;
		bPendingResize = false;
		// DXGIが窓へ通知を送っても相互待機にならないよう、専用側を空にして窓側で交換先を更新する。
		const bool bDrained = ThreadedSurface->Flush() && RenderCommands->WaitForCommands(); // GPU接続を排他的に借りる前のCPU完了。
		bWindowRenderPhase = true;
		const bool bSwapResized = bDrained && Device->Resize( NewWidth, NewHeight ); // この中のGPU完了確認が失敗すれば資源を回収しない。
		bWindowRenderPhase = false;
		bool bResized = false; // 描画スレッドに所有を戻してから主画面の深度を張り替える。
		auto ApplyResize = [this, NewWidth, NewHeight, &bResized]()
		{
			// ResizeがGPU完了を確認した後だけ、別パスの提出参照と主画面の深度を戻す。
			if ( TargetPassRenderer )
			{
				TargetPassRenderer->CollectAfterGPUIdle();
			}
			bResized = GameRenderer->Resize( NewWidth, NewHeight );
		};
		if ( !bSwapResized || !ExecuteRender( MoveTemp( ApplyResize ) ) || !bResized )
		{
			LOG( LogDX12, Error, "DX12 resize failed" );
			bExit = true;
			return false;
		}
		Settings.Width = NewWidth;
		Settings.Height = NewHeight;
	}
	WheelX = PendingWheelX;
	WheelY = PendingWheelY;
	PendingWheelX = PendingWheelY = 0;
	const uint32 BackBuffer = Device->GetCurrentBackBufferIndex(); // DXGIへの照会は窓の所有側で行う。
	bool bStarted = false; // CPU側の区切りが戻るまで結果を借用する。
	auto Start = [this, BackBuffer, &bStarted]()
	{
		bStarted = Device->BeginFrameForBuffer( BackBuffer ) && GameRenderer->BeginFrame( Device->GetFrameCommands(), Device->GetFrameColorView(), Device->GetImageDescriptorHeap() ) && ( !ImGuiFrameRenderer || ImGuiFrameRenderer->BeginFrame() );
		if ( bStarted && TargetPassRenderer )
		{
			TargetPassRenderer->CollectAfterGPUIdle();
		}
	};
	if ( !ExecuteRender( MoveTemp( Start ) ) || !bStarted || !ThreadedSurface->BeginFrame( Settings.Width, Settings.Height ) )
	{
		LOG( LogDX12, Error, "DX12 frame could not start (device lost or GPU wait failed)" );
		bExit = true;
		return false;
	}
	const int64 Now = NPlatformTime::GetTicks(); // 今フレームの時刻。
	DeltaTime = static_cast<float32>( NPlatformTime::TicksToMs( Now - LastTick ) / 1000.0 );
	DeltaTime = NMath::Max( DeltaTime, 0.000001f );
	LastTick = Now;
	TotalTime += DeltaTime;
	if ( bContext )
	{
		ImGui_ImplWin32_NewFrame();
		ImGuiIO &IO = ImGui::GetIO(); // 論理解像度に固定する。
		IO.DeltaTime = DeltaTime;
		IO.ConfigFlags = Settings.bShowMouseCursor ? IO.ConfigFlags & ~ImGuiConfigFlags_NoMouseCursorChange : IO.ConfigFlags | ImGuiConfigFlags_NoMouseCursorChange;
		ImGui::NewFrame();
	}
	bInFrame = true;
	return true;
}

void CDX12PlatformSubsystem::EndFrame()
{
	GAME_THREAD_ONLY();
	if ( !bInFrame )
	{
		return;
	}
	IFrameCaptureSink *RequestedSink = FrameCaptureSink; // 通知中の解除・差し替え後に古い借用先を呼ばない。
	const uint64 CaptureFrame = FrameCount + 1; // 通常描画と同じフレーム番号。
	const bool bGame = RequestedSink && RequestedSink->WantsFrame( CaptureFrame, EFrameCaptureStage::Game ); // 必要な段階だけGPUコピーする。
	const bool bUI = RequestedSink && RequestedSink->WantsFrame( CaptureFrame, EFrameCaptureStage::WithDebugUI ); // 外部窓を含まない主画面のUIまで。
	// UIを組み立てる間、先に渡したゲーム描画のバッチは専用スレッドで進行できる。
	if ( bContext )
	{
		ImGui::Render();
	}
	ImDrawData *UIData = bContext ? ImGui::GetDrawData() : nullptr; // 待機が戻るまでゲーム側は変更しない。
	const bool bGameCommands = ThreadedSurface->EndFrame(); // ここでゲームの全命令のCPU記録を確定する。
	bool bCapturePrepared = false, bSubmitted = false; // 描画スレッドが返すこのフレームの結果。
	auto Submit = [&, this]()
	{
		bCapturePrepared = ( bGame || bUI ) && Device->PrepareFrameCapture( bGame, bUI );
		if ( bCapturePrepared && bGame )
		{
			Device->RecordGameFrameCopy();
		}
		bool bRendered = GameRenderer->EndFrame() && bGameCommands;
		const uint64 UploadTicket = GameRenderer->TakeUploadTicket(); // 提出成否に応じてアリーナ予約を確定または解放する。
		if ( ImGuiFrameRenderer )
		{
			bRendered = ImGuiFrameRenderer->Render( UIData ) && bRendered;
		}
		if ( bRendered )
		{
			bSubmitted = Device->SubmitFrame( UploadTicket );
		}
		else
		{
			Device->ResolvePendingUploadTicket( UploadTicket, false );
			bSubmitted = false;
		}
	};
	const bool bRecorded = ExecuteRender( MoveTemp( Submit ) );
	// PresentはOSからの通知を待つ場合があるため、窓のスレッドに残す。
	bWindowRenderPhase = true;
	const bool bPresented = bRecorded && bSubmitted && Device->PresentFrame( Settings.bVSync );
	bWindowRenderPhase = false;
	if ( !bPresented )
	{
		LOG( LogDX12, Error, "DX12 present failed" );
		bExit = true;
	}
	if ( bCapturePrepared && bPresented )
	{
		for ( EFrameCaptureStage Stage : { EFrameCaptureStage::Game, EFrameCaptureStage::WithDebugUI } )
		{
			if ( FrameCaptureSink != RequestedSink || !( Stage == EFrameCaptureStage::Game ? bGame : bUI ) )
			{
				continue;
			}
			bool bRead = false; // 通知する画素は同期処理の完了後だけ読む。
			auto Read = [this, Stage, &bRead]()
			{ bRead = Device->ReadFrameCapture( Stage, CapturePixels ); };
			if ( ExecuteRender( MoveTemp( Read ) ) && bRead )
			{
				FFramePixels Frame; // この通知中だけ画素列を借用する。
				Frame.Width = Settings.Width;
				Frame.Height = Settings.Height;
				Frame.Pixels = CapturePixels.GetData();
				Frame.FrameIndex = CaptureFrame;
				Frame.Stage = Stage;
				RequestedSink->OnFrame( Frame );
			}
			else
			{
				LOG( LogDX12, Warning, "DX12 frame capture readback failed" );
			}
		}
	}
	if ( !bExit && bContext )
	{
		// 無効なフレームも完了を通知し、次のNewFrame前の有効化を許す。
		// 画素通知の中で追加された命令も終えてから、窓側へ排他的に貸す。
		RenderCommands->WaitForCommands();
		bWindowRenderPhase = true;
		ImGui::UpdatePlatformWindows();
		Viewports.Render( Settings.ImGuiViewportMaxFPS, Settings.ImGuiViewportMaxPresentsPerFrame );
		bWindowRenderPhase = false;
	}
	bInFrame = false;
	++FrameCount;
	if ( Settings.MaxFPS > 0 )
	{
		const float64 Remaining = 1000.0 / Settings.MaxFPS - NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - LastTick ); // 上限までの待ち時間。
		if ( Remaining >= 1 )
		{
			Sleep( static_cast<DWORD>( Remaining ) );
		}
	}
}

bool CDX12PlatformSubsystem::IsInitialized() const
{
	return bInitialized;
}
void CDX12PlatformSubsystem::RequestExit()
{
	bExit = true;
}
bool CDX12PlatformSubsystem::IsExitRequested() const
{
	return bExit;
}
float32 CDX12PlatformSubsystem::GetDeltaTime() const
{
	return DeltaTime;
}
float64 CDX12PlatformSubsystem::GetTime() const
{
	return TotalTime;
}
uint64 CDX12PlatformSubsystem::GetFrameCount() const
{
	return FrameCount;
}
float32 CDX12PlatformSubsystem::GetFPS() const
{
	return DeltaTime > 0 ? 1.0f / DeltaTime : 0;
}
int32 CDX12PlatformSubsystem::GetScreenWidth() const
{
	return Settings.Width;
}
int32 CDX12PlatformSubsystem::GetScreenHeight() const
{
	return Settings.Height;
}
EWindowMode CDX12PlatformSubsystem::GetWindowMode() const
{
	return Settings.WindowMode;
}
const FString &CDX12PlatformSubsystem::GetWindowTitle() const
{
	return Settings.Title;
}
bool CDX12PlatformSubsystem::IsMouseCursorVisible() const
{
	return Settings.bShowMouseCursor;
}
void *CDX12PlatformSubsystem::GetNativeWindow() const
{
	return Window;
}
IRenderCanvas2D *CDX12PlatformSubsystem::GetCanvas2D()
{
	GAME_THREAD_ONLY( nullptr );
	return bInFrame && ThreadedSurface ? ThreadedSurface->GetCanvas2D() : nullptr;
}

void CDX12PlatformSubsystem::SetFrameCaptureSink( IFrameCaptureSink *Sink )
{
	FrameCaptureSink = Sink;
}

bool CDX12PlatformSubsystem::BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc )
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->BeginOffscreenCapture( Desc );
}

bool CDX12PlatformSubsystem::EndOffscreenCapture( TArray<FColor32> &Out )
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->EndOffscreenCapture( Out );
}

bool CDX12PlatformSubsystem::BeginRenderTargetPass( const FRenderTargetPassDesc &Desc )
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->BeginRenderTargetPass( Desc );
}

bool CDX12PlatformSubsystem::EndRenderTargetPass()
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->EndRenderTargetPass();
}

bool CDX12PlatformSubsystem::BeginGpuTimestamp( FGpuTimestampHandle Query )
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->BeginGpuTimestamp( Query );
}

bool CDX12PlatformSubsystem::EndGpuTimestamp( FGpuTimestampHandle Query )
{
	GAME_THREAD_ONLY( false );
	return bInFrame && ThreadedSurface && ThreadedSurface->EndGpuTimestamp( Query );
}

void CDX12PlatformSubsystem::RecordClear( FColor32 Color )
{
	GAME_THREAD_ONLY();
	if ( bInFrame && ThreadedSurface )
	{
		ThreadedSurface->RecordClear( Color );
	}
}

bool CDX12PlatformSubsystem::WaitForGPU()
{
	GAME_THREAD_ONLY( false );
	bool bResult = false;
	auto Wait = [this, &bResult]()
	{ bResult = !Device || Device->Wait(); };
	return ExecuteRender( MoveTemp( Wait ) ) && bResult;
}

bool CDX12PlatformSubsystem::ExecuteRender( TCallable<void()> Task ) const
{
	if ( bWindowRenderPhase )
	{
		// 窓側へ貸している間の再入は、その所有側で完了させる。専用側には未完了命令がない。
		Task();
		return true;
	}
	if ( RenderCommands && RenderCommands->IsReady() )
	{
		if ( ThreadedSurface && !ThreadedSurface->Flush() )
		{
			return false;
		}
		return RenderCommands->Execute( MoveTemp( Task ) );
	}
	Task();
	return true;
}

void CDX12PlatformSubsystem::GetWheel( float32 &X, float32 &Y ) const
{
	X = WheelX;
	Y = WheelY;
}

void CDX12PlatformSubsystem::AddWheel( float32 X, float32 Y )
{
	PendingWheelX += X;
	PendingWheelY += Y;
}

void CDX12PlatformSubsystem::HandleWindowResize( uint32 SizeType, int32 ClientWidth, int32 ClientHeight )
{
	if ( SizeType == SIZE_MINIMIZED || ClientWidth <= 0 || ClientHeight <= 0 )
	{
		bMinimized = true;
		bPendingResize = false;
		PendingResizeWidth = 0;
		PendingResizeHeight = 0;
		return;
	}
	bMinimized = false;
	if ( !Settings.bResizable )
	{
		return;
	}
	const int32 ClampedWidth = NMath::Clamp( ClientWidth, 1, 16384 ); // GPUの最大画像寸法へ収める。
	const int32 ClampedHeight = NMath::Clamp( ClientHeight, 1, 16384 );
	PendingResizeWidth = ClampedWidth;
	PendingResizeHeight = ClampedHeight;
	bPendingResize = ClampedWidth != Settings.Width || ClampedHeight != Settings.Height;
}

void CDX12PlatformSubsystem::SetWindowTitle( const FString &Title )
{
	Settings.Title = Title;
	if ( Window )
	{
		SetWindowTextW( static_cast<HWND>( Window ), Title.ToWide().GetData() );
	}
}

void CDX12PlatformSubsystem::SetMouseCursorVisible( bool bVisible )
{
	Settings.bShowMouseCursor = bVisible;
	SetCursor( bVisible ? LoadCursorW( nullptr, MAKEINTRESOURCEW( 32512 ) ) : nullptr );
}

void CDX12PlatformSubsystem::SetWindowMode( EWindowMode Mode )
{
	if ( Mode == EWindowMode::Fullscreen )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Exclusive fullscreen", "Use Borderless" );
		return;
	}
	if ( !Window || Settings.WindowMode == Mode )
	{
		return;
	}
	const HWND Handle = static_cast<HWND>( Window ); // 操作する窓。
	if ( Mode == EWindowMode::Borderless )
	{
		RECT Rect = {}; // 復帰用の通常位置。
		GetWindowRect( Handle, &Rect );
		SavedRect[0] = Rect.left;
		SavedRect[1] = Rect.top;
		SavedRect[2] = Rect.right - Rect.left;
		SavedRect[3] = Rect.bottom - Rect.top;
		MONITORINFO Monitor = {}; // 現在のモニター。
		Monitor.cbSize = sizeof( Monitor );
		if ( !GetMonitorInfoW( MonitorFromWindow( Handle, MONITOR_DEFAULTTONEAREST ), &Monitor ) )
		{
			return;
		}
		SetWindowLongPtrW( Handle, GWL_STYLE, WS_POPUP );
		SetWindowPos( Handle, nullptr, Monitor.rcMonitor.left, Monitor.rcMonitor.top, Monitor.rcMonitor.right - Monitor.rcMonitor.left,
					  Monitor.rcMonitor.bottom - Monitor.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOZORDER );
	}
	else
	{
		SetWindowLongPtrW( Handle, GWL_STYLE, Settings.bResizable ? WS_OVERLAPPEDWINDOW : WS_OVERLAPPEDWINDOW & ~( WS_THICKFRAME | WS_MAXIMIZEBOX ) );
		SetWindowPos( Handle, nullptr, SavedRect[0], SavedRect[1], SavedRect[2], SavedRect[3], SWP_FRAMECHANGED | SWP_NOZORDER );
	}
	Settings.WindowMode = Mode;
}

void CDX12PlatformSubsystem::ToggleFullscreen()
{
	SetWindowMode( Settings.WindowMode == EWindowMode::Windowed ? EWindowMode::Borderless : EWindowMode::Windowed );
}

IRenderGeometry3D *CDX12PlatformSubsystem::GetGeometry3D()
{
	GAME_THREAD_ONLY( nullptr );
	return bInFrame && ThreadedSurface ? ThreadedSurface->GetGeometry3D() : nullptr;
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !( bInitialized && Device ) )
	{
		return FTextureResourceHandle();
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	FTextureResourceHandle Result = FTextureResourceHandle();
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateTextureRGBA8( Width, Height, Pixels ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

bool CDX12PlatformSubsystem::GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || !Device )
	{
		return false;
	}
	bool bResult = false; // 出力は同期実行が完了するまで呼び出し元が保持する。
	auto Work = [&]()
	{ bResult = Device->GetTextureResources()->GetComputeTextureCapabilities( Out ); };
	return ExecuteRender( MoveTemp( Work ) ) && bResult;
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FTextureResourceHandle Result; // 入力画素の借用は同期実行から戻るまでに終える。
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateComputeTextureRGBA8( Width, Height, Pixels ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateComputeTextureRGBA16F( int32 Width, int32 Height, const FColor *Pixels )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FTextureResourceHandle Result; // 描画所有側でRGBA16F転送を完了させる。
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateComputeTextureRGBA16F( Width, Height, Pixels ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

bool CDX12PlatformSubsystem::GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || !Device )
	{
		return false;
	}
	bool bResult = false; // 出力は同期実行が完了するまで呼び出し元が保持する。
	auto Work = [&]()
	{ bResult = Device->GetTextureResources()->GetRenderTargetCapabilities( Out ); };
	return ExecuteRender( MoveTemp( Work ) ) && bResult;
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FTextureResourceHandle Result; // 描画所有側で初期消去とSRV登録を完了させる。
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateRenderTargetRGBA8( Width, Height, ClearColor ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateRenderTargetRGBA16F( int32 Width, int32 Height, FColor ClearColor )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FTextureResourceHandle Result; // 描画所有側でRGBA16F描画先の初期消去とSRV登録を完了させる。
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateRenderTargetRGBA16F( Width, Height, ClearColor ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth )
{
	GAME_THREAD_ONLY( FTextureResourceHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FTextureResourceHandle Result; // 描画所有側で初期深度消去と登録を完了させる。
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->CreateDepthTextureD32( Width, Height, ClearDepth ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FTextureResourceHandle();
}

bool CDX12PlatformSubsystem::CopyTexture( FTextureResourceHandle Source, FTextureResourceHandle Destination )
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || !Device )
	{
		return false;
	}
	bool bResult = false; // 専用描画スレッド上で同じDIRECTキューのコピーを完了させる。
	auto Work = [&]()
	{ bResult = Device->GetTextureResources()->CopyTexture( Source, Destination ); };
	return ExecuteRender( MoveTemp( Work ) ) && bResult;
}

bool CDX12PlatformSubsystem::ReadTextureRGBA8( FTextureResourceHandle Texture, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || !Device )
	{
		return false;
	}
	bool bResult = false; // 専用描画スレッド上でREADBACK同期読み戻しを完了させる。
	auto Work = [&]()
	{ bResult = Device->GetTextureResources()->ReadTextureRGBA8( Texture, OutPixels, OutWidth, OutHeight ); };
	return ExecuteRender( MoveTemp( Work ) ) && bResult;
}

bool CDX12PlatformSubsystem::ReadTextureRGBA16F( FTextureResourceHandle Texture, TArray<FColor> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	GAME_THREAD_ONLY( false );
	if ( !bInitialized || !Device )
	{
		return false;
	}
	bool bResult = false; // 専用描画スレッド上でRGBA16F READBACK同期読み戻しを完了させる。
	auto Work = [&]()
	{ bResult = Device->GetTextureResources()->ReadTextureRGBA16F( Texture, OutPixels, OutWidth, OutHeight ); };
	return ExecuteRender( MoveTemp( Work ) ) && bResult;
}

bool CDX12PlatformSubsystem::UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->UpdateTextureRGBA8( Texture, Width, Height, Pixels ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

void CDX12PlatformSubsystem::ReleaseTexture( FTextureResourceHandle Texture )
{
	GAME_THREAD_ONLY();
	if ( !( Device ) )
	{
		return;
	}
	auto Work = [&]()
	{ if ( auto* Resources = Device->GetTextureResources() ) { Resources->ReleaseTexture( Texture ); } };
	ExecuteRender( MoveTemp( Work ) );
}

bool CDX12PlatformSubsystem::GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetTextureResources()->GetTextureResourceView( Texture, Out ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

FBufferResourceHandle CDX12PlatformSubsystem::CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access )
{
	GAME_THREAD_ONLY( FBufferResourceHandle() );
	if ( !( bInitialized && Device ) )
	{
		return FBufferResourceHandle();
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	FBufferResourceHandle Result = FBufferResourceHandle();
	auto Work = [&]()
	{ Result = Device->GetBufferResources()->CreateBuffer( Bytes, Access ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FBufferResourceHandle();
}

void CDX12PlatformSubsystem::ReleaseBuffer( FBufferResourceHandle Buffer )
{
	GAME_THREAD_ONLY();
	if ( !( Device ) )
	{
		return;
	}
	auto Work = [&]()
	{ if ( auto* Resources = Device->GetBufferResources() ) { Resources->ReleaseBuffer( Buffer ); } };
	ExecuteRender( MoveTemp( Work ) );
}

bool CDX12PlatformSubsystem::GetBufferSize( FBufferResourceHandle Buffer, uint64 &OutSize ) const
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetBufferResources()->GetBufferSize( Buffer, OutSize ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

bool CDX12PlatformSubsystem::ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8> &Out )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetBufferResources()->ReadBuffer( Buffer, Offset, NumBytes, Out ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

FComputePipelineHandle CDX12PlatformSubsystem::CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots )
{
	GAME_THREAD_ONLY( FComputePipelineHandle() );
	if ( !( bInitialized && Device ) )
	{
		return FComputePipelineHandle();
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	FComputePipelineHandle Result = FComputePipelineHandle();
	auto Work = [&]()
	{ Result = Device->GetComputePipelineResources()->CreateComputePipeline( Bytecode, Slots ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FComputePipelineHandle();
}

FComputePipelineHandle CDX12PlatformSubsystem::CreateComputePipelineWithTextures( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, TArrayView<const FComputeTextureSlot> TextureSlots )
{
	GAME_THREAD_ONLY( FComputePipelineHandle() );
	if ( !bInitialized || !Device )
	{
		return {};
	}
	FComputePipelineHandle Result; // 宣言とバイナリの借用は同期実行から戻るまでに終える。
	auto Work = [&]()
	{ Result = Device->GetComputePipelineResources()->CreateComputePipelineWithTextures( Bytecode, Slots, TextureSlots ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FComputePipelineHandle();
}

void CDX12PlatformSubsystem::ReleaseComputePipeline( FComputePipelineHandle Pipeline )
{
	GAME_THREAD_ONLY();
	if ( !( Device ) )
	{
		return;
	}
	auto Work = [&]()
	{ if ( auto* Resources = Device->GetComputePipelineResources() ) { Resources->ReleaseComputePipeline( Pipeline ); } };
	ExecuteRender( MoveTemp( Work ) );
}

FGraphicsPipelineHandle CDX12PlatformSubsystem::CreateGraphicsPipeline( const FGraphicsPipelineDesc &Desc )
{
	GAME_THREAD_ONLY( FGraphicsPipelineHandle() );
	if ( !( bInitialized && Device ) )
	{
		return FGraphicsPipelineHandle();
	}
	// 宣言配列とシェーダーバイナリの借用は同期実行から戻るまでに終える。
	FGraphicsPipelineHandle Result = FGraphicsPipelineHandle();
	auto Work = [&]()
	{ Result = Device->GetGraphicsPipelineResources()->CreateGraphicsPipeline( Desc ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FGraphicsPipelineHandle();
}

void CDX12PlatformSubsystem::ReleaseGraphicsPipeline( FGraphicsPipelineHandle Pipeline )
{
	GAME_THREAD_ONLY();
	if ( !( Device ) )
	{
		return;
	}
	auto Work = [&]()
	{ if ( auto* Resources = Device->GetGraphicsPipelineResources() ) { Resources->ReleaseGraphicsPipeline( Pipeline ); } };
	ExecuteRender( MoveTemp( Work ) );
}

bool CDX12PlatformSubsystem::GetGpuTimestampCapabilities( FGpuTimestampCapabilities &Out ) const
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	bool Result = false;
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Result = Resources->GetGpuTimestampCapabilities( Out );
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

FGpuTimestampHandle CDX12PlatformSubsystem::CreateGpuTimestampQuery()
{
	GAME_THREAD_ONLY( FGpuTimestampHandle() );
	if ( !( bInitialized && Device ) )
	{
		return FGpuTimestampHandle();
	}
	FGpuTimestampHandle Result;
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Result = Resources->CreateGpuTimestampQuery();
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FGpuTimestampHandle();
}

void CDX12PlatformSubsystem::ReleaseGpuTimestampQuery( FGpuTimestampHandle Query )
{
	GAME_THREAD_ONLY();
	if ( !( Device ) )
	{
		return;
	}
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Resources->ReleaseGpuTimestampQuery( Query );
		}
	};
	ExecuteRender( MoveTemp( Work ) );
}

bool CDX12PlatformSubsystem::IsGpuTimestampReady( FGpuTimestampHandle Query, bool &OutReady )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	bool Result = false;
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Result = Resources->IsGpuTimestampReady( Query, OutReady );
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

bool CDX12PlatformSubsystem::WaitGpuTimestamp( FGpuTimestampHandle Query, uint32 TimeoutMs )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	bool Result = false;
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Result = Resources->WaitGpuTimestamp( Query, TimeoutMs );
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

bool CDX12PlatformSubsystem::GetGpuTimestampResult( FGpuTimestampHandle Query, FGpuTimestampResult &Out )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	bool Result = false;
	auto Work = [&]()
	{
		if ( auto *Resources = Device->GetTimestampResources() )
		{
			Result = Resources->GetGpuTimestampResult( Query, Out );
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

FComputeSubmission CDX12PlatformSubsystem::SubmitComputeInternal( TArrayView<const FComputeDispatch> Dispatches, const FGpuTimestampHandle *Timestamp )
{
	GAME_THREAD_ONLY( FComputeSubmission() );
	if ( !( bInitialized && Device ) )
	{
		return FComputeSubmission();
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	FComputeSubmission Result = FComputeSubmission();
	auto Work = [&]()
	{
		if ( NativeSurface && NativeSurface->SubmitPendingMainCommands() )
		{
			IComputeQueue *Queue = Device->GetComputeQueue();
			Result = Timestamp ? Queue->SubmitComputeWithTimestamp( Dispatches, *Timestamp ) : Queue->SubmitCompute( Dispatches );
		}
	};
	return ExecuteRender( MoveTemp( Work ) ) ? Result : FComputeSubmission();
}

FComputeSubmission CDX12PlatformSubsystem::SubmitCompute( TArrayView<const FComputeDispatch> Dispatches )
{
	return SubmitComputeInternal( Dispatches, nullptr );
}

FComputeSubmission CDX12PlatformSubsystem::SubmitComputeWithTimestamp( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp )
{
	return SubmitComputeInternal( Dispatches, &Timestamp );
}

bool CDX12PlatformSubsystem::GetComputeCompletion( FComputeSubmission Submission, bool &OutCompleted )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetComputeQueue()->GetComputeCompletion( Submission, OutCompleted ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

bool CDX12PlatformSubsystem::WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs )
{
	GAME_THREAD_ONLY( false );
	if ( !( bInitialized && Device ) )
	{
		return false;
	}
	// 呼出し元は同期処理の完了まで、入力と出力の寿命を保持する。
	bool Result = false;
	auto Work = [&]()
	{ Result = Device->GetComputeQueue()->WaitCompute( Submission, TimeoutMs ); };
	return ExecuteRender( MoveTemp( Work ) ) ? Result : false;
}

FTaskFence CDX12PlatformSubsystem::EnqueueRenderCommand( TCallable<void( const FRenderCommandContext & )> Task, const char *Name )
{
	GAME_THREAD_ONLY( FTaskFence() );
	if ( !bInitialized || bWindowRenderPhase || !RenderCommands || !ThreadedSurface || !NativeSurface || !Task || !ThreadedSurface->Flush() )
	{
		return {};
	}
	// 公開のゲーム側サービスを使い回さず、実行中だけ有効な描画窓口を渡す。
	auto Work = [Command = MoveTemp( Task ), Target = NativeSurface]()
	{
		const FRenderCommandContext Context{ Target->GetCanvas2D(), Target->GetGeometry3D() };
		Command( Context );
	};
	return RenderCommands->Enqueue( MoveTemp( Work ), Name ? Name : "Render.UserCommand" );
}
