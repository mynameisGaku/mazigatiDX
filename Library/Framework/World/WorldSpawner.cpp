#include "WorldSpawner.h"

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/World/WorldHierarchy.h"

DEFINE_LOG_CATEGORY_STATIC( LogWorld, Log )

//=============================================================================
// FWorldSpawnContext
//=============================================================================
void FWorldSpawnContext::Warn( const FString& Message ) const
{
	const FString Text = FString::Printf( "world object #%d ('%s'): %s", Definition.Id, *Definition.Name, *Message );
	Report.Warnings.Add( Text );
	LOG( LogWorld, Warning, "%s", *Text );
}

//=============================================================================
// FWorldBindings
//=============================================================================
void FWorldBindings::Add( const FString& Tag, FWorldBindFactory Factory )
{
	for ( FEntry& Entry : Entries )
	{
		if ( Entry.Tag == Tag )
		{
			Entry.Factory = MoveTemp( Factory );
			return;
		}
	}
	FEntry Entry;
	Entry.Tag = Tag;
	Entry.Factory = MoveTemp( Factory );
	Entries.Add( MoveTemp( Entry ) );
}

const FWorldBindFactory* FWorldBindings::Find( const FString& Tag ) const
{
	for ( const FEntry& Entry : Entries )
	{
		if ( Entry.Tag == Tag ) return &Entry.Factory;
	}
	return nullptr;
}

//=============================================================================
// FWorldSpawnerRegistry
//=============================================================================
void FWorldSpawnerRegistry::Add( const FString& Name, FWorldComponentSpawner Spawner )
{
	for ( FEntry& Entry : Entries )
	{
		if ( Entry.Name == Name )
		{
			Entry.Spawner = MoveTemp( Spawner );
			return;
		}
	}
	FEntry Entry;
	Entry.Name = Name;
	Entry.Spawner = MoveTemp( Spawner );
	Entries.Add( MoveTemp( Entry ) );
}

bool FWorldSpawnerRegistry::Remove( const FString& Name )
{
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		if ( Entries[i].Name == Name )
		{
			Entries.RemoveAt( i );
			return true;
		}
	}
	return false;
}

bool FWorldSpawnerRegistry::Has( const FString& Name ) const
{
	for ( const FEntry& Entry : Entries )
	{
		if ( Entry.Name == Name ) return true;
	}
	return false;
}

FWorldSpawnerRegistry FWorldSpawnerRegistry::CreateWithBuiltins()
{
	FWorldSpawnerRegistry Registry;
	NWorld::RegisterBuiltinSpawners( Registry );
	return Registry;
}

FWorldSpawnerRegistry& FWorldSpawnerRegistry::Global()
{
	static FWorldSpawnerRegistry Registry = CreateWithBuiltins();
	return Registry;
}

//=============================================================================
// FWorldInstance
//=============================================================================
void FWorldInstance::AddPlaceholder( int32 ObjectId, const TSharedPtr<CGameObject>& Placeholder )
{
	FEntry Entry;
	Entry.ObjectId = ObjectId;
	Entry.Placeholder = Placeholder;
	Entries.Add( MoveTemp( Entry ) );
}

void FWorldInstance::AddNested( int32 HostId, const TSharedPtr<FWorldInstance>& Instance )
{
	FNestedEntry Entry;
	Entry.HostId = HostId;
	Entry.Instance = Instance;
	Nested.Add( MoveTemp( Entry ) );
}

const FWorldInstance* FWorldInstance::FindNested( int32 HostId ) const
{
	for ( const FNestedEntry& Entry : Nested )
	{
		if ( Entry.HostId == HostId ) return Entry.Instance.Get();
	}
	return nullptr;
}

void FWorldInstance::AddBound( int32 ObjectId, const FString& Tag, const TSharedPtr<CGameObject>& Object )
{
	for ( FEntry& Entry : Entries )
	{
		if ( Entry.ObjectId != ObjectId ) continue;
		FBound Bound;
		Bound.Tag = Tag;
		Bound.Object = Object;
		Entry.Bound.Add( MoveTemp( Bound ) );
		return;
	}
}

TSharedPtr<CGameObject> FWorldInstance::FindById( int32 ObjectId ) const
{
	for ( const FEntry& Entry : Entries )
	{
		if ( Entry.ObjectId != ObjectId ) continue;
		TSharedPtr<CGameObject> Object = Entry.Placeholder.Pin();
		return Object.IsValid() && Object->IsAlive() ? Object : TSharedPtr<CGameObject>();
	}
	return TSharedPtr<CGameObject>();
}

