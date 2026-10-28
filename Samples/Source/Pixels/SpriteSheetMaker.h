// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/PixelImage.h"

//=============================================================================
// NSpriteSheetMaker
//   サンプルで使う、スプライトシートの画像を、CPU 側で作る（画像のファイルを置かなくても動くようにするため）。
//   4 x 2 のコマ（1 コマ 32 x 32）。コマごとに背景の色が違い、中に、大きさが段々変わる円が、半透明の縁つきで描いてある。
//=============================================================================
namespace NSpriteSheetMaker
{
	// コマの数（横 x 縦）と、1 コマの大きさ（ピクセル）
	constexpr int32 Columns = 4;
	constexpr int32 Rows = 2;
	constexpr int32 CellSize = 32;

	// シート全体（Columns x Cell 幅、Rows x Cell 高さ）
	FPixelImage Make();
}
