// SPDX-License-Identifier: MIT
#include "PixelEffectTile.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/PixelEffect/Canvas/PixelBackdrop.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

namespace
{
	// 画像を画面で何倍に描くか
	constexpr float32 Zoom = 2.0f;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 22;
	// 一度きりのエフェクトが終わってから、始め直すまで（秒）
	constexpr float32 PauseAfterFinish = 0.4f;
	// 背景・立ち絵の画像の名前に付ける通し番号（見本ごとに別の画像にする）
	int32 NextSerial = 0;

	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CPixelEffectTile::CPixelEffectTile( const FVector2& InOrigin, TUniquePtr<IPixelEffect> InEffect, EPixelBackdrop InBackdrop, const FPixelActor* InActor )
	: Origin( InOrigin )
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr || !InEffect.IsValid() )
	{
		return;
	}
	const FPixelEffectMeta Meta = InEffect->GetMeta();
	Effect = MakeUnique<CPixelEffectView>( *Assets, MoveTemp( InEffect ) );
	++NextSerial;
	Backdrop = Assets->CreateDynamicTexture( FString::Printf( "Samples/PixelEffects/Backdrop#%d", NextSerial ), NPixelBackdrop::MakeBackdrop( InBackdrop, Meta.Width, Meta.Height, NMath::RoundToInt( Meta.Anchor.y ) ) );
	if ( InActor != nullptr )
	{
		FPixelImage Figure = FPixelImage::FromPixels( InActor->Width, InActor->Height, InActor->Pixels.GetData() );
		Actor = Assets->CreateDynamicTexture( FString::Printf( "Samples/PixelEffects/Actor#%d", NextSerial ), Figure );
		Figure.FlipHorizontal();
		FlippedActor = Assets->CreateDynamicTexture( FString::Printf( "Samples/PixelEffects/FlippedActor#%d", NextSerial ), Figure );
	}
	Restart();
}

void CPixelEffectTile::Restart()
{
	Effect->Play( Seed, bFlip );
	RestartDelay = PauseAfterFinish;
}

void CPixelEffectTile::Tick( float32 DeltaTime )
{
	if ( !Effect.IsValid() )
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
}

void CPixelEffectTile::Draw( FDrawContext& CommandList )
{
	if ( !Effect.IsValid() )
	{
		return;
	}
	const FPixelEffectMeta& Meta = Effect->GetPlayer().GetMeta();
	const float32 Left = static_cast<float32>( Px( Origin.x ) );
	const float32 Top = static_cast<float32>( Px( Origin.y ) + TitleHeight );
	const float32 Right = Left + static_cast<float32>( Meta.Width ) * Zoom;
	const float32 Bottom = Top + static_cast<float32>( Meta.Height ) * Zoom;
	CommandList.TextF( Px( Origin.x ), Px( Origin.y ), FColor32::White, "%s%s", *Meta.Name, Meta.bLoop ? " (loop)" : "" );
	if ( Backdrop.IsValid() )
	{
		CommandList.ImageStretch( Left, Top, Right, Bottom, Backdrop );
	}

	// ゲームと同じ順: エフェクトの後ろ → キャラクター（足元を基準点に合わせる）→ エフェクトの前
	const FVector2 Anchor( Left + Meta.Anchor.x * Zoom, Top + Meta.Anchor.y * Zoom );
	Effect->DrawBack( CommandList, Anchor, Zoom );
	const TSharedPtr<CDynamicTexture>& Figure = bFlip ? FlippedActor : Actor;
	if ( Figure.IsValid() )
	{
		const float32 FigureWidth = static_cast<float32>( Figure->GetWidth() ) * Zoom;
		const float32 FigureHeight = static_cast<float32>( Figure->GetHeight() ) * Zoom;
		const float32 FigureLeft = Anchor.x - static_cast<float32>( Figure->GetWidth() / 2 ) * Zoom;
		CommandList.ImageStretch( FigureLeft, Anchor.y - FigureHeight, FigureLeft + FigureWidth, Anchor.y, Figure );
	}
	Effect->DrawFront( CommandList, Anchor, Zoom );
	CommandList.Box( Px( Left ), Px( Top ), Px( Right ), Px( Bottom ), FColor32( 90, 90, 90 ), false );
}
