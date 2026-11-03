// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

// 別の描画先へ描き直して画像を取得する際の設定値。
struct FOffscreenCaptureDesc
{
	// 描画先の幅を画素で指定する（有効範囲1〜16384）。
	int32 Width = 0;
	// 描画先の高さを画素で指定する（有効範囲1〜16384）。
	int32 Height = 0;
	// 2D描画の拡大率。画面サイズに掛けると描画先サイズになる。
	float32 Scale = 1.0f;
	// 描き始める前に描画先を塗る色。
	FColor32 ClearColor = FColor32( 0, 0, 0, 255 );
};
