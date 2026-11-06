// SPDX-License-Identifier: MIT
#include "BackendSupport.h"

#include "Library/Core/Collection/Set.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogBackendSupport, Log )

namespace
{
	// すでに出した (バックエンド名 + 機能) の組
	TSet<FString>& GetReported()
	{
		static TSet<FString> Reported;
		return Reported;
	}
}

bool NBackendSupport::ReportUnsupported( const char* BackendName, const char* Feature, const char* Reason )
{
	const FString Name = BackendName != nullptr ? BackendName : "(unknown)";
	const FString FeatureName = Feature != nullptr ? Feature : "(unknown)";
	const FString Key = Name + "|" + FeatureName;
	if ( GetReported().Contains( Key ) )
	{
		return false;
	}
	GetReported().Add( Key );
	const FString Detail = Reason != nullptr ? FString( " (" ) + Reason + ")" : FString();
	LOG( LogBackendSupport, Warning, "The %s backend does not support: %s%s", *Name, *FeatureName, *Detail );
	return true;
}

int32 NBackendSupport::GetNumReported()
{
	return GetReported().Num();
}

void NBackendSupport::ResetReported()
{
	GetReported().Reset();
}
