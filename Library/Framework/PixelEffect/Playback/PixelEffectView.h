// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectPlayer.h"
#include "Library/Math/Vector2.h"

class CAssetManagerSubsystem;
class FDrawContext;

//=============================================================================
// CPixelEffectView
//   エフェクトを画面に貼る役。CPixelEffectPlayer の後ろの層と前の層を、それぞれ画像（CDynamicTexture）にして、
//   画面の好きな所へ、好きな倍率（最近傍で拡大）で描く。ゲームのキャラクターは、DrawBack と DrawFront の間に、ふつうに描けばよい。
//   エフェクトは、キャラクターを描かない（オーラ・残像など形が要るものは、作るときにシルエットだけを受け取る）。
//
//   CPixelEffectView Heal( *Assets, MakeUnique<CPixelHealEffect>() );
//   Heal.Play();
//   // 毎フレーム: Tick で
//   Heal.Update( DeltaTime );
//   // Draw( FDrawContext& CommandList ) で（Feet は、キャラクターの足元の画面の座標）
//   Heal.DrawBack( CommandList, Feet );
//   CommandList.Image( ..., HeroTexture );   // キャラクター
//   Heal.DrawFront( CommandList, Feet );
//=============================================================================
class CPixelEffectView
{
public:
	// 画像は Assets で作る（名前は、ほかの CPixelEffectView と重ならないよう、通し番号を付ける）
	CPixelEffectView( CAssetManagerSubsystem& Assets, TUniquePtr<IPixelEffect> InEffect );

	// 初めから再生する（CPixelEffectPlayer::Play と同じ）
	void Play( uint32 Seed = 1, bool bFlipHorizontal = false, const TArray<FColor32>& Palette = TArray<FColor32>() );

	// 時間を進め、2 枚の画像の中身を替える
	void Update( float32 DeltaTime );

	FORCEINLINE bool IsFinished() const
	{
		return Player.IsFinished();
	}

	FORCEINLINE const CPixelEffectPlayer& GetPlayer() const
	{
		return Player;
	}

	// 後ろの層・前の層を描く。Anchor は、エフェクトの基準点を置く画面の座標。Zoom は拡大の倍率（1 以上の整数がドット絵らしい）
	void DrawBack( FDrawContext& CommandList, const FVector2& Anchor, float32 Zoom = 2.0f ) const;
	void DrawFront( FDrawContext& CommandList, const FVector2& Anchor, float32 Zoom = 2.0f ) const;

	// 層の画像（作れなかったときは nullptr）
	FORCEINLINE const TSharedPtr<CDynamicTexture>& GetLayerTexture( EPixelLayer Layer ) const
	{
		return Layer == EPixelLayer::Back ? BackTexture : FrontTexture;
	}

private:
	CPixelEffectPlayer Player;
	TSharedPtr<CDynamicTexture> BackTexture;
	TSharedPtr<CDynamicTexture> FrontTexture;
};
