// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/ScreenshotNames.h"

#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	const char* const Prefix = "Screenshot_";
	// 番号の最低の桁数
	constexpr int32 Digits = 5;

	bool IsAbsolutePath( const FString& Path )
	{
		return Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" );
	}

	bool HasExtension( const FString& Path )
	{
		const int32 Dot = Path.FindLast( "." );
		const int32 Slash = Path.Replace( "\\", "/" ).FindLast( "/" );
		return Dot != INDEX_NONE && Dot > Slash && Dot + 1 < Path.Len();
	}
}

FString NScreenshotNames::GetDefaultDirectory()
{
	return NFileSystem::FindProjectRoot() / "Saved" / "Screenshots";
}

int32 NScreenshotNames::FindNextNumber( TArrayView<const FString> ExistingFiles )
{
	int32 Largest = 0;
	const FString Head( Prefix );
	for ( int32 Index = 0; Index < ExistingFiles.Num(); ++Index )
	{
		const FString& Name = ExistingFiles[Index];
		if ( !Name.StartsWith( Head, ESearchCase::IgnoreCase ) )
		{
			continue;
		}
		// 接頭辞のあとに続く数字（5 桁以上）だけを数える
		int32 Number = 0;
		int32 Count = 0;
		for ( int32 Position = Head.Len(); Position < Name.Len() && Name[Position] >= '0' && Name[Position] <= '9' && Count < 9; ++Position )
		{
			Number = Number * 10 + ( Name[Position] - '0' );
			++Count;
		}
		if ( Count >= Digits && Number > Largest )
		{
			Largest = Number;
		}
	}
	return Largest + 1;
}

FString NScreenshotNames::MakeNumberedName( int32 Number, const char* Extension )
{
	return FString::Printf( "%s%05d%s", Prefix, Number, Extension != nullptr ? Extension : "" );
}

FString NScreenshotNames::ResolvePath( const FString& Directory, const FString& RequestedName, const char* Extension, int32 MinimumNumber, int32* OutNumber )
{
	if ( RequestedName.IsEmpty() )
	{
		const TArray<FString> Existing = NFileSystem::ListFiles( Directory, FString( Prefix ) + "*" );
		const int32 Number = NMath::Max( MinimumNumber, FindNextNumber( Existing ) );
		if ( OutNumber != nullptr )
		{
			*OutNumber = Number;
		}
		return ( Directory / MakeNumberedName( Number, Extension ) ).Replace( "\\", "/" );
	}
	FString Path = IsAbsolutePath( RequestedName ) ? RequestedName : Directory / RequestedName;
	if ( !HasExtension( RequestedName ) && Extension != nullptr )
	{
		Path += Extension;
	}
	return Path.Replace( "\\", "/" );
}
