// SPDX-License-Identifier: MIT
#include "PixelEffectTile.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/PixelEffect/PixelBackdrop.h"
#include "Library/Framework/PixelEffect/PixelPainter.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 画像を画面で何倍に描くか
	constexpr int32 Zoom = 2;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 22;
	// 一度きりのエフェクトが終わってから、始め直すまで（秒）
	constexpr float32 PauseAfterFinish = 0.4f;

	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CPixelEffectTile::CPixelEffectTile( const FVector2& InOrigin, TUniquePtr<IPixelEffect> InEffect, EPixelBackdrop InBackdrop, const FPixelActor* InActor )
	: Origin( InOrigin )
	, Effect( MoveTemp( InEffect ) )
	, bHasActor( InActor != nullptr )
{
	if ( !Effect.IsValid() )
	{
		return;
	}
	if ( InActor != nullptr )
	{
		Actor = *InActor;
	}
	const FPixelEffectMeta& Meta = Effect->GetMeta();
	Frame.Initialize( Meta.Width, Meta.Height );
	Backdrop = NPixelBackdrop::MakeBackdrop( InBackdrop, Meta.Width, Meta.Height, NMath::RoundToInt( Meta.Anchor.y ) );
	if ( CAssetManager* Assets = GetAppSubsystem<CAssetManager>() )
	{
		Texture = Assets->CreateDynamicTexture( FString::Printf( "Samples/PixelEffects/%s", *Meta.Name ), Backdrop );
	}
	Restart();
}

void CPixelEffectTile::Restart()
{
	FPixelPlacement Placement;
	Placement.Position = Effect->GetMeta().Anchor;
	Placement.bFlipHorizontal = bFlip;
	Placement.Seed = Seed;
	Effect->Start( Placement );
	RestartDelay = PauseAfterFinish;
}

void CPixelEffectTile::Tick( float32 DeltaTime )
{
	if ( !Effect.IsValid() || !Texture.IsValid() )
	{
		return;
	}
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::F ) )
	{
		bFlip = !bFlip;
		Restart();
	}
	if ( Input != nullptr && Input->IsKeyPressed( EKey::R ) )
	{
		++Seed;
		Restart();
	}

	Effect->Update( DeltaTime );
	if ( Effect->IsFinished() )
	{
		RestartDelay -= DeltaTime;
		if ( RestartDelay <= 0.0f )
		{
			++Seed;
			Restart();
		}
	}

	// 後ろの層 → 立ち絵 → 前の層の順に描いて、背景の上に合成する
	Frame.Clear();
	Effect->Draw( Frame, EPixelLayer::Back );
	if ( bHasActor )
	{
		CPixelPainter( Frame, Effect->GetMeta().Anchor, bFlip ).Actor( Actor, FVector2() );
	}
	Effect->Draw( Frame, EPixelLayer::Front );
	Texture->Update( Frame.CompositeToImage( Backdrop.GetData() ) );
}

void CPixelEffectTile::Draw( FDrawContext& CommandList )
{
	if ( !Effect.IsValid() )
	{
		return;
	}
	const FPixelEffectMeta& Meta = Effect->GetMeta();
	const int32 Left = Px( Origin.x );
	const int32 Top = Px( Origin.y ) + TitleHeight;
	CommandList.TextF( Left, Px( Origin.y ), FColor32::White, "%s  (%s%s)", *Meta.Name, *Meta.Category, Meta.bLoop ? ", loop" : "" );
	if ( !Texture.IsValid() )
	{
		return;
	}
	CommandList.ImageStretch( static_cast<float32>( Left ), static_cast<float32>( Top ), static_cast<float32>( Left + Meta.Width * Zoom ), static_cast<float32>( Top + Meta.Height * Zoom ), Texture );
	CommandList.Box( Left, Top, Left + Meta.Width * Zoom, Top + Meta.Height * Zoom, FColor32( 90, 90, 90 ), false );
}
