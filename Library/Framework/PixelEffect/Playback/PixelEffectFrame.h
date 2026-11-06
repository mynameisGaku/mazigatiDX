// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/PixelEffect/Canvas/IPixelSurface.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Math/Color.h"

//=============================================================================
// FPixelEffectFrame
//   ピクセルアート演出の描き先となる 2 枚の画素バッファ（IPixelSurface の実装）。
//   - ColorBuffer: 本体の色（不透明または半透明の画素。a == 0 は空）
//   - LightBuffer: 加算される発光成分（RGB に蓄積）
//   CompositeToImage / CompositeOverColor で 1 枚の FPixelImage に合成し、
//   CDynamicTexture::Update で GPU 側の画像へ載せる。
//=============================================================================
class FPixelEffectFrame final : public IPixelSurface
{
public:
	FPixelEffectFrame() = default;
	FPixelEffectFrame( int32 InWidth, int32 InHeight );

	// 大きさを設定し直して空にする
	void Initialize( int32 InWidth, int32 InHeight );

	// 色バッファと光バッファをどちらも空（0）に戻す
	void Clear();

	FORCEINLINE bool IsValid() const
	{
		return Width > 0 && Height > 0 && ColorBuffer.Num() == Width * Height && LightBuffer.Num() == Width * Height;
	}

	int32 GetWidth() const override
	{
		return Width;
	}

	int32 GetHeight() const override
	{
		return Height;
	}

	void SetPixel( int32 X, int32 Y, const FColor32& Color ) override;
	void BlendPixel( int32 X, int32 Y, const FColor32& Color, float32 Alpha ) override;
	void AddLight( int32 X, int32 Y, const FColor32& Color, float32 Strength ) override;
	FColor32 GetPixel( int32 X, int32 Y ) const override;

	FORCEINLINE TArray<FColor32>& GetColorBuffer()
	{
		return ColorBuffer;
	}

	FORCEINLINE const TArray<FColor32>& GetColorBuffer() const
	{
		return ColorBuffer;
	}

	FORCEINLINE TArray<FColor32>& GetLightBuffer()
	{
		return LightBuffer;
	}

	FORCEINLINE const TArray<FColor32>& GetLightBuffer() const
	{
		return LightBuffer;
	}

	// 背景なし（透明地、または BackgroundPixels が非 nullptr なら Width x Height 個の背景）の上に
	// ColorBuffer と LightBuffer（強さ LightStrength）を合成して FPixelImage を返す
	FPixelImage CompositeToImage( const FColor32* BackgroundPixels = nullptr, float LightStrength = 1.0f ) const;

	// 単色の背景 BackgroundColor の上に合成して不透明な FPixelImage を返す
	FPixelImage CompositeOverColor( const FColor32& BackgroundColor, float LightStrength = 1.0f ) const;

private:
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> ColorBuffer;
	TArray<FColor32> LightBuffer;
};
