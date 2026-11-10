// SPDX-License-Identifier: MIT
#include "DX12PlatformSubsystem.h"
#include "Library/Platform/DX12/ImGui/DX12ImGuiFrameRenderer.h"
#include "Library/Platform/DX12/Render/DX12GameRenderer.h"
#include "Library/Platform/DX12/Render/DX12GameCapture.h"
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

void CDX12PlatformSubsystem::Initialize( FSubsystemCollection & )
{
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
	Device = new FDX12Device();
	const auto Warp = NCommandLine::GetValue( "dx12-warp" ); // UIの有無とは独立したGPU選択。
	if ( !Device->Initialize( static_cast<HWND>( Window ), Settings.Width, Settings.Height, Warp.IsSet() && Warp.GetValue().Equals( "true", ESearchCase::IgnoreCase ) ) || !GameText.Initialize() )
	{
		LOG( LogDX12, Error, "DX12 game resources could not initialize" );
		Deinitialize();
		return;
	}
	GameRenderer = new FDX12GameRenderer();
	if ( !GameRenderer->Initialize( Device->GetNativeDevice(), Settings.Width, Settings.Height, *Device->GetTextureResources(), GameText ) )
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
		ImGuiFrameRenderer = new FDX12ImGuiFrameRenderer();
		if ( !bWin32 || !ImGuiFrameRenderer->Initialize( *Device ) )
		{
			Deinitialize();
			return;
		}
	}
	const EWindowMode InitialMode = Settings.WindowMode; // 窓の作成後に起動モードへ移す。
	CaptureRenderer = new FDX12GameCapture( *Device, GameText );
	Settings.WindowMode = EWindowMode::Windowed;
	SetWindowMode( InitialMode );
	ShowWindow( static_cast<HWND>( Window ), SW_SHOW );
	LastTick = NPlatformTime::GetTicks();
	bInitialized = true;
	LOG( LogDX12, Log, "DX12 initialized (%dx%d). Static FBX/OBJ models supported; lighting, animated models, audio and general render target assets are not supported yet.", Settings.Width, Settings.Height );
}

void CDX12PlatformSubsystem::Deinitialize()
{
	FrameCaptureSink = nullptr;
	if ( bInFrame && bContext )
	{
		ImGui::EndFrame();
	}
	bInFrame = false;
	// 撮影側がGPU完了を待ち、借用したUI接続と画像heapより先に記録を戻す。
	delete CaptureRenderer;
	CaptureRenderer = nullptr;
	// GPU所有者が新規提出を止めてから、UIの資源と外部窓を外す。
	while ( Device && !Device->PrepareShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
	delete GameRenderer;
	GameRenderer = nullptr;
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
	WheelX = PendingWheelX;
	WheelY = PendingWheelY;
	PendingWheelX = PendingWheelY = 0;
	if ( !Device->BeginFrame() || !GameRenderer->BeginFrame( Device->GetFrameCommands(), Device->GetFrameColorView(), Device->GetImageDescriptorHeap() ) || ( ImGuiFrameRenderer && !ImGuiFrameRenderer->BeginFrame() ) )
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
	if ( !bInFrame )
	{
		return;
	}
	IFrameCaptureSink *RequestedSink = FrameCaptureSink; // 通知中の解除・差し替え後に古い借用先を呼ばない。
	const uint64 CaptureFrame = FrameCount + 1; // 通常描画と同じフレーム番号。
	const bool bGame = RequestedSink && RequestedSink->WantsFrame( CaptureFrame, EFrameCaptureStage::Game ); // 必要な段階だけGPUコピーする。
	const bool bUI = RequestedSink && RequestedSink->WantsFrame( CaptureFrame, EFrameCaptureStage::WithDebugUI ); // 外部窓を含まない主画面のUIまで。
	const bool bCapturePrepared = ( bGame || bUI ) && Device->PrepareFrameCapture( bGame, bUI ); // GPU完了前に古い読み取り領域を再利用しない。
	if ( bCapturePrepared && bGame )
	{
		Device->RecordGameFrameCopy();
	}
	bool bRendered = GameRenderer->EndFrame(); // UIがなくてもゲーム描画だけで提出できる。
	if ( ImGuiFrameRenderer )
	{
		ImGui::Render();
		bRendered = ImGuiFrameRenderer->Render( ImGui::GetDrawData() ) && bRendered;
	}
	const bool bPresented = bRendered && Device->EndFrame( Settings.bVSync ); // 撮影に失敗しても通常の表示とは区別する。
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
			if ( Device->ReadFrameCapture( Stage, CapturePixels ) )
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
		ImGui::UpdatePlatformWindows();
		Viewports.Render( Settings.ImGuiViewportMaxFPS, Settings.ImGuiViewportMaxPresentsPerFrame );
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
	if ( bInFrame && CaptureRenderer && CaptureRenderer->IsActive() )
	{
		return CaptureRenderer->GetCanvas2D();
	}
	return bInFrame && GameRenderer ? GameRenderer->GetCanvas2D() : nullptr;
}

void CDX12PlatformSubsystem::SetFrameCaptureSink( IFrameCaptureSink *Sink )
{
	FrameCaptureSink = Sink;
}

bool CDX12PlatformSubsystem::BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc )
{
	return bInFrame && CaptureRenderer && CaptureRenderer->Begin( Desc );
}

