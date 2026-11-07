#include "Platformer3DView.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Common/SoftLighting.h"
#include "Source/Platformer3D/Platformer3DActor.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	const FColor32 SkyTop( 92, 148, 252 );
	const FColor32 SkyBottom( 190, 224, 255 );
}

CPlatformer3DView::CPlatformer3DView( const TSharedPtr<FPlatformer3DState>& InState )
	: State( InState )
{}

void CPlatformer3DView::Tick( float32 DeltaTime )
{
	State->Time += DeltaTime;
	State->Level.Tick( DeltaTime );
}

void CPlatformer3DView::DrawSky( FDrawContext& CommandList ) const
{
	// 空（上が濃く、下が淡い。帯を重ねる）
	const int32 Bands = 8;
	for ( int32 Band = 0; Band < Bands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( Bands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.r ), static_cast<float32>( SkyBottom.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.g ), static_cast<float32>( SkyBottom.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.b ), static_cast<float32>( SkyBottom.b ), T ) ) );
		CommandList.Box( 0, FPlatformer3DState::ScreenHeight * Band / Bands, FPlatformer3DState::ScreenWidth, FPlatformer3DState::ScreenHeight * ( Band + 1 ) / Bands, Color );
	}
}

void CPlatformer3DView::RecordBlocks( FDrawContext& CommandList ) const
{
	for ( const FBlock3D& Block : State->Level.Blocks )
	{
		if ( !Block.IsActive )
		{
			continue;
		}
		FVector3 Min = Block.Min;
		FVector3 Max = Block.Max;
		// 叩かれて、持ち上がっているブロック
		if ( Block.BumpTime > 0.0f )
		{
			const float32 Lift = 0.25f * NMath::Sin( NMath::PI * Block.BumpTime / 0.2f );
			Min.y += Lift;
			Max.y += Lift;
		}
		switch ( Block.Kind )
		{
		case EBlock3DKind::Ground:
			// 土の色の箱に、上面だけ、薄い草の板を重ねる
			CommandList.Cube3D( Min, Max, FColor32( 168, 104, 52 ) );
			CommandList.Cube3D( FVector3( Min.x, Max.y - 0.2f, Min.z ), Max, FColor32( 88, 190, 70 ) );
			// 草の上の格子（4 メートルごと。進んだ距離と、高さの見当が付く）
			{
				const FColor32 GridColor( 60, 150, 50 );
				const float32 Top = Max.y + 0.01f;
				for ( float32 X = NMath::Ceil( Min.x / 4.0f ) * 4.0f; X <= Max.x; X += 4.0f )
				{
					CommandList.Line3D( FVector3( X, Top, Min.z ), FVector3( X, Top, Max.z ), GridColor );
				}
				for ( float32 Z = NMath::Ceil( Min.z / 4.0f ) * 4.0f; Z <= Max.z; Z += 4.0f )
				{
					CommandList.Line3D( FVector3( Min.x, Top, Z ), FVector3( Max.x, Top, Z ), GridColor );
				}
			}
			break;
		case EBlock3DKind::Wall:
			CommandList.Cube3D( Min, Max, FColor32( 176, 150, 120 ) );
			break;
		case EBlock3DKind::Pipe:
			CommandList.Cube3D( Min, Max, FColor32( 40, 168, 72 ) );
			CommandList.Cube3D( FVector3( Min.x - 0.1f, Max.y - 0.35f, Min.z - 0.1f ), FVector3( Max.x + 0.1f, Max.y, Max.z + 0.1f ), FColor32( 30, 140, 60 ) );
			break;
		case EBlock3DKind::Brick:
			CommandList.Cube3D( Min, Max, FColor32( 200, 96, 48 ) );
			break;
		case EBlock3DKind::Question:
		case EBlock3DKind::PowerUp:
		case EBlock3DKind::StarBlock:
			{
				// 点滅（ゆっくり明るさが変わる）
				const float32 Blink = 0.5f + 0.5f * NMath::Sin( State->Time * 6.0f );
				CommandList.Cube3D( Min, Max, FColor32( 244, static_cast<uint8>( 150.0f + 40.0f * Blink ), 40 ) );
				// 上の面の、白いしるし
				const FVector3 Center( ( Min.x + Max.x ) * 0.5f, Max.y, ( Min.z + Max.z ) * 0.5f );
				CommandList.Cube3D( Center + FVector3( -0.12f, 0.0f, -0.12f ), Center + FVector3( 0.12f, 0.03f, 0.12f ), FColor32( 255, 250, 220 ) );
			}
			break;
		case EBlock3DKind::Used:
			CommandList.Cube3D( Min, Max, FColor32( 132, 108, 84 ) );
			break;
		}
	}
}

void CPlatformer3DView::RecordCoins( FDrawContext& CommandList ) const
{
	for ( const FVector3& Coin : State->Level.Coins )
	{
		// 回って見えるように、横幅を変える
		const float32 Spin = NMath::Abs( NMath::Cos( State->Time * 5.0f + Coin.z * 0.4f ) );
		const FVector3 Half( 0.26f * Spin + 0.04f, 0.3f, 0.04f );
		CommandList.Cube3D( Coin - Half, Coin + Half, FColor32( 250, 206, 40 ) );
	}
}

void CPlatformer3DView::RecordGoal( FDrawContext& CommandList ) const
{
	const FVector3 Base = State->Level.GoalPosition;
	CommandList.Cube3D( Base + FVector3( -0.06f, 0.0f, -0.06f ), Base + FVector3( 0.06f, 6.0f, 0.06f ), FColor32( 236, 240, 236 ) );
	CommandList.Sphere3D( Base + FVector3( 0.0f, 6.1f, 0.0f ), 0.2f, 10, FColor32( 90, 200, 120 ) );
	// 旗
	CommandList.Cube3D( Base + FVector3( 0.06f, 4.8f, -0.03f ), Base + FVector3( 1.6f, 5.9f, 0.03f ), FColor32( 230, 70, 70 ) );
}

void CPlatformer3DView::Draw( FDrawContext& CommandList )
{
	DrawSky( CommandList );

	// このカメラで見える 3D を描く（使えない値のカメラのときは、何も描かない）
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	// 太陽（右上の手前から、奥の左下へ）と、やわらかい光（影の側も、真っ黒にしない）
	CommandList.DefaultLight( false );
	FLightScope Sun( CommandList, FLight::MakeDirectional( FVector3( -0.45f, -0.8f, 0.4f ), FColor( 1.0f, 0.96f, 0.88f ) ) );
	FSoftLightingScope Soft( CommandList );

	RecordBlocks( CommandList );
	RecordCoins( CommandList );
	RecordGoal( CommandList );
	for ( const TSharedPtr<CPlatformer3DActor>& Actor : GetScene()->FindObjects<CPlatformer3DActor>() )
	{
		Actor->Record3D( CommandList );
	}
}
