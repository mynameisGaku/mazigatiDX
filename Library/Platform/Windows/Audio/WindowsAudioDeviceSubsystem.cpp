// SPDX-License-Identifier: MIT
#include "WindowsAudioDeviceSubsystem.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

#include <xaudio2.h>
#pragma comment( lib, "xaudio2.lib" )

REGISTER_SUBSYSTEM( CWindowsAudioDeviceSubsystem )

DEFINE_LOG_CATEGORY_STATIC( LogWindowsAudio, Log )

bool CWindowsAudioDeviceSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsAudioDeviceSubsystem::Initialize( FSubsystemCollection& )
{
	Start();
}

void CWindowsAudioDeviceSubsystem::Start()
{
	bComInitialized = SUCCEEDED( CoInitializeEx( nullptr, COINIT_MULTITHREADED ) );
	IXAudio2* Created = nullptr;
	if ( FAILED( XAudio2Create( &Created, 0, XAUDIO2_DEFAULT_PROCESSOR ) ) || Created == nullptr )
	{
		LOG( LogWindowsAudio, Warning, "XAudio2 could not start. Sounds will not be played." );
		return;
	}
	IXAudio2MasteringVoice* CreatedMaster = nullptr;
	if ( FAILED( Created->CreateMasteringVoice( &CreatedMaster ) ) || CreatedMaster == nullptr )
	{
		LOG( LogWindowsAudio, Warning, "No audio output device is available. Sounds will not be played." );
		Created->Release();
		return;
	}
	XAUDIO2_VOICE_DETAILS Details = {};
	CreatedMaster->GetVoiceDetails( &Details );
	Engine = Created;
	Master = CreatedMaster;
	OutputChannels = static_cast<int32>( Details.InputChannels );
	LOG( LogWindowsAudio, Log, "XAudio2 ready (%d output channel(s), %u Hz)", OutputChannels, Details.InputSampleRate );
}

void CWindowsAudioDeviceSubsystem::Deinitialize()
{
	if ( Master != nullptr )
	{
		Master->DestroyVoice();
		Master = nullptr;
	}
	if ( Engine != nullptr )
	{
		Engine->Release();
		Engine = nullptr;
	}
	OutputChannels = 0;
	if ( bComInitialized )
	{
		CoUninitialize();
		bComInitialized = false;
	}
}

bool CWindowsAudioDeviceSubsystem::IsAvailable() const
{
	return Engine != nullptr && Master != nullptr;
}

IXAudio2* CWindowsAudioDeviceSubsystem::GetEngine() const
{
	return Engine;
}

int32 CWindowsAudioDeviceSubsystem::GetOutputChannels() const
{
	return OutputChannels;
}
