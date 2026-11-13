// SPDX-License-Identifier: MIT
#include "AssetPathResolver.h"

#include "Library/Core/File/File.h"
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
		if ( !Found.IsSet() )
		{
			// フォルダが近くに無いとき（フレームワークとは別の場所に作った、ゲームのプロジェクト）: 場所を書いたファイル（EngineContent.location）を読む。
			// 中身は、フォルダへのパス 1 行（絶対パスか、そのファイルのあるフォルダからの相対パス）
			const TOptional<FString> LocationFile = NFileSystem::FindFile( EngineContentDirectoryName + ".location" );
			TArray<uint8> Bytes;
			if ( LocationFile.IsSet() && NFile::ReadAllBytes( LocationFile.Get( FString() ), Bytes ) )
			{
				// 先頭の BOM と、最初の改行（13・10）より後ろは、読まない
				int32 Begin = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF ? 3 : 0;
				int32 End = Begin;
				while ( End < Bytes.Num() && Bytes[End] != 13 && Bytes[End] != 10 && Bytes[End] != 0 )
				{
					++End;
				}
				const FString Written = FString( reinterpret_cast<const char*>( Bytes.GetData() ) + Begin, End - Begin ).TrimStartAndEnd();
				if ( !Written.IsEmpty() )
				{
					// 相対パスは、ファイルのあるフォルダから
					const FString FilePath = NFileSystem::GetFullPath( LocationFile.Get( FString() ) ).Replace( "\\", "/" );
					int32 Slash = -1;
					for ( int32 Index = 0; Index < FilePath.Len(); ++Index )
					{
						Slash = FilePath[Index] == '/' ? Index : Slash;
					}
					const bool bAbsolute = Written.Len() >= 2 && ( Written[1] == ':' || Written.StartsWith( "\\\\" ) || Written.StartsWith( "//" ) );
					const FString Candidate = bAbsolute || Slash < 0 ? Written : FilePath.Left( Slash ) / Written;
					if ( NFileSystem::DirectoryExists( Candidate ) )
					{
						Found = Candidate;
					}
					else
					{
						LOG( LogAsset, Warning, "%s.location points to a folder that does not exist: %s", *EngineContentDirectoryName, *Candidate );
					}
				}
			}
		}
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
