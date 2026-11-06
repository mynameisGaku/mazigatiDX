// SPDX-License-Identifier: MIT
#include "SpriteSheetLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CSpriteSheetLoader::CSpriteSheetLoader( CAssetLoadContext& InContext, CTextureLoader& InTextures )
	: Context( InContext )
	, Textures( InTextures )
{
}

void CSpriteSheetLoader::AddPending( const TSharedPtr<CSpriteSheet>& Sheet )
{
	if ( !Sheet->IsReady() )
	{
		Pending.Add( Sheet );
	}
}

TSharedPtr<CSpriteSheet> CSpriteSheetLoader::Create( const TSharedPtr<CTexture>& Texture, const FSpriteSheetGrid& Grid, const FString& Name )
{
	GAME_THREAD_ONLY( nullptr );
	TSharedPtr<CSpriteSheet> Sheet = Create( Texture, Name );
	if ( Sheet.IsValid() )
	{
		Sheet->BuildGrid( Grid );
		AddPending( Sheet );
	}
	return Sheet;
}

TSharedPtr<CSpriteSheet> CSpriteSheetLoader::Create( const TSharedPtr<CTexture>& Texture, const FString& Name )
{
	GAME_THREAD_ONLY( nullptr );
	if ( !Texture.IsValid() )
	{
		LOG( LogAsset, Error, "Cannot create the sprite sheet '%s': no texture was given.", *Name );
		return nullptr;
	}
	return MakeShared<CSpriteSheet>( Name.IsEmpty() ? Texture->GetPath() : Name, Texture );
}

TSharedPtr<CSpriteSheet> CSpriteSheetLoader::Create( const FSpriteSheetDefinition& Definition, const FString& Name, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	TSharedPtr<CTexture> Texture = Textures.Load( Definition.TexturePath, bAsync );
	if ( !Texture.IsValid() )
	{
		return nullptr;   // 原因は CTextureLoader がログに出している
	}

	TSharedPtr<CSpriteSheet> Sheet = MakeShared<CSpriteSheet>( Name.IsEmpty() ? Definition.TexturePath : Name, Texture );
	Sheet->SetDefinition( Definition );
	AddPending( Sheet );
	return Sheet;
}

bool CSpriteSheetLoader::Save( const FString& Path, const FSpriteSheetDefinition& Definition )
{
	const TOptional<FString> Full = Context.GetPaths().PrepareSavePath( Path );
	if ( !Full.IsSet() )
	{
		return false;
	}

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the sprite sheet definition '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );   // 古い内容のキャッシュは捨てる
	LOG( LogAsset, Log, "Saved the sprite sheet definition '%s' (%d grid(s), %d frame(s), %d clip(s))", *Full.Get( FString() ), Definition.Grids.Num(), Definition.GetNumFrames(), Definition.Clips.Num() );
	return true;
}

TSharedPtr<CSpriteSheet> CSpriteSheetLoader::Load( const FString& DefinitionPath, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "sprite sheet", DefinitionPath );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CSpriteSheet> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadSpriteSheet", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	if ( !Json.LoadFromFile( FullPath ) )
	{
		LOG( LogAsset, Error, "Failed to read the sprite sheet definition '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}

	FSpriteSheetDefinition Definition;
	if ( !FSpriteSheetDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		LOG( LogAsset, Error, "The sprite sheet definition '%s' is not a JSON object.", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	if ( Definition.TexturePath.IsEmpty() )
	{
		LOG( LogAsset, Error, "The sprite sheet definition '%s' has no \"Texture\".", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CTexture> Texture = Textures.Load( Definition.TexturePath, bAsync );
	if ( !Texture.IsValid() )
	{
		return nullptr;   // 原因は CTextureLoader がログに出している
	}

	TSharedPtr<CSpriteSheet> Sheet = MakeShared<CSpriteSheet>( FullPath, Texture );
	Sheet->SetDefinition( Definition );
	Context.GetRecorder().RecordLoad( *Sheet, FullPath, StartTick );
	AddPending( Sheet );
	Cache.Add( Key, Sheet );
	LOG( LogAsset, Verbose, "Loaded the sprite sheet '%s' (%d frame(s), %d clip(s))", *FullPath, Sheet->GetNumFrames(), Sheet->GetNumClips() );
	return Sheet;
}

void CSpriteSheetLoader::Update()
{
	// 画像の読み込みを待っていたスプライトシートのコマ割りを、読み込みが終わったら作る
	for ( int32 Index = Pending.Num() - 1; Index >= 0; --Index )
	{
		const TSharedPtr<CSpriteSheet> Sheet = Pending[Index].Pin();
		if ( !Sheet.IsValid() || Sheet->IsReady() )
		{
			Pending.RemoveAtSwap( Index );
		}
	}
}

void CSpriteSheetLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CSpriteSheetLoader::Prune()
{
	Cache.Prune();
}

void CSpriteSheetLoader::Clear()
{
	Pending.Empty();
	Cache.Clear();
}
