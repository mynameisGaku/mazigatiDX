// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/DynamicTexture.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CSpotlightView
//   マスクの使い方を、動かしながら見せる。暗い部屋を、動くスポットライトが照らす: 絵は、スポットライトの形の所だけに、段階的に（縁はなめらかに）見える。
//   1. マスクの画像（アルファを持つ描画先）に、光の画像（縁が透明になる白い円）を、動く位置に描く
//   2. 画面に出す描画先の中で、FMaskScope（マスクの画像）の中に、絵を描く
//   3. その描画先を、ふつうの画像として、画面へ貼る
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ（DxLib も実行先も知らない）。マスクの画像は、描画先の左上を合わせて使う。
//=============================================================================
class CSpotlightView : public CGameObject
{
public:
	// 表示する描画先（マスクを使う描画の先）と、マスクの画像の大きさ（ピクセル。正方形）
	static constexpr int32 ViewSize = 192;
	// 光の画像の大きさ（ピクセル。正方形）
	static constexpr int32 GlowSize = 128;

	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InScene は、照らされる絵、InGlow は、光の画像。
	// InMask は、アルファを持つ描画先、InView は、アルファを持たない描画先（どちらも ViewSize の正方形。このオブジェクトが持つ）
	CSpotlightView( const FVector2& InOrigin, const TSharedPtr<CDynamicTexture>& InScene, const TSharedPtr<CDynamicTexture>& InGlow, const TSharedPtr<CRenderTarget>& InMask, const TSharedPtr<CRenderTarget>& InView );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	TSharedPtr<CDynamicTexture> Scene;
	TSharedPtr<CDynamicTexture> Glow;
	TSharedPtr<CRenderTarget> Mask;
	TSharedPtr<CRenderTarget> View;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
