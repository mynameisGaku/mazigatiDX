// SPDX-License-Identifier: MIT
#include "NullPlatform.h"

#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/CommandLine.h"
#include "VendorModules/ImGui/imgui.h"

DEFINE_LOG_CATEGORY_STATIC( LogNullPlatform, Log )

REGISTER_SUBSYSTEM( CNullPlatform )

#pragma region Lifecycle

bool CNullPlatform::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

void CNullPlatform::Initialize( FSubsystemCollection& Collection )
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

void CNullPlatform::Deinitialize()
{
	if ( !bInitialized )
	{
		return;
	}
	LOG( LogNullPlatform, Log, "Shutting down (%llu frame(s) run)", static_cast<unsigned long long>( FrameCount ) );
	ShutdownImGui();
	bInitialized = false;
}

void CNullPlatform::InitImGui()
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

void CNullPlatform::ShutdownImGui()
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

void CNullPlatform::AnswerImGuiTextureRequests()
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

bool CNullPlatform::BeginFrame()
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

void CNullPlatform::EndFrame()
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

bool CNullPlatform::IsInitialized() const
{
	return bInitialized;
}

void CNullPlatform::RequestExit()
{
	if ( !bExitRequested )
	{
		LOG( LogNullPlatform, Log, "Exit requested" );
	}
	bExitRequested = true;
}

bool CNullPlatform::IsExitRequested() const
{
	return bExitRequested;
}

float32 CNullPlatform::GetDeltaTime() const
{
	return DeltaTime;
}

float64 CNullPlatform::GetTime() const
{
	return TotalTime;
}

uint64 CNullPlatform::GetFrameCount() const
{
	return FrameCount;
}

float32 CNullPlatform::GetFPS() const
{
	return 1.0f / FixedDeltaTime;
}

int32 CNullPlatform::GetScreenWidth() const
{
	return Settings.Width;
}

int32 CNullPlatform::GetScreenHeight() const
{
	return Settings.Height;
}

EWindowMode CNullPlatform::GetWindowMode() const
{
	return Settings.WindowMode;
}

void CNullPlatform::SetWindowMode( EWindowMode Mode )
{
	Settings.WindowMode = Mode;
	if ( Mode != EWindowMode::Windowed )
	{
		LastFullscreenMode = Mode;
	}
}

void CNullPlatform::ToggleFullscreen()
{
	SetWindowMode( Settings.WindowMode == EWindowMode::Windowed ? LastFullscreenMode : EWindowMode::Windowed );
}

const FString& CNullPlatform::GetWindowTitle() const
{
	return Settings.Title;
}

void CNullPlatform::SetWindowTitle( const FString& Title )
{
	Settings.Title = Title;
}

bool CNullPlatform::IsMouseCursorVisible() const
{
	return Settings.bShowMouseCursor;
}

void CNullPlatform::SetMouseCursorVisible( bool bVisible )
{
	Settings.bShowMouseCursor = bVisible;
}

int32 CNullPlatform::GetMaxFrames() const
{
	return MaxFrames;
}

#pragma endregion
