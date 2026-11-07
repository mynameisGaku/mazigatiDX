#include "EffectsView.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"
#include "Source/Common/SoftLighting.h"
#include "Source/Effects/EffectsState.h"

namespace
{
	const FColor32 SkyTop( 8, 10, 24 );
	const FColor32 SkyBottom( 28, 30, 58 );
}

CEffectsView::CEffectsView( const TSharedPtr<FEffectsState>& InState )
	: State( InState )
{}

void CEffectsView::DrawBackground( FDrawContext& CommandList ) const
{
	// 夜空（上が暗く、下が少し明るい。帯を重ねる）
	const int32 Bands = 8;
	for ( int32 Band = 0; Band < Bands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( Bands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.r ), static_cast<float32>( SkyBottom.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.g ), static_cast<float32>( SkyBottom.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.b ), static_cast<float32>( SkyBottom.b ), T ) ) );
		CommandList.Box( 0, FEffectsState::ScreenHeight * Band / Bands, FEffectsState::ScreenWidth, FEffectsState::ScreenHeight * ( Band + 1 ) / Bands, Color );
	}
}

void CEffectsView::RecordStage( FDrawContext& CommandList ) const
{
	// 床: 暗い板に、薄い格子（広く）
	CommandList.Cube3D( FVector3( -40.0f, -0.4f, -12.0f ), FVector3( 40.0f, 0.0f, 20.0f ), FColor32( 36, 40, 52 ) );
	const FColor32 GridColor( 54, 60, 78 );
	for ( int32 X = -40; X <= 40; X += 2 )
	{
		CommandList.Line3D( FVector3( static_cast<float32>( X ), 0.01f, -12.0f ), FVector3( static_cast<float32>( X ), 0.01f, 20.0f ), GridColor );
	}
	for ( int32 Z = -12; Z <= 20; Z += 2 )
	{
		CommandList.Line3D( FVector3( -40.0f, 0.01f, static_cast<float32>( Z ) ), FVector3( 40.0f, 0.01f, static_cast<float32>( Z ) ), GridColor );
	}
	// 台座（選んでいるものは、明るい縁）
	for ( int32 Index = 0; Index < State->Entries.Num(); ++Index )
	{
		const FEffectsEntry& Entry = State->Entries[Index];
		const bool bSelected = Index == State->Selected;
		const FVector3 P = Entry.Pedestal;
		CommandList.Cube3D( P + FVector3( -1.2f, 0.0f, -1.2f ), P + FVector3( 1.2f, 0.45f, 1.2f ), bSelected ? FColor32( 96, 108, 140 ) : FColor32( 64, 70, 90 ) );
		CommandList.Cube3D( P + FVector3( -1.0f, 0.45f, -1.0f ), P + FVector3( 1.0f, 0.55f, 1.0f ), bSelected ? FColor32( 150, 200, 255 ) : FColor32( 90, 98, 124 ) );
	}
}

void CEffectsView::DrawLabels( FDrawContext& CommandList ) const
{
	for ( int32 Index = 0; Index < State->Entries.Num(); ++Index )
	{
		const FEffectsEntry& Entry = State->Entries[Index];
		FVector2 Pixel;
		if ( State->Camera.WorldToScreen( State->GetViewport(), Entry.Pedestal + FVector3( 0.0f, -0.1f, -1.3f ), Pixel ) )
		{
			const bool bSelected = Index == State->Selected;
			const int32 Width = static_cast<int32>( Entry.Title.Len() ) * 6;
			CommandList.Text( static_cast<int32>( Pixel.x ) - Width, static_cast<int32>( Pixel.y ), *Entry.Title, bSelected ? FColor32( 255, 230, 120 ) : FColor32( 170, 176, 196 ) );
		}
	}
	// 選んでいるものの説明と、粒子の数
	const FEffectsEntry& Selected = State->Entries[State->Selected];
	CommandList.TextF( 16, 640, FColor32::White, "%s   (%s)", *Selected.Title, *Selected.Path );
	CommandList.Text( 16, 664, *Selected.Description, FColor32( 190, 196, 216 ) );
	CommandList.TextF( 16, 688, FColor32( 150, 156, 176 ), "Scale x%.2f   Particles: %d   %s", Selected.Scale, Selected.Playback.IsValid() ? Selected.Playback->GetNumParticles() : 0, Selected.Playback.IsValid() ? "" : "(could not be loaded)" );
}

void CEffectsView::Draw( FDrawContext& CommandList )
{
	DrawBackground( CommandList );
	{
		// このカメラで見える 3D を描く（使えない値のカメラのときは、何も描かない）
		FCameraScope Scope( CommandList, State->Camera );
		if ( Scope.IsPushed() )
		{
			CommandList.DefaultLight( false );
			FLightScope Moon( CommandList, FLight::MakeDirectional( FVector3( -0.3f, -0.8f, 0.5f ), FColor( 0.55f, 0.6f, 0.8f ) ) );
			FSoftLightingScope Soft( CommandList );
			RecordStage( CommandList );
		}
	}
	// エフェクトは、ライトを使わず（自分で光る）、奥行きは見るが書かずに（半透明の粒子が、後ろの粒子を隠さないように）重ねて描く
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
			// 粒子は自分で光る（ライトを切らないと、ライトのない夜の舞台では、真っ黒になる）
			CommandList.UseLighting( false );
			for ( const FEffectsEntry& Entry : State->Entries )
			{
				if ( Entry.Playback.IsValid() )
				{
					Entry.Playback->Record( CommandList );
				}
			}
			CommandList.UseLighting( true );
		}
	}
	DrawLabels( CommandList );
}
