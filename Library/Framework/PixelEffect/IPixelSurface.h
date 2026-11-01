// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

//=============================================================================
// IPixelSurface
//   ピクセルアートを描く先の、いちばん小さい窓口。大きさと、1 画素の読み書きだけ。
//   図形・文字・光は、CPixelPainter が 1 画素ずつに分けて渡すので、描き先はこの 6 つを用意すればよい。
//   実装: FPixelEffectFrame（色と光の 2 枚に溜めて合成する）、FPixelImageSurface（CPU の画像 FPixelImage へ直接書く）、検査の偽物。
//   座標は画素（左上が (0, 0)）。範囲の外への書き込みは、何もしない（呼ぶ側は確かめなくてよい）。
//=============================================================================
class IPixelSurface
{
public:
	virtual ~IPixelSurface() = default;

	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;

	// 不透明な色で塗る
	virtual void SetPixel( int32 X, int32 Y, const FColor32& Color ) = 0;

	// 色を Alpha（0〜1）の濃さで混ぜる
	virtual void BlendPixel( int32 X, int32 Y, const FColor32& Color, float32 Alpha ) = 0;

	// 光を Strength（0 以上）の強さで足す
	virtual void AddLight( int32 X, int32 Y, const FColor32& Color, float32 Strength ) = 0;

	// いまの色（範囲の外は透明な黒）
	virtual FColor32 GetPixel( int32 X, int32 Y ) const = 0;
};
