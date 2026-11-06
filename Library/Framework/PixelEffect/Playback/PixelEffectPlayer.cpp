// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Playback/PixelEffectPlayer.h"

namespace
{
	const FPixelEffectMeta EmptyMeta;
}

CPixelEffectPlayer::CPixelEffectPlayer( TUniquePtr<IPixelEffect> InEffect )
	: Effect( MoveTemp( InEffect ) )
{
	if ( Effect.IsValid() )
	{
		const FPixelEffectMeta& Meta = Effect->GetMeta();
		BackFrame.Initialize( Meta.Width, Meta.Height );
		FrontFrame.Initialize( Meta.Width, Meta.Height );
	}
}

void CPixelEffectPlayer::Play( uint32 Seed, bool bFlipHorizontal, const TArray<FColor32>& Palette )
{
	if ( !Effect.IsValid() )
	{
		return;
	}
	FPixelPlacement Placement;
	Placement.Position = Effect->GetMeta().Anchor;
	Placement.Seed = Seed;
	Placement.bFlipHorizontal = bFlipHorizontal;
	Placement.Palette = Palette;
	Effect->Start( Placement );
	Redraw();
}

void CPixelEffectPlayer::Update( float32 DeltaTime )
{
	if ( !Effect.IsValid() )
	{
		return;
	}
	Effect->Update( DeltaTime );
	Redraw();
}

bool CPixelEffectPlayer::IsFinished() const
{
	return !Effect.IsValid() || Effect->IsFinished();
}

const FPixelEffectMeta& CPixelEffectPlayer::GetMeta() const
{
	return Effect.IsValid() ? Effect->GetMeta() : EmptyMeta;
}

FPixelImage CPixelEffectPlayer::GetLayerImage( EPixelLayer Layer ) const
{
	return GetLayerFrame( Layer ).CompositeToImage();
}

void CPixelEffectPlayer::Redraw()
{
	BackFrame.Clear();
	FrontFrame.Clear();
	Effect->Draw( BackFrame, EPixelLayer::Back );
	Effect->Draw( FrontFrame, EPixelLayer::Front );
}
