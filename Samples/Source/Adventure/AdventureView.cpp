#include "AdventureView.h"

#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"
#include "Library/Framework/Render/Mesh/MeshRender.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Adventure/AdventureActor.h"
#include "Source/Adventure/AdventureState.h"
#include "Source/Common/SoftLighting.h"

namespace
{
	const FColor32 SkyTop( 24, 28, 56 );
	const FColor32 SkyBottom( 70, 74, 110 );
}

CAdventureView::CAdventureView( const TSharedPtr<FAdventureState>& InState )
	: State( InState )
{}

void CAdventureView::Tick( float32 DeltaTime )
{
	State->Time += DeltaTime;
	// その場に出したエフェクト: 時間を進めて、終わったものを捨てる
	for ( int32 Index = State->Effects.Num() - 1; Index >= 0; --Index )
	{
		State->Effects[Index]->Update( DeltaTime );
		if ( State->Effects[Index]->IsFinished() )
		{
			State->Effects.RemoveAt( Index );
		}
	}
}

void CAdventureView::Draw( FDrawContext& CommandList )
{
	// 夜空（上が暗く、下が少し明るい。帯を重ねる）
	const int32 Bands = 8;
	for ( int32 Band = 0; Band < Bands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( Bands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.r ), static_cast<float32>( SkyBottom.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.g ), static_cast<float32>( SkyBottom.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.b ), static_cast<float32>( SkyBottom.b ), T ) ) );
		CommandList.Box( 0, FAdventureState::ScreenHeight * Band / Bands, FAdventureState::ScreenWidth, FAdventureState::ScreenHeight * ( Band + 1 ) / Bands, Color );
	}

	const TArray<TSharedPtr<CGameObject>> Scenery = State->Stage.FindAll( "Scenery" );

	// ① 光を受けるもの: メッシュの置き物と、コードのアクター
	{
		FCameraScope Scope( CommandList, State->Camera );
		if ( Scope.IsPushed() )
		{
			CommandList.DefaultLight( false );
			FLightScope Moon( CommandList, FLight::MakeDirectional( FVector3( -0.4f, -0.8f, 0.3f ), FColor( 0.7f, 0.74f, 0.95f ) ) );
			FSoftLightingScope Soft( CommandList );
			for ( const TSharedPtr<CGameObject>& Object : Scenery )
			{
				const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>();
				const FMeshRenderer* Mesh = Object->GetData<FMeshRenderer>();
				if ( Mesh != nullptr && Transform != nullptr )
				{
					Mesh->Record( CommandList, Transform->GetWorldTransform() );
				}
			}
			for ( const TSharedPtr<CAdventureActor>& Actor : GetScene()->FindObjects<CAdventureActor>() )
			{
				Actor->Record3D( CommandList );
			}
		}
	}
	// ② 粒子のエフェクト: 自分で光るので、ライトを切って、深度は見るが書かずに描く
	{
		FCameraScope Scope( CommandList, State->Camera );
		if ( Scope.IsPushed() )
		{
			FDepthState DepthState;
			DepthState.bTestDepth = true;
			DepthState.bWriteDepth = false;
			CommandList.Depth( DepthState );
			CommandList.WorldMatrix( FMatrix4x4::Identity );
			CommandList.Blend( EDrawBlend::Opaque );
			CommandList.Tint( FColor32( 255, 255, 255 ) );
			CommandList.UseLighting( false );
			for ( const TSharedPtr<CGameObject>& Object : Scenery )
			{
				const FEffectPlayer* Effect = Object->GetData<FEffectPlayer>();
				if ( Effect != nullptr && Effect->Playback.IsValid() )
				{
					Effect->Playback->Record( CommandList );
				}
			}
			for ( const TSharedPtr<IEffectPlayback>& Playback : State->Effects )
			{
				Playback->Record( CommandList );
			}
			CommandList.UseLighting( true );
		}
	}
}
