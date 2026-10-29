// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/DynamicTexture.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CFilterGallery
//   1 枚の元の画像に、いろいろなフィルター（ApplyFilter）を掛けた結果と、別の画像との混ぜ方（BlendImages）を、並べて見せる。
//   結果は、フィルターごとの描画先（CRenderTarget）に出して、ふつうの画像として、画面へ貼る。ぼかしの強さ・色相は、動かして見せる。
//   Draw( FDrawContext& CommandList ) は、何をするかを記録するだけ（DxLib も実行先も知らない）。記録した順に、フィルター → 貼る、と実行される。
//=============================================================================
class CFilterGallery : public CGameObject
{
public:
	// 並べる数（0 番は、元の画像そのまま。1〜7 はフィルター、8〜11 は混ぜ方）
	static constexpr int32 NumTiles = 12;
	// 1 列に並べる数、タイルの大きさ（元の画像と同じ）、並べる間隔（横と縦。縦は、見出しの分を含む）
	static constexpr int32 Columns = 4;
	static constexpr int32 TileSize = 96;
	static constexpr int32 PitchX = 112;
	static constexpr int32 PitchY = 128;

	// InOrigin は、見出しの左上（画面の座標。ピクセル）。InSource は元の画像、InOverlay は混ぜる画像、InHeatMap はグラデーションマップ。
	// InTiles は、結果を出す描画先（NumTiles 個。0 番は使わない。このオブジェクトが持つ）
	CFilterGallery( const FVector2& InOrigin, const TSharedPtr<CDynamicTexture>& InSource, const TSharedPtr<CDynamicTexture>& InOverlay, const TSharedPtr<CDynamicTexture>& InHeatMap,
					const TArray<TSharedPtr<CRenderTarget>>& InTiles );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 見出しの左上（画面の座標。ピクセル）
	FVector2 Origin;

	TSharedPtr<CDynamicTexture> Source;
	TSharedPtr<CDynamicTexture> Overlay;
	TSharedPtr<CDynamicTexture> HeatMap;
	TArray<TSharedPtr<CRenderTarget>> Tiles;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
