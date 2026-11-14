#include "AdventureSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Data/WorldAsset.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Framework/Render/Mesh/MeshRender.h"
#include "Library/Framework/SaveGame/SaveGameStorage.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/ProjectPaths.h"
#include "Source/Adventure/AdventureCampfire.h"
#include "Source/Adventure/AdventureChest.h"
#include "Source/Adventure/AdventureEnemy.h"
#include "Source/Adventure/AdventureGate.h"
#include "Source/Adventure/AdventureHud.h"
#include "Source/Adventure/AdventureParts.h"
#include "Source/Adventure/AdventurePickup.h"
#include "Source/Adventure/AdventurePlayer.h"
#include "Source/Adventure/AdventureSave.h"
#include "Source/Adventure/AdventureState.h"
#include "Source/Adventure/AdventureView.h"
#include "Source/Audio/ToneMaker.h"
#include "Source/Catalog/SampleCatalog.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CAdventureSample> Registrar( FSampleInfo { "Adventure", "Adventure: escape the ruins (reference game)", "A small 3D action game that combines the samples: a world file, a state-machine hero, behavior-tree enemies, items, UI and save data. Find the key, open the gate, reach the altar.", 120 } );

	constexpr int32 ViewOrder = 0;
	constexpr int32 HudOrder = 100;
	const char* const QuickSlot = "AdventureQuick";

	TArray<uint8> ToBytes( const NlohmannJson& Json )
	{
		const std::string Text = Json.dump();
		TArray<uint8> Bytes;
		for ( const char Character : Text )
		{
			Bytes.Add( static_cast<uint8>( Character ) );
		}
		return Bytes;
	}

	bool FromBytes( const TArray<uint8>& Bytes, NlohmannJson& OutJson )
	{
		const std::string Text( reinterpret_cast<const char*>( Bytes.GetData() ), static_cast<size_t>( Bytes.Num() ) );
		OutJson = NlohmannJson::parse( Text, nullptr, false );
		return !OutJson.is_discarded();
	}

	// セーブデータの検査: 保存 → 読み込みで同じ / 形の悪いデータを直して読む / 読めない形は断る
	bool RunSelfCheck( const CSaveGameStorage& Storage )
	{
		bool bOk = true;
		const auto Check = [&bOk]( bool bCondition, const char* Name )
		{
			if ( !bCondition )
			{
				LOG( LogSamples, Warning, "Adventure self-test: failed: %s", Name );
				bOk = false;
			}
		};
		FAdventureSave Original;
		Original.Hp = 63;
		Original.Potions = 4;
		Original.Keys = 1;
		Original.Gems = 7;
		Original.bGateOpen = true;
		Original.Position = FVector3( 3.5f, 0.0f, -2.25f );
		Original.OpenedChests.Add( 12 );
		Original.OpenedChests.Add( 14 );
		Original.DefeatedEnemies.Add( 33 );
		Original.PickedUp.Add( 40 );
		const char* Slot = "AdventureSelfTest";
		Check( Storage.Write( Slot, ToBytes( Original.ToJson() ) ), "write the save" );
		TArray<uint8> Bytes;
		NlohmannJson Json;
		FAdventureSave Loaded;
		Check( Storage.Read( Slot, Bytes ) == ESaveLoadResult::Ok, "read the save" );
		Check( FromBytes( Bytes, Json ) && FAdventureSave::FromJson( Json, Loaded ), "parse the save" );
		Check( Loaded.IsSameAs( Original ), "the loaded data is the same as the saved data" );

		const NlohmannJson Bad = NlohmannJson::parse( R"({"Hp":-4,"Potions":99,"Keys":-1,"Gems":5,"Position":[999,5,"x"],"OpenedChests":[3,"a",-2,7],"DefeatedEnemies":5})" );
		FAdventureSave Repaired;
		Check( FAdventureSave::FromJson( Bad, Repaired ), "read the broken data" );
		Check( Repaired.Hp == 1 && Repaired.Potions == FAdventureState::MaxPotions && Repaired.Keys == 0 && Repaired.Gems == 5, "numbers are put back into range" );
		Check( Repaired.OpenedChests.Num() == 2 && Repaired.OpenedChests[0] == 3 && Repaired.OpenedChests[1] == 7, "only valid ids are kept" );
		Check( Repaired.DefeatedEnemies.IsEmpty(), "a non-array list becomes empty" );
		Check( !FAdventureSave::FromJson( NlohmannJson::parse( R"({"Potions":1})" ), Repaired ), "data without HP is refused" );
		FAdventureState State;
		Original.Apply( State );
		Check( State.Hp == 63 && State.Keys == 1 && State.bGateOpen && FAdventureState::Contains( State.OpenedChests, 14 ), "applying the save sets the progress" );
		Check( FAdventureSave::Capture( State ).IsSameAs( [&Original] { FAdventureSave Copy = Original; Copy.Position = FVector3::Zero; return Copy; }() ), "capturing the state gives the same progress" );
		Storage.Delete( Slot );
		LOG( LogSamples, Log, "Adventure self-test: %s", bOk ? "PASS" : "FAIL" );
		return bOk;
	}
}