bool CDX12PlatformSubsystem::EndOffscreenCapture( TArray<FColor32> &Out )
{
	return bInFrame && CaptureRenderer && CaptureRenderer->End( Out );
}

void CDX12PlatformSubsystem::RecordClear( FColor32 Color )
{
	if ( bInFrame && CaptureRenderer && CaptureRenderer->IsActive() )
	{
		CaptureRenderer->RecordClear( Color );
		return;
	}
	if ( bInFrame )
	{
		GameRenderer->RecordClear( Color );
	}
}

bool CDX12PlatformSubsystem::WaitForGPU()
{
	return !Device || Device->Wait();
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
	if ( bInFrame && CaptureRenderer && CaptureRenderer->IsActive() )
	{
		return CaptureRenderer->GetGeometry3D();
	}
	return bInFrame && GameRenderer ? GameRenderer->GetGeometry3D() : nullptr;
}

FTextureResourceHandle CDX12PlatformSubsystem::CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return bInitialized && Device ? Device->GetTextureResources()->CreateTextureRGBA8( Width, Height, Pixels ) : FTextureResourceHandle();
}

bool CDX12PlatformSubsystem::UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels )
{
	return bInitialized && Device && Device->GetTextureResources()->UpdateTextureRGBA8( Texture, Width, Height, Pixels );
}

void CDX12PlatformSubsystem::ReleaseTexture( FTextureResourceHandle Texture )
{
	if ( Device )
	{
		Device->GetTextureResources()->ReleaseTexture( Texture );
	}
}

bool CDX12PlatformSubsystem::GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const
{
	return bInitialized && Device && Device->GetTextureResources()->GetTextureResourceView( Texture, Out );
}

FBufferResourceHandle CDX12PlatformSubsystem::CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access )
{
	return bInitialized && Device ? Device->GetBufferResources()->CreateBuffer( Bytes, Access ) : FBufferResourceHandle();
}

void CDX12PlatformSubsystem::ReleaseBuffer( FBufferResourceHandle Buffer )
{
	if ( Device )
	{
		Device->GetBufferResources()->ReleaseBuffer( Buffer );
	}
}

bool CDX12PlatformSubsystem::GetBufferSize( FBufferResourceHandle Buffer, uint64 &OutSize ) const
{
	return bInitialized && Device && Device->GetBufferResources()->GetBufferSize( Buffer, OutSize );
}

bool CDX12PlatformSubsystem::ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8> &Out )
{
	return bInitialized && Device && Device->GetBufferResources()->ReadBuffer( Buffer, Offset, NumBytes, Out );
}

FComputePipelineHandle CDX12PlatformSubsystem::CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots )
{
	return bInitialized && Device ? Device->GetComputePipelineResources()->CreateComputePipeline( Bytecode, Slots ) : FComputePipelineHandle();
}

void CDX12PlatformSubsystem::ReleaseComputePipeline( FComputePipelineHandle Pipeline )
{
	if ( Device )
	{
		Device->GetComputePipelineResources()->ReleaseComputePipeline( Pipeline );
	}
}

FComputeSubmission CDX12PlatformSubsystem::SubmitCompute( TArrayView<const FComputeDispatch> Dispatches )
{
	return bInitialized && Device ? Device->GetComputeQueue()->SubmitCompute( Dispatches ) : FComputeSubmission();
}

bool CDX12PlatformSubsystem::GetComputeCompletion( FComputeSubmission Submission, bool &OutCompleted )
{
	return bInitialized && Device && Device->GetComputeQueue()->GetComputeCompletion( Submission, OutCompleted );
}

bool CDX12PlatformSubsystem::WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs )
{
	return bInitialized && Device && Device->GetComputeQueue()->WaitCompute( Submission, TimeoutMs );
}
