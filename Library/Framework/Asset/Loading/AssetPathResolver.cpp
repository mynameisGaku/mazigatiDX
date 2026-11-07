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

void CAssetPathResolver::SetEngineContentDirectory( const FString& Directory )
{
	EngineContentDirectoryName = Directory;
	ResolvedEngineContent.Reset();
}

FString CAssetPathResolver::GetEngineContentDirectory() const
{
	if ( !ResolvedEngineContent.IsSet() )
	{
		TOptional<FString> Found = NFileSystem::FindDirectory( EngineContentDirectoryName );
		ResolvedEngineContent = Found.IsSet() ? NFileSystem::GetFullPath( Found.Get( FString() ) ) : FString();
	}
	const FString Root = ResolvedEngineContent.Get( FString() );
	return Root.IsEmpty() ? EngineContentDirectoryName : Root;
}

bool CAssetPathResolver::IsEnginePath( const FString& Path, FString* OutRelative )
{
	const FString Normalized = Path.Replace( "\\", "/" );
	if ( !Normalized.StartsWith( "Engine:/", ESearchCase::IgnoreCase ) )
	{
		return false;
	}
	if ( OutRelative != nullptr )
	{
		FString Relative = Normalized.Mid( FString( "Engine:/" ).Len() );
		while ( Relative.StartsWith( "/" ) )
		{
			Relative = Relative.Mid( 1 );
		}
		*OutRelative = Relative;
	}
	return true;
}

TOptional<FString> CAssetPathResolver::ResolvePath( const FString& Path ) const
{
	if ( Path.IsEmpty() )
	{
		return TOptional<FString>();
	}

	// エンジン組み込みの素材は、EngineContent フォルダの下だけから探す（ユーザーのルートや作業ディレクトリは見ない）
	FString EngineRelative;
	if ( IsEnginePath( Path, &EngineRelative ) )
	{
		// "../" で EngineContent の外へ出ようとするパスは、解決しない
		if ( EngineRelative.IsEmpty() || EngineRelative.Contains( ".." ) )
		{
			return TOptional<FString>();
		}
		const FString Candidate = GetEngineContentDirectory() / EngineRelative;
		if ( NFileSystem::FileExists( Candidate ) )
		{
			return NFileSystem::GetFullPath( Candidate );
		}
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
	if ( IsEnginePath( Path ) )
	{
		LOG( LogAsset, Error, "Cannot save '%s': the engine content is read-only.", *Path );
		return TOptional<FString>();
	}

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