void CAdventureSample::OnEnter()
{
	// ステートマシン・ビヘイビアツリーが呼ぶ部品を登録する（1 回だけ）
	NAdventureParts::Register();

	State = MakeShared<FAdventureState>();
	// 効果音: 計算で作った短い音（始めは無音。鳴るのは、操作したときだけ）
	State->HitSound = NToneMaker::MakeSound( "AdventureHit", NToneMaker::MakeNote( 440.0f, 0.12f, 0.5f ) );
	State->HurtSound = NToneMaker::MakeSound( "AdventureHurt", NToneMaker::MakeNote( 180.0f, 0.2f, 0.5f ) );
	State->PickupSound = NToneMaker::MakeSound( "AdventurePickup", NToneMaker::MakeNote( 880.0f, 0.15f, 0.4f ) );
	State->OpenSound = NToneMaker::MakeSound( "AdventureOpen", NToneMaker::MakeNote( 330.0f, 0.3f, 0.5f ) );
	State->Say( "Find the key, open the gate, and reach the altar." );

	SpawnObject<CAdventureView>( State )->SetDrawOrder( ViewOrder );
	Player = SpawnObject<CAdventurePlayer>( State );
	State->Player = Player;
	SpawnObject<CAdventureHud>( State )->SetDrawOrder( HudOrder );

	// 起動の引数 --start: 2 つ目の部屋から / 3 つ目の部屋から（鍵を使って、門を開けた状態）
	const TOptional<FString> Start = NCommandLine::GetValue( "start" );
	if ( Start.IsSet() && Start.GetValue() == "3" )
	{
		State->bGateOpen = true;
	}
	SpawnStage();
	if ( Start.IsSet() && Start.GetValue() == "2" )
	{
		Player->TeleportTo( FVector3( 0.0f, 0.0f, 0.0f ) );
	}
	else if ( Start.IsSet() && Start.GetValue() == "3" )
	{
		Player->TeleportTo( FVector3( 0.0f, 0.0f, 12.0f ) );
	}

	if ( NCommandLine::HasFlag( "selftest" ) )
	{
		RunSelfCheck( CSaveGameStorage( FProjectPaths().GetSaveGamesDirectory() ) );
	}
}

void CAdventureSample::OnExit()
{
	// 次のシーンに、置いたワールドを残さない
	if ( State.IsValid() )
	{
		State->Stage.Despawn();
	}
}