TSharedPtr<CGameObject> FWorldInstance::FindFirst( const FString& Tag ) const
{
	const TArray<TSharedPtr<CGameObject>> All = FindAll( Tag );
	return All.IsEmpty() ? TSharedPtr<CGameObject>() : All[0];
}

TArray<TSharedPtr<CGameObject>> FWorldInstance::FindAll( const FString& Tag ) const
{
	TArray<TSharedPtr<CGameObject>> Result;
	if ( !Definition.IsValid() ) return Result;
	for ( const FEntry& Entry : Entries )
	{
		const FWorldObject* Object = Definition->FindObject( Entry.ObjectId );
		if ( Object == nullptr || !Object->HasTag( Tag ) ) continue;
		if ( TSharedPtr<CGameObject> Placeholder = Entry.Placeholder.Pin(); Placeholder.IsValid() && Placeholder->IsAlive() ) Result.Add( Placeholder );
	}
	for ( const FNestedEntry& Entry : Nested )
	{
		if ( Entry.Instance.IsValid() ) Result.Append( Entry.Instance->FindAll( Tag ) );
	}
	return Result;
}

TSharedPtr<CGameObject> FWorldInstance::FindByName( const FString& Name ) const
{
	if ( !Definition.IsValid() ) return TSharedPtr<CGameObject>();
	for ( const FEntry& Entry : Entries )
	{
		const FWorldObject* Object = Definition->FindObject( Entry.ObjectId );
		if ( Object == nullptr || Object->Name != Name ) continue;
		if ( TSharedPtr<CGameObject> Placeholder = Entry.Placeholder.Pin(); Placeholder.IsValid() && Placeholder->IsAlive() ) return Placeholder;
	}
	for ( const FNestedEntry& Entry : Nested )
	{
		if ( !Entry.Instance.IsValid() ) continue;
		if ( TSharedPtr<CGameObject> Found = Entry.Instance->FindByName( Name ); Found.IsValid() ) return Found;
	}
	return TSharedPtr<CGameObject>();
}

void FWorldInstance::Despawn()
{
	for ( FEntry& Entry : Entries )
	{
		if ( TSharedPtr<CGameObject> Placeholder = Entry.Placeholder.Pin() ) Placeholder->Destroy();
	}
	Entries.Empty();
	for ( FNestedEntry& Entry : Nested )
	{
		if ( Entry.Instance.IsValid() ) Entry.Instance->Despawn();   // 入れ子の置き物は、ホストの子なので、ホストと一緒に消えるが、記録も片付ける
	}
	Nested.Empty();
}

