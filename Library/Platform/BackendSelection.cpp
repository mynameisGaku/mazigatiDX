// SPDX-License-Identifier: MIT
#include "BackendSelection.h"

#include "Library/Core/Collection/Optional.h"
#include "Library/Platform/System/CommandLine.h"

bool NBackendSelection::IsNullRequested()
{
	return IsNullRequested( NCommandLine::Get() );
}

bool NBackendSelection::IsNullRequested( const FString& CommandLine )
{
	const TOptional<FString> Value = NCommandLine::GetValue( CommandLine, "backend" );
	return Value.IsSet() && Value.GetValue().Equals( "null", ESearchCase::IgnoreCase );
}