void CAdventureSample::SpawnStage()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	const TSharedPtr<CWorldAsset> Level = Assets != nullptr ? Assets->LoadWorld( "Worlds/ruins.gxworld" ) : TSharedPtr<CWorldAsset>();
	if ( !Level.IsValid() )
	{
		LOG( LogSamples, Warning, "Adventure: Worlds/ruins.gxworld could not be loaded" );
		return;
	}

	// タグに、コードを結び付ける。倒した敵・開けた宝箱・拾ったもの・開けた門は、置いてすぐ、消す（セーブの続きから）
	FWorldBindings Bindings;
	const TSharedPtr<FAdventureState> StateRef = State;
	Bindings.Add( "Enemy", [StateRef]( const FWorldBindContext& Context )
	{
		const FWorldProp* Kind = Context.Definition.FindProp( "Kind" );
		const TSharedPtr<CAdventureEnemy> Enemy = Context.Scene.SpawnObject<CAdventureEnemy>( StateRef, Kind != nullptr ? Kind->StringValue : FString( "Slime" ), Context.Definition.Id );
		if ( FAdventureState::Contains( StateRef->DefeatedEnemies, Context.Definition.Id ) )
		{
			Enemy->Destroy();
		}
		return TSharedPtr<CGameObject>( Enemy );
	} );
	Bindings.Add( "Pickup", [StateRef]( const FWorldBindContext& Context )
	{
		const FWorldProp* Item = Context.Definition.FindProp( "Item" );
		const TSharedPtr<CAdventurePickup> Pickup = Context.Scene.SpawnObject<CAdventurePickup>( StateRef, Item != nullptr ? Item->StringValue : FString( "Potion" ), Context.Definition.Id, FVector3::Zero );
		if ( FAdventureState::Contains( StateRef->PickedUp, Context.Definition.Id ) )
		{
			Pickup->Destroy();
		}
		return TSharedPtr<CGameObject>( Pickup );
	} );
	Bindings.Add( "Chest", [StateRef]( const FWorldBindContext& Context )
	{
		const FWorldProp* Item = Context.Definition.FindProp( "Item" );
		const FWorldProp* Count = Context.Definition.FindProp( "Count" );
		return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CAdventureChest>( StateRef, Item != nullptr ? Item->StringValue : FString( "Potion" ), Count != nullptr ? static_cast<int32>( Count->IntValue ) : 1,
			Context.Definition.Id, FAdventureState::Contains( StateRef->OpenedChests, Context.Definition.Id ) ) );
	} );
	Bindings.Add( "Gate", [StateRef]( const FWorldBindContext& Context )
	{
		if ( StateRef->bGateOpen )
		{
			Context.Placeholder.Destroy();
		}
		return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CAdventureGate>( StateRef ) );
	} );
	Bindings.Add( "Campfire", [StateRef]( const FWorldBindContext& Context )
	{
		return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CAdventureCampfire>( StateRef ) );
	} );

	State->Stage = NWorld::Spawn( *this, Level->GetDefinitionShared(), Bindings );

	// 見た目を持つ置き物（タグ "Scenery"）: メッシュとエフェクトの自動描画を切って、CAdventureView が、カメラの中で描く
	for ( const TSharedPtr<CGameObject>& Object : State->Stage.FindAll( "Scenery" ) )
	{
		if ( FMeshRenderer* Mesh = Object->GetData<FMeshRenderer>() )
		{
			Mesh->bAutoDraw = false;
		}
		if ( FEffectPlayer* Effect = Object->GetData<FEffectPlayer>() )
		{
			Effect->bAutoDraw = false;
		}
	}
	// 通れない四角形（タグ "Solid"）: 置き物の位置と大きさから、地面の上から見た四角形にする（回転は、無い）
	State->Solids.Reset();
	for ( const TSharedPtr<CGameObject>& Object : State->Stage.FindAll( "Solid" ) )
	{
		const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>();
		if ( Transform == nullptr || Object->IsPendingDestroy() )
		{
			continue;
		}
		const FTransform World = Transform->GetWorldTransform();
		FAdventureSolid Solid;
		Solid.Object = Object;
		Solid.MinX = World.Position.x - World.Scale.x * 0.5f;
		Solid.MaxX = World.Position.x + World.Scale.x * 0.5f;
		Solid.MinZ = World.Position.z - World.Scale.z * 0.5f;
		Solid.MaxZ = World.Position.z + World.Scale.z * 0.5f;
		State->Solids.Add( Solid );
	}
	// 出口の位置と、主人公の最初の位置
	const TSharedPtr<CGameObject> Exit = State->Stage.FindFirst( "Exit" );
	if ( Exit.IsValid() && Exit->GetComponent<CTransformComponent>() != nullptr )
	{
		State->ExitPosition = Exit->GetComponent<CTransformComponent>()->GetWorldTransform().Position;
	}
	const TSharedPtr<CGameObject> Start = State->Stage.FindFirst( "PlayerStart" );
	if ( Start.IsValid() && Start->GetComponent<CTransformComponent>() != nullptr && Player.IsValid() )
	{
		Player->TeleportTo( Start->GetComponent<CTransformComponent>()->GetWorldTransform().Position );
	}
}

