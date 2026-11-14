// SPDX-License-Identifier: MIT
#include "BackendSelection.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Platform/System/CommandLine.h"

EPlatformBackend NBackendSelection::GetRequested()
{
	return GetRequested( NCommandLine::Get() );
}

EPlatformBackend NBackendSelection::GetRequested( const FString& CommandLine )
{
	const TOptional<FString> Value = NCommandLine::GetValue( CommandLine, "backend" ); // 選択値。
	if ( Value.IsSet() )
	{
		if ( Value.GetValue().Equals( "null", ESearchCase::IgnoreCase ) )
		{
			return EPlatformBackend::Null;
		}
		if ( Value.GetValue().Equals( "dx12", ESearchCase::IgnoreCase ) )
		{
			return EPlatformBackend::DX12;
		}
	}
	return EPlatformBackend::DxLib;
}

bool NBackendSelection::IsNullRequested()
{
	return GetRequested() == EPlatformBackend::Null;
}

bool NBackendSelection::IsNullRequested( const FString& CommandLine )
{
	return GetRequested( CommandLine ) == EPlatformBackend::Null;
}
