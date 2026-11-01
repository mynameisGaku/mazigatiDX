// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/IPixelSurface.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Math/Color.h"

//=============================================================================
// FPixelImageSurface
//   既存の CPU 画像（FPixelImage）へ直接 CPixelPainter で描き込むための IPixelSurface アダプター。
//   FPixelEffectFrame を使わずに 1 枚の FPixelImage へドット絵・文字・粒子を直接描きたいときに使う。
//=============================================================================
class FPixelImageSurface final : public IPixelSurface
{
public:
	explicit FPixelImageSurface( FPixelImage& InImage );

	int32 GetWidth() const override;
	int32 GetHeight() const override;

	void SetPixel( int32 X, int32 Y, const FColor32& Color ) override;
	void BlendPixel( int32 X, int32 Y, const FColor32& Color, float32 Alpha ) override;
	void AddLight( int32 X, int32 Y, const FColor32& Color, float32 Strength ) override;
	FColor32 GetPixel( int32 X, int32 Y ) const override;

private:
	FPixelImage* Image = nullptr;
};