void CAdventureSample::ResetProgress()
{
	State->Hp = FAdventureState::MaxHp;
	State->Potions = 1;
	State->Keys = 0;
	State->Gems = 0;
	State->bGateOpen = false;
	State->OpenedChests.Reset();
	State->DefeatedEnemies.Reset();
	State->PickedUp.Reset();
	State->bPlayerDead = false;
	State->bWon = false;
	State->Messages.Reset();
	State->Say( "Find the key, open the gate, and reach the altar." );
}

void CAdventureSample::QuickSave( const char* Reason )
{
	CSaveGameStorage Storage( FProjectPaths().GetSaveGamesDirectory() );
	if ( Storage.Write( QuickSlot, ToBytes( FAdventureSave::Capture( *State ).ToJson() ) ) )
	{
		State->Say( FString::Printf( "%s", Reason ) );
	}
	else
	{
		State->Say( "Saving failed." );
	}
}

void CAdventureSample::QuickLoad()
{
	CSaveGameStorage Storage( FProjectPaths().GetSaveGamesDirectory() );
	TArray<uint8> Bytes;
	const ESaveLoadResult Result = Storage.Read( QuickSlot, Bytes );
	if ( Result == ESaveLoadResult::NotFound )
	{
		State->Say( "There is no save yet. Rest at the campfire, or press F5." );
		return;
	}
	NlohmannJson Json;
	FAdventureSave Save;
	if ( ( Result != ESaveLoadResult::Ok && Result != ESaveLoadResult::RecoveredFromBackup ) || !FromBytes( Bytes, Json ) || !FAdventureSave::FromJson( Json, Save ) )
	{
		State->Say( "The save is damaged and cannot be read." );
		return;
	}
	// 進み具合を戻して、ワールドを置き直し（倒した敵・開けた宝箱は、居ない・開いたまま）、位置へ戻る
	State->Stage.Despawn();
	Save.Apply( *State );
	State->Effects.Reset();
	SpawnStage();
	Player->TeleportTo( Save.Position );
	Player->Revive();
	State->Say( Result == ESaveLoadResult::RecoveredFromBackup ? "Loaded from the backup (the main save was damaged)." : "Loaded." );
}

void CAdventureSample::OnSampleTick( float32 DeltaTime )
{
	if ( State->bSaveRequested )
	{
		State->bSaveRequested = false;
		QuickSave( "You rest at the fire. HP restored, game saved." );
	}
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::F5 ) && !State->bPlayerDead )
	{
		QuickSave( "Game saved." );
	}
	if ( Input->IsKeyPressed( EKey::F9 ) )
	{
		QuickLoad();
	}
	if ( Input->IsKeyPressed( EKey::R ) && ( State->bPlayerDead || State->bWon ) )
	{
		// やり直し: 進み具合を、最初に戻して、置き直す
		State->Stage.Despawn();
		ResetProgress();
		State->Effects.Reset();
		SpawnStage();
		Player->Revive();
	}
}

void CAdventureSample::OnSampleDraw( FImmediateDraw& Screen )
{
}
