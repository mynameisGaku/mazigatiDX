// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CViewShowcase
//   描画の状態のうち、見え方にかかわるもの（クリップ範囲・描画モード・2D の変換）を見せる。
//   左: クリップ。動く窓の中だけに、縞模様が描かれる（FClipScope。窓の中の小さな窓は、入れ子）。
//   中: 描画モード。同じ画像の一部分を大きく拡大して、Nearest（ドットのまま）と Bilinear（なめらか）を並べる。
//   右: 2D の変換。拡大・回転・移動する「カメラ」を、行列 1 つで作る。窓の外へ出ないよう、クリップと組み合わせている。
//   Draw( FDrawContext& CommandList ) は、画像を IImage として渡して、何を描くかを記録するだけ（DxLib も実行先も知らない）。
//=============================================================================
class CViewShowcase : public CGameObject
{
public:
	// InOrigin は、左上の見出しの位置（画面の座標。ピクセル）。絵は、そこから下と右へ並ぶ。InTexture は、描く画像（このオブジェクトが持つ）
	CViewShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InTexture );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 絵 1 つぶんの描き方（Left / Top は、その絵の枠の左上）
	void DrawClip( FDrawContext& CommandList, int32 Left, int32 Top ) const;
	void DrawFilter( FDrawContext& CommandList, int32 Left, int32 Top ) const;
	void DrawCamera( FDrawContext& CommandList, int32 Left, int32 Top ) const;

	// カメラに映す世界（原点が中心）。世界の座標のまま描く（Transform2D が、画面の座標にする）
	void DrawWorld( FDrawContext& CommandList ) const;

	// 左上の見出しの位置（画面の座標。ピクセル）
	FVector2 Origin;

	// 描く画像
	TSharedPtr<CTexture> Texture;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
