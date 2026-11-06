// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/PixelEffect/Playback/IPixelEffect.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectFrame.h"

//=============================================================================
// CPixelEffectPlayer
//   エフェクト 1 つを再生し、後ろの層と前の層を、別々の描き先（FPixelEffectFrame）へ描き分けておく（CPU だけ。画像は作らない）。
//   ゲームのキャラクターを、後ろの層と前の層の間に挟んで描けるようにするためのもの。画面へ貼るのは CPixelEffectView。
//   描き先の大きさはエフェクトの GetMeta() の Width x Height で、基準点（足元・当たった点など）は Meta.Anchor の画素。
//
//   CPixelEffectPlayer Player( MakeUnique<CPixelHealEffect>() );
//   Player.Play( Seed );
//   // 毎フレーム
//   Player.Update( DeltaTime );
//   const FPixelImage Back = Player.GetLayerImage( EPixelLayer::Back );
//=============================================================================
class CPixelEffectPlayer
{
public:
	explicit CPixelEffectPlayer( TUniquePtr<IPixelEffect> InEffect );

	// エフェクトを持っているか（nullptr を渡したときは false で、ほかの関数は何もしない）
	FORCEINLINE bool IsValid() const
	{
		return Effect.IsValid();
	}

	// 初めから再生する（Palette が空なら、エフェクトの既定の色）。2 枚の描き先も、始めの絵に描き直す
	void Play( uint32 Seed = 1, bool bFlipHorizontal = false, const TArray<FColor32>& Palette = TArray<FColor32>() );

	// 時間を進め、2 枚の描き先を描き直す
	void Update( float32 DeltaTime );

	// 一度きりのエフェクトが終わったか（持っていないときも true）
	bool IsFinished() const;

	// エフェクトの情報（持っていないときは、既定の値）
	const FPixelEffectMeta& GetMeta() const;

	// Layer の層の絵（透明な地。光は、色と透明度に混ぜる）
	FPixelImage GetLayerImage( EPixelLayer Layer ) const;

	FORCEINLINE const FPixelEffectFrame& GetLayerFrame( EPixelLayer Layer ) const
	{
		return Layer == EPixelLayer::Back ? BackFrame : FrontFrame;
	}

private:
	// 2 枚の描き先を空にして、いまの絵を描く
	void Redraw();

	TUniquePtr<IPixelEffect> Effect;
	FPixelEffectFrame BackFrame;
	FPixelEffectFrame FrontFrame;
};
