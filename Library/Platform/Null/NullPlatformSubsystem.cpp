// SPDX-License-Identifier: MIT
#include "NullPlatformSubsystem.h"

#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/CommandLine.h"
#include "VendorModules/ImGui/imgui.h"

DEFINE_LOG_CATEGORY_STATIC( LogNullPlatform, Log )

REGISTER_SUBSYSTEM( CNullPlatformSubsystem )

#pragma region Lifecycle

bool CNullPlatformSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

void CNullPlatformSubsystem::Initialize( FSubsystemCollection& Collection )
{
	if ( CEngine* Engine = GetEngine() )
	{
		Settings = Engine->GetStartupSettings();
		if ( !Settings.bEnableImGui )
		{
			Settings.bEnableImGui = false;
		}
	}
	else
	{
		Settings.bEnableImGui = false;
	}
	bExitRequested = false;
	bInFrame = false;
	DeltaTime = 0.0f;
	TotalTime = 0.0;
	FrameCount = 0;
	if ( Settings.WindowMode != EWindowMode::Windowed )
	{
		LastFullscreenMode = Settings.WindowMode;
	}

	MaxFrames = 0;
	const TOptional<FString> MaxFramesValue = NCommandLine::GetValue( "max-frames" );
	if ( MaxFramesValue.IsSet() )
	{
		MaxFrames = MaxFramesValue.GetValue().ToInt();
		if ( MaxFrames < 0 )
		{
			MaxFrames = 0;
		}
	}

	if ( Settings.bEnableImGui )
	{
		InitImGui();
	}
	bInitialized = true;

	LOG( LogNullPlatform, Log, "Initialized (no window, fixed %.4f s per frame) %dx%d, max frames %d, ImGui %s", FixedDeltaTime, Settings.Width, Settings.Height, MaxFrames,
		 bImGuiActive ? "on (no renderer)" : "off" );
}

void CNullPlatformSubsystem::Deinitialize()
{
	if ( !bInitialized )
	{
		return;
	}
	LOG( LogNullPlatform, Log, "Shutting down (%llu frame(s) run)", static_cast<unsigned long long>( FrameCount ) );
	ShutdownImGui();
	bInitialized = false;
}

void CNullPlatformSubsystem::InitImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	if ( Settings.bImGuiDocking )
	{
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}
	// 窓を持たないので、窓の外へ出す機能は使えない。利用者の imgui.ini は、読まず、書かない
	IO.IniFilename = nullptr;
	// 文字の画像は、AnswerImGuiTextureRequests が「できた」と答える
	IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	IO.DisplaySize = ImVec2( static_cast<float32>( Settings.Width ), static_cast<float32>( Settings.Height ) );
	IO.DeltaTime = FixedDeltaTime;
	bImGuiActive = true;
}

void CNullPlatformSubsystem::ShutdownImGui()
{
	if ( !bImGuiActive )
	{
		return;
	}
	for ( ImTextureData* Texture : ImGui::GetPlatformIO().Textures )
	{
		if ( Texture->Status != ImTextureStatus_Destroyed )
		{
			Texture->SetTexID( ImTextureID_Invalid );
			Texture->SetStatus( ImTextureStatus_Destroyed );
		}
	}
	ImGui::DestroyContext();
	bImGuiActive = false;
}

void CNullPlatformSubsystem::AnswerImGuiTextureRequests()
{
	for ( ImTextureData* Texture : ImGui::GetPlatformIO().Textures )
	{
		if ( Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates )
		{
			// 画像の中身は持たない。0 でない番号を渡して、「作れた」ことにする
			Texture->SetTexID( static_cast<ImTextureID>( 1 ) );
			Texture->SetStatus( ImTextureStatus_OK );
		}
		else if ( Texture->Status == ImTextureStatus_WantDestroy && Texture->UnusedFrames > 0 )
		{
			Texture->SetTexID( ImTextureID_Invalid );
			Texture->SetStatus( ImTextureStatus_Destroyed );
		}
	}
}

#pragma endregion

#pragma region Frame

bool CNullPlatformSubsystem::BeginFrame()
{
	if ( !bInitialized || bExitRequested )
	{
		return false;
	}
	if ( MaxFrames > 0 && FrameCount >= static_cast<uint64>( MaxFrames ) )
	{
		LOG( LogNullPlatform, Log, "Reached the frame limit (%d)", MaxFrames );
		return false;
	}

	DeltaTime = FixedDeltaTime;
	TotalTime += static_cast<float64>( FixedDeltaTime );

	if ( bImGuiActive )
	{
		PROFILE_SCOPE_OWNER( "Platform", "Null", "ImGui.NewFrame" );
		ImGuiIO& IO = ImGui::GetIO();
		IO.DisplaySize = ImVec2( static_cast<float32>( Settings.Width ), static_cast<float32>( Settings.Height ) );
		IO.DeltaTime = FixedDeltaTime;
		ImGui::NewFrame();
	}
	bInFrame = true;
	return true;
}

void CNullPlatformSubsystem::EndFrame()
{
	if ( !bInFrame )
	{
		return;
	}
	if ( bImGuiActive )
	{
		PROFILE_SCOPE_OWNER( "Platform", "Null", "ImGui.Render" );
		ImGui::Render();
		AnswerImGuiTextureRequests();
	}
	++FrameCount;
	bInFrame = false;
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

bool CNullPlatformSubsystem::IsInitialized() const
{
	return bInitialized;
}

void CNullPlatformSubsystem::RequestExit()
{
	if ( !bExitRequested )
	{
		LOG( LogNullPlatform, Log, "Exit requested" );
	}
	bExitRequested = true;
}

bool CNullPlatformSubsystem::IsExitRequested() const
{
	return bExitRequested;
}

float32 CNullPlatformSubsystem::GetDeltaTime() const
{
	return DeltaTime;
}

float64 CNullPlatformSubsystem::GetTime() const
{
	return TotalTime;
}

uint64 CNullPlatformSubsystem::GetFrameCount() const
{
	return FrameCount;
}

float32 CNullPlatformSubsystem::GetFPS() const
{
	return 1.0f / FixedDeltaTime;
}

int32 CNullPlatformSubsystem::GetScreenWidth() const
{
	return Settings.Width;
}

int32 CNullPlatformSubsystem::GetScreenHeight() const
{
	return Settings.Height;
}

EWindowMode CNullPlatformSubsystem::GetWindowMode() const
{
	return Settings.WindowMode;
}

void CNullPlatformSubsystem::SetWindowMode( EWindowMode Mode )
{
	Settings.WindowMode = Mode;
	if ( Mode != EWindowMode::Windowed )
	{
		LastFullscreenMode = Mode;
	}
}

void CNullPlatformSubsystem::ToggleFullscreen()
{
	SetWindowMode( Settings.WindowMode == EWindowMode::Windowed ? LastFullscreenMode : EWindowMode::Windowed );
}

const FString& CNullPlatformSubsystem::GetWindowTitle() const
{
	return Settings.Title;
}

void CNullPlatformSubsystem::SetWindowTitle( const FString& Title )
{
	Settings.Title = Title;
}

bool CNullPlatformSubsystem::IsMouseCursorVisible() const
{
	return Settings.bShowMouseCursor;
}

void CNullPlatformSubsystem::SetMouseCursorVisible( bool bVisible )
{
	Settings.bShowMouseCursor = bVisible;
}

int32 CNullPlatformSubsystem::GetMaxFrames() const
{
	return MaxFrames;
}

#pragma endregion
