#include "WorldSampleView.h"

#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"
#include "Library/Framework/Render/Mesh/MeshRender.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Common/SoftLighting.h"
#include "Source/World/WorldSampleActor.h"
#include "Source/World/WorldSampleState.h"

namespace
{
	const FColor32 SkyTop( 92, 148, 252 );
	const FColor32 SkyBottom( 196, 226, 255 );
}

CWorldSampleView::CWorldSampleView( const TSharedPtr<FWorldSampleState>& InState )
	: State( InState )
{}

void CWorldSampleView::Tick( float32 DeltaTime )
{
	State->Time += DeltaTime;
}

void CWorldSampleView::Draw( FDrawContext& CommandList )
{
	// 空（上が濃く、下が淡い。帯を重ねる）
	const int32 Bands = 8;
	for ( int32 Band = 0; Band < Bands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( Bands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.r ), static_cast<float32>( SkyBottom.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.g ), static_cast<float32>( SkyBottom.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.b ), static_cast<float32>( SkyBottom.b ), T ) ) );
		CommandList.Box( 0, FWorldSampleState::ScreenHeight * Band / Bands, FWorldSampleState::ScreenWidth, FWorldSampleState::ScreenHeight * ( Band + 1 ) / Bands, Color );
	}

	// ① 光を受けるもの: メッシュの置き物と、コードのアクター
	{
		FCameraScope Scope( CommandList, State->Camera );
		if ( Scope.IsPushed() )
		{
			CommandList.DefaultLight( false );
			FLightScope Sun( CommandList, FLight::MakeDirectional( FVector3( -0.45f, -0.8f, 0.4f ), FColor( 1.0f, 0.96f, 0.88f ) ) );
			FSoftLightingScope Soft( CommandList );
			for ( const TSharedPtr<CGameObject>& Object : State->Scenery )
			{
				const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>();
				const FMeshRenderer* Mesh = Object->GetData<FMeshRenderer>();
				if ( Mesh != nullptr && Transform != nullptr )
				{
					Mesh->Record( CommandList, Transform->GetWorldTransform() );
				}
			}
			for ( const TSharedPtr<CWorldSampleActor>& Actor : GetScene()->FindObjects<CWorldSampleActor>() )
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
			for ( const TSharedPtr<CGameObject>& Object : State->Scenery )
			{
				const FEffectPlayer* Effect = Object->GetData<FEffectPlayer>();
				if ( Effect != nullptr && Effect->Playback.IsValid() )
				{
					Effect->Playback->Record( CommandList );
				}
			}
			CommandList.UseLighting( true );
		}
	}
}
