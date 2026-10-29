// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/PixelImage.h"

//=============================================================================
// NFilterImages
//   サンプルで使う画像を、CPU 側で作る（画像のファイルを置かなくても動くようにするため）。
//=============================================================================
namespace NFilterImages
{
	// フィルターを掛ける元の画像の大きさ（ピクセル。正方形）
	constexpr int32 SourceSize = 96;

	// 元の画像。左上は赤、右上は緑、左下は青、右下は黒から白へのグラデーション（境目と、なめらかな部分の、両方が見える）
	FPixelImage MakeSource();

	// 混ぜる画像（BlendImages）。上が橙、下が青のグラデーション（不透明）
	FPixelImage MakeOverlay();

	// グラデーションマップ（GradientMap）。明るさを、黒・紫・赤・黄・白の順に色へ置き換える（5 x 1）
	FPixelImage MakeHeatMap();

	// スポットライトの光（中心が不透明で、縁へ向かって、なめらかに透明になる白い円。Size x Size）
	FPixelImage MakeGlow( int32 Size );
}