//=============================================================================
// 設置
//=============================================================================
FWorldInstance NWorld::Spawn( CScene& Scene, const TSharedPtr<FWorldDefinition>& Definition, const FWorldBindings& Bindings, const FWorldSpawnOptions& Options )
{
	FWorldInstance Instance;
	Instance.SetDefinition( Definition );
	FWorldSpawnReport& Report = Instance.EditReport();
	if ( !Definition.IsValid() )
	{
		LOG( LogWorld, Error, "Spawn: no world definition was given" );
		return Instance;
	}

	const FWorldDefinition& World = *Definition;
	const FWorldSpawnerRegistry& Registry = Options.Registry != nullptr ? *Options.Registry : FWorldSpawnerRegistry::Global();
	CAssetManager* Assets = Options.Assets != nullptr ? Options.Assets : GetAppSubsystem<CAssetManager>();

	const NWorld::FWorldSpawnOrder Order = NWorld::ComputeSpawnOrder( World, !Options.bSpawnDisabled );
	for ( const int32 Index : Order.Orphans )
	{
		const FWorldObject& Object = World.Objects[Index];
		const FString Message = FString::Printf( "world object #%d ('%s'): the parent #%d does not exist; it is placed as a top-level object", Object.Id, *Object.Name, Object.Parent );
		Report.Warnings.Add( Message );
		LOG( LogWorld, Warning, "%s", *Message );
	}
	for ( const int32 Index : Order.Cyclic )
	{
		const FWorldObject& Object = World.Objects[Index];
		const FString Message = FString::Printf( "world object #%d ('%s'): its parents form a loop; it was not placed", Object.Id, *Object.Name );
		Report.Warnings.Add( Message );
		LOG( LogWorld, Warning, "%s", *Message );
	}
	Report.NumSkipped = Order.Cyclic.Num();
	Report.NumDisabled = Options.bSpawnDisabled ? 0 : World.Objects.Num() - Order.Order.Num() - Order.Cyclic.Num();

	// 入れ子の道筋（いちばん外側のワールドのファイルは、WorldPath。そこから、入れ子のファイルをたどってきた道筋が、PrefabStack）
	const TArray<FString>& Stack = Options.PrefabStack;
	const FString SelfKey = NWorld::NormalizeAssetPath( Options.WorldPath );

	TMap<int32, TSharedPtr<CGameObject>> Placed;
	for ( const int32 Index : Order.Order )
	{
		const FWorldObject& Definition_ = World.Objects[Index];

		TSharedPtr<CGameObject> Object = Scene.SpawnObject<CGameObject>();
		Object->SetName( Definition_.Name );

		// 位置・向き・大きさ。親があれば親からの相対（親の置き物の子）、最上位ならワールド全体の置き場所を掛ける
		const TSharedPtr<CGameObject>* Parent = Definition_.Parent != WorldNoId ? Placed.Find( Definition_.Parent ) : nullptr;
		FTransform Local = Definition_.Transform.ToTransform();
		if ( Parent == nullptr && Options.AttachTo == nullptr ) Local = FTransform::Combine( Options.Root, Local );
		CTransformComponent* Transform = Object->AddComponent<CTransformComponent>();
		Transform->SetLocalTransform( Local );
		if ( Parent != nullptr ) Object->AttachToParent( Parent->Get() );
		else if ( Options.AttachTo != nullptr ) Object->AttachToParent( Options.AttachTo );   // 入れ子のワールド: 最上位は、ホストの子（ホストからの相対）

		FWorldObjectRef Ref;
		Ref.World = Definition;
		Ref.ObjectId = Definition_.Id;
		Object->AddData( Ref );

		Placed.Add( Definition_.Id, Object );
		Instance.AddPlaceholder( Definition_.Id, Object );
		++Report.NumSpawned;

		// 部品（登録された順）
		FWorldSpawnContext Context{ Scene, World, Definition_, *Object, FTransform::Combine( Options.Root, NWorld::ComputeWorldTransform( World, Definition_.Id ) ), Assets, Report };
		for ( const FWorldSpawnerRegistry::FEntry& Entry : Registry.GetEntries() ) Entry.Spawner( Context );
		for ( const FWorldRawComponent& Raw : Definition_.RawComponents )
		{
			if ( !Registry.Has( Raw.Type ) ) Context.Warn( "the component '" + Raw.Type + "' is not supported by this version; it was not placed" );
		}

		// 入れ子のワールド（Prefab 部品）: 別の .world を、この置き物の子として置く
		if ( Definition_.Prefab.IsSet() )
		{
			const FString& PrefabPath = Definition_.Prefab.GetValue().Path;
			const FString Key = NWorld::NormalizeAssetPath( PrefabPath );
			bool bLoop = !SelfKey.IsEmpty() && Key == SelfKey;
			for ( const FString& Visited : Stack ) bLoop = bLoop || Visited == Key;
			if ( PrefabPath.IsEmpty() ) Context.Warn( "the prefab has no file; nothing was placed" );
			else if ( bLoop ) Context.Warn( "the prefab '" + PrefabPath + "' leads back to itself; it was not placed" );
			else if ( Stack.Num() >= WorldMaxPrefabDepth ) Context.Warn( FString::Printf( "the prefab '%s' is nested deeper than %d levels; it was not placed", *PrefabPath, WorldMaxPrefabDepth ) );
			else if ( Assets == nullptr ) Context.Warn( "the prefab '" + PrefabPath + "' was not placed: the asset manager is not running" );
			else
			{
				const TSharedPtr<CWorldAsset> Prefab = Assets->LoadWorld( PrefabPath );
				if ( !Prefab.IsValid() ) Context.Warn( "the prefab '" + PrefabPath + "' could not be loaded" );
				else
				{
					FWorldSpawnOptions Inner = Options;
					Inner.Root = Context.WorldTransform;   // 入れ子の中の物理などが使う、ワールドでの位置
					Inner.AttachTo = Object.Get();
					Inner.PrefabStack = Stack;   // WorldPath は、そのまま（いちばん外側のワールドのファイル）
					Inner.PrefabStack.Add( Key );
					TSharedPtr<FWorldInstance> InnerInstance = MakeShared<FWorldInstance>( NWorld::Spawn( Scene, Prefab->GetDefinitionShared(), Bindings, Inner ) );
					const FWorldSpawnReport& InnerReport = InnerInstance->GetReport();
					Report.NumSpawned += InnerReport.NumSpawned;
					Report.NumDisabled += InnerReport.NumDisabled;
					Report.NumBound += InnerReport.NumBound;
					Report.NumSkipped += InnerReport.NumSkipped;
					Report.NumPrefabs += InnerReport.NumPrefabs + 1;
					for ( const FString& Message : InnerReport.Warnings ) Report.Warnings.Add( "prefab '" + PrefabPath + "': " + Message );
					Instance.AddNested( Definition_.Id, InnerInstance );
				}
			}
		}

		// タグに結び付けた生成物（タグの付いた順）。置き物の子になる
		for ( const FString& Tag : Definition_.Tags )
		{
			const FWorldBindFactory* Factory = Bindings.Find( Tag );
			if ( Factory == nullptr ) continue;
			const FWorldBindContext BindContext{ Scene, World, Definition_, *Object, Tag };
			TSharedPtr<CGameObject> Bound = ( *Factory )( BindContext );
			if ( !Bound.IsValid() )
			{
				Context.Warn( "the binding for the tag '" + Tag + "' did not create an object" );
				continue;
			}
			// 置き物に付いて動くよう、位置の部品が無ければ足す（位置は置き物と同じ）
			if ( Bound->GetComponent<CTransformComponent>() == nullptr ) Bound->AddComponent<CTransformComponent>();
			Bound->AttachToParent( Object.Get() );
			Instance.AddBound( Definition_.Id, Tag, Bound );
			++Report.NumBound;
		}
	}

	// 全オブジェクトを配置してから参照を解決し、生成順に依存させない。
	// 自分自身・置かれなかった対象・有限でないずれは接続せず、警告して定義側の終点のままにする。
	for ( const FWorldObject& Object : World.Objects )
	{
		if ( !Object.Effect.IsSet() || Object.Effect.GetValue().Target == WorldNoId ) continue;
		const FWorldEffect& Spec = Object.Effect.GetValue();
		TSharedPtr<CGameObject>* Owner = Placed.Find( Object.Id );
		if ( Owner == nullptr || !Owner->IsValid() ) continue;
		// 部品が置かれなかった場合は、その警告がすでに出ている。
		CEffectComponent* Effect = Owner->Get()->GetComponent<CEffectComponent>();
		if ( Effect == nullptr ) continue;
		TSharedPtr<CGameObject>* Target = Spec.Target != Object.Id ? Placed.Find( Spec.Target ) : nullptr;
		const bool bOffsetFinite = NMath::IsFinite( Spec.TargetOffset.x ) && NMath::IsFinite( Spec.TargetOffset.y ) && NMath::IsFinite( Spec.TargetOffset.z );
		if ( Target == nullptr || !Target->IsValid() || !bOffsetFinite )
		{
			const FString Message = FString::Printf( "world object #%d ('%s'): the effect's target #%d is the object itself, was not placed, or has an offset that is not finite; the end stays at its fallback", Object.Id, *Object.Name, Spec.Target );
			Report.Warnings.Add( Message );
			LOG( LogWorld, Warning, "%s", *Message );
			continue;
		}
		Effect->SetTargetObject( *Target, Spec.TargetOffset );
	}

	LOG( LogWorld, Log, "Spawn: placed %d object(s) of '%s' (%d bound, %d disabled, %d skipped, %d warning(s))", Report.NumSpawned, *World.Name, Report.NumBound, Report.NumDisabled, Report.NumSkipped,
		 Report.Warnings.Num() );
	return Instance;
}

FWorldInstance NWorld::Spawn( CScene& Scene, const FWorldDefinition& Definition, const FWorldBindings& Bindings, const FWorldSpawnOptions& Options )
{
	return Spawn( Scene, MakeShared<FWorldDefinition>( Definition ), Bindings, Options );
}
