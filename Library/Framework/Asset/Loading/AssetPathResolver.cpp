// SPDX-License-Identifier: MIT
#include "AssetPathResolver.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Platform/System/FileSystem.h"

void CAssetPathResolver::SetRootDirectory( const FString& Directory )
{
	RootDirectoryName = Directory;
	ResolvedRoot.Reset();
}

FString CAssetPathResolver::GetRootDirectory() const
{
	if ( !ResolvedRoot.IsSet() )
	{
		TOptional<FString> Found = NFileSystem::FindDirectory( RootDirectoryName );
		ResolvedRoot = Found.IsSet() ? NFileSystem::GetFullPath( Found.Get( FString() ) ) : FString();
	}
	const FString Root = ResolvedRoot.Get( FString() );
	return Root.IsEmpty() ? RootDirectoryName : Root;
}

TOptional<FString> CAssetPathResolver::ResolvePath( const FString& Path ) const
{
	if ( Path.IsEmpty() )
	{
		return TOptional<FString>();
	}

	// ルートフォルダを最優先、次に作業ディレクトリ・exe のフォルダ（と親）。絶対パスはそのまま
	if ( NFileSystem::FileExists( Path ) && ( Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" ) ) )
	{
		return NFileSystem::GetFullPath( Path );
	}

	const FString Root = GetRootDirectory();
	if ( !Root.IsEmpty() )
	{
		const FString Candidate = Root / Path;
		if ( NFileSystem::FileExists( Candidate ) )
		{
			return NFileSystem::GetFullPath( Candidate );
		}
	}
	if ( TOptional<FString> Found = NFileSystem::FindFile( Path ); Found.IsSet() )
	{
		return NFileSystem::GetFullPath( Found.Get( FString() ) );
	}
	return TOptional<FString>();
}

TOptional<FString> CAssetPathResolver::PrepareSavePath( const FString& Path ) const
{
	FString Full = Path;
	const bool bAbsolute = Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" );
	if ( !bAbsolute )
	{
		Full = GetRootDirectory() / Path;
	}
	Full = Full.Replace( "\\", "/" );

	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 && !NFileSystem::EnsureDirectory( Full.Left( Slash ) ) )
	{
		LOG( LogAsset, Error, "Cannot save '%s': the folder could not be created.", *Full );
		return TOptional<FString>();
	}
	return NFileSystem::GetFullPath( Full );
}

FString CAssetPathResolver::MakeKey( const FString& FullPath )
{
	return FullPath.Replace( "\\", "/" ).ToLower();
}
