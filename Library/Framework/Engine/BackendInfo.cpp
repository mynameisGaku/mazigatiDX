// SPDX-License-Identifier: MIT
#include "BackendInfo.h"

EPlatformBackend NBackendInfo::Get()
{
	// 起動の引数から決まり、実行中は変わらない。最初の呼び出しで決めて持つ
	static const EPlatformBackend Backend = NBackendSelection::GetRequested();
	return Backend;
}

FString NBackendInfo::GetName()
{
	switch ( Get() )
	{
	case EPlatformBackend::Null:
		return FString( "Null" );
	case EPlatformBackend::DX12:
		return FString( "DX12" );
	case EPlatformBackend::DxLib:
	default:
		return FString( "DxLib" );
	}
}

bool NBackendInfo::TryParse( const FString& Name, EPlatformBackend& Out )
{
	if ( Name.Equals( "dxlib", ESearchCase::IgnoreCase ) )
	{
		Out = EPlatformBackend::DxLib;
		return true;
	}
	if ( Name.Equals( "null", ESearchCase::IgnoreCase ) )
	{
		Out = EPlatformBackend::Null;
		return true;
	}
	if ( Name.Equals( "dx12", ESearchCase::IgnoreCase ) )
	{
		Out = EPlatformBackend::DX12;
		return true;
	}
	return false;
}
