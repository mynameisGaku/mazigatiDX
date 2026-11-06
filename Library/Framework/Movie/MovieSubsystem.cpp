// SPDX-License-Identifier: MIT
#include "MovieSubsystem.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY( LogMovie )

REGISTER_SUBSYSTEM( CMovieSubsystem )

void CMovieSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Assets = Collection.InitializeDependency<CAssetSubsystem>();
	Backend = Collection.InitializeDependency<IMovieBackend>();
	if ( Backend == nullptr )
	{
		LOG( LogMovie, Warning, "No movie backend is available. Movies will not be played." );
	}
}

void CMovieSubsystem::Deinitialize()
{
	// 残っているムービーを閉じて、使えなくする（CMovie が先に壊れても、後に壊れても困らないように）
	for ( const TWeakPtr<CMovie>& Weak : Movies )
	{
		if ( TSharedPtr<CMovie> Movie = Weak.Pin() )
		{
			Movie->Detach();
		}
	}
	Movies.Empty();
	Backend = nullptr;
	Assets = nullptr;
}

void CMovieSubsystem::SetBackendOverride( IMovieBackend* InBackend )
{
	BackendOverride = InBackend;
}

TSharedPtr<CMovie> CMovieSubsystem::Open( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	IMovieBackend* MovieBackend = GetBackend();
	if ( MovieBackend == nullptr )
	{
		LOG( LogMovie, Warning, "Cannot open the movie '%s': no movie backend is available.", *Path );
		return nullptr;
	}

	// 絶対パスで実在すればそのまま、そうでなければアセットのルートフォルダから探す
	FString FullPath;
	if ( NFileSystem::FileExists( Path ) && ( Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" ) ) )
	{
		FullPath = Path;
	}
	else if ( Assets != nullptr )
	{
		const TOptional<FString> Resolved = Assets->ResolvePath( Path );
		if ( !Resolved.IsSet() )
		{
			LOG( LogMovie, Error, "Cannot open the movie '%s': the file was not found.", *Path );
			return nullptr;
		}
		FullPath = Resolved.Get( FString() );
	}
	else
	{
		LOG( LogMovie, Error, "Cannot open the movie '%s': the file was not found.", *Path );
		return nullptr;
	}

	TSharedPtr<CMovie> Movie = CMovie::Create( *MovieBackend, FullPath );
	if ( Movie.IsValid() )
	{
		// 手放された分を片付けてから足す
		for ( int32 Index = Movies.Num() - 1; Index >= 0; --Index )
		{
			if ( !Movies[Index].Pin().IsValid() )
			{
				Movies.RemoveAtSwap( Index );
			}
		}
		Movies.Add( Movie );
	}
	return Movie;
}
