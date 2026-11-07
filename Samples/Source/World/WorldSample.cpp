#include "WorldSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Data/WorldAsset.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Framework/Render/Mesh/MeshRender.h"
#include "Library/Framework/World/Runtime/WorldSpawner.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/World/WorldCoin.h"
#include "Source/World/WorldCrystal.h"
#include "Source/World/WorldExplorer.h"
#include "Source/World/WorldSampleState.h"
#include "Source/World/WorldSampleHud.h"
#include "Source/World/WorldSampleView.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CWorldSample> Registrar( FSampleInfo { "World", "World: a level loaded from a .gxworld", "A courtyard placed from a world file: meshes, fire effects, tagged objects bound to code (coins, spinning crystals). Walk around and collect the coins; R loads the world again.", 96 } );

	// 描く順: 中庭（置き物とアクターもここで記録される）
	constexpr int32 ViewOrder = 0;
	constexpr int32 HudOrder = 100;
}

void CWorldSample::OnEnter()
{
	State = MakeShared<FWorldSampleState>();
	SpawnObject<CWorldSampleView>( State )->SetDrawOrder( ViewOrder );
	SpawnStage();
	Explorer = SpawnObject<CWorldExplorer>( State );
	SpawnObject<CWorldSampleHud>( State )->SetDrawOrder( HudOrder );
}

void CWorldSample::SpawnStage()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	const TSharedPtr<CWorldAsset> Level = Assets != nullptr ? Assets->LoadWorld( "Worlds/courtyard.gxworld" ) : TSharedPtr<CWorldAsset>();
	if ( !Level.IsValid() )
	{
		LOG( LogSamples, Warning, "World: Worlds/courtyard.gxworld could not be loaded" );
		return;
	}

	// タグに、コードを結び付ける。ファイルには、位置・見た目・プロパティだけがあり、動きは、ここで決める
	FWorldBindings Bindings;
	const TSharedPtr<FWorldSampleState> StateRef = State;
	Bindings.Add( "Coin", [StateRef]( const FWorldBindContext& Context )
	{
		const FWorldProp* Value = Context.Definition.FindProp( "Value" );
		return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CWorldCoin>( StateRef, Value != nullptr ? static_cast<int32>( Value->IntValue ) : 1 ) );
	} );
	Bindings.Add( "Crystal", []( const FWorldBindContext& Context )
	{
		const FWorldProp* Speed = Context.Definition.FindProp( "Speed" );
		return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CWorldCrystal>( Speed != nullptr ? static_cast<float32>( Speed->FloatValue != 0.0 ? Speed->FloatValue : static_cast<float64>( Speed->IntValue ) ) : 45.0f ) );
	} );

	State->Stage = NWorld::Spawn( *this, Level->GetDefinitionShared(), Bindings );
	++State->NumLoads;

	// 見た目を持つ置き物（タグ "Scenery"）: メッシュとエフェクトの自動描画を切って、CWorldSampleView が、カメラの中で描く
	State->Scenery = State->Stage.FindAll( "Scenery" );
	for ( const TSharedPtr<CGameObject>& Object : State->Scenery )
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
	// 柱の位置（主人公がよける）
	State->PillarPositions.Empty();
	for ( const TSharedPtr<CGameObject>& Pillar : State->Stage.FindAll( "Pillar" ) )
	{
		if ( const CTransformComponent* Transform = Pillar->GetComponent<CTransformComponent>() )
		{
			State->PillarPositions.Add( Transform->GetWorldTransform().Position );
		}
	}
	State->CoinsCollected = 0;
	State->ScoreCollected = 0;
	State->CoinsTotal = State->Stage.FindAllBound<CWorldCoin>( "Coin" ).Num();
}

void CWorldSample::OnSampleTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::R ) )
	{
		// 置いたものを、まとめて消して、もう一度置く（コインも、結晶も、置き物の子なので、一緒に消える）
		State->Stage.Despawn();
		SpawnStage();
		if ( Explorer.IsValid() )
		{
			Explorer->ResetToStart();
		}
	}
}

void CWorldSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 64, "W A S D: move   Left / Right or Q / E: camera   Shift: run   R: load the world again", FColor32::Gray );
}
