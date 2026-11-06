// SPDX-License-Identifier: MIT
#include "ModelAnimationLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CModelAnimationLoader::CModelAnimationLoader( CAssetLoadContext& InContext, CModelLoader& InModels )
	: Context( InContext )
	, Models( InModels )
{
}

TSharedPtr<CModelAnimationAsset> CModelAnimationLoader::Create( const FModelAnimationDefinition& Definition, const FString& Name, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	// Model が空なら、モデルを持たないアニメーション集（モーション専用）
	TSharedPtr<CModel> Model;
	if ( !Definition.ModelPath.IsEmpty() )
	{
		Model = Models.Load( Definition.ModelPath, bAsync );
		if ( !Model.IsValid() )
		{
			return nullptr;   // 原因は CModelLoader がログに出している
		}
		if ( Model->IsReady() && Model->GetMeshCount() == 0 )
		{
			LOG( LogAsset, Warning, "The model '%s' of the animation set has no mesh (it only holds animations), so nothing will be drawn. Use it as a clip's Source instead.", *Definition.ModelPath );
		}
	}

	TArray<TSharedPtr<CModel>> Sources;
	for ( const FModelClipDef& Clip : Definition.Clips )
	{
		TSharedPtr<CModel> Source = Clip.SourcePath.IsEmpty() ? Model : Models.Load( Clip.SourcePath, bAsync );
		if ( !Source.IsValid() )
		{
			Source = Model;   // 読めなかったクリップは、モデル自身のアニメーションとして扱う（原因はログに出ている）
		}
		Sources.Add( Source );
	}
	const FString DefaultName = Definition.ModelPath.IsEmpty() ? FString( "(animation set)" ) : Definition.ModelPath;
	return MakeShared<CModelAnimationAsset>( Name.IsEmpty() ? DefaultName : Name, Definition, Model, Sources );
}

TSharedPtr<CModelAnimationAsset> CModelAnimationLoader::Load( const FString& DefinitionPath, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "model animation set", DefinitionPath );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CModelAnimationAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadModelAnimations", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FModelAnimationDefinition Definition;
	if ( !Json.LoadFromFile( FullPath ) || !FModelAnimationDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		LOG( LogAsset, Error, "Failed to read the model animation definition '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CModelAnimationAsset> Asset = Create( Definition, FullPath, bAsync );
	if ( !Asset.IsValid() )
	{
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the model animation definition '%s' (%d clip(s))", *FullPath, Definition.Clips.Num() );
	return Asset;
}

bool CModelAnimationLoader::Save( const FString& Path, const FModelAnimationDefinition& Definition )
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
		LOG( LogAsset, Error, "Failed to write the model animation definition '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the model animation definition '%s' (%d clip(s))", *Full.Get( FString() ), Definition.Clips.Num() );
	return true;
}

void CModelAnimationLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CModelAnimationLoader::Prune()
{
	Cache.Prune();
}

void CModelAnimationLoader::Clear()
{
	Cache.Clear();
}
