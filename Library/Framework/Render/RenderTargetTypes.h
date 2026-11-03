// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 描画先（レンダーターゲット）にできる画像の、作り方と保存の形式
//   描画先は、描き込める画像。別の画像に描いておいて（ミニマップ・鏡・影響を受けない UI の部品など）、あとで、ふつうの画像として画面へ貼れる。
//   DxLib の型・定数は出さない（実装は Platform/DxLib）。
//=============================================================================

// 描画先の 1 チャンネルの形式
enum class ERenderTargetFormat : uint8
{
	Color8,  // 8 ビットの整数（0〜1 に収まる。ふつうの画像。既定）
	Float16, // 16 ビットの浮動小数点。1 を超える明るさや負の値も残る（HDR・加算で溜める光など）
	Float32, // 32 ビットの浮動小数点。精度が要る値（深度・位置など）
};

// 描画先の作り方
struct FRenderTargetDesc
{
	// 幅・高さの上限（ピクセル。DxLib / Direct3D 11 の上限）
	static constexpr int32 MaxSize = 16384;
	// マルチサンプル数の上限
	static constexpr int32 MaxMultiSamples = 8;

	// 幅・高さ（ピクセル。1〜MaxSize）
	int32 Width = 0;
	int32 Height = 0;
	// アルファチャンネル（透明度）を持つか。持てば、描いていない所を透明のまま残せる。持たなければ、不透明
	bool bAlpha = false;
	// 専用の Z バッファを持つか。3D を描くなら必要。2D だけなら false にすると、メモリを節約できる
	bool bDepth = true;
	// マルチサンプル（アンチエイリアス）の数。0 は使わない。2 / 4 / 8 など
	int32 MultiSamples = 0;
	// 1 チャンネルの形式。浮動小数点の描画先も、ふつうの画像として貼ったり、読んだり、保存したりできるが、そのときは 0〜1 に収められる
	// （1 を超える値を活かすのは、シェーダーで読むとき。シェーダーの ShaderTexture で読める）
	ERenderTargetFormat Format = ERenderTargetFormat::Color8;
	// チャンネルの数（1: R だけ、2: R と G、4: RGBA）。1 と 2 は浮動小数点のときだけ。シェーダーで読むと、ないチャンネルは 0、A は 1 になる
	int32 Channels = 4;

	// 大きさ・マルチサンプル数・形式とチャンネルの数が、作れる範囲か
	constexpr bool IsValid() const
	{
		const bool bFormatValid = Format == ERenderTargetFormat::Color8 || Format == ERenderTargetFormat::Float16 || Format == ERenderTargetFormat::Float32;
		const bool bChannelsValid = Channels == 4 || ( Format != ERenderTargetFormat::Color8 && ( Channels == 1 || Channels == 2 ) );
		return Width >= 1 && Width <= MaxSize && Height >= 1 && Height <= MaxSize && MultiSamples >= 0 && MultiSamples <= MaxMultiSamples && bFormatValid && bChannelsValid;
	}

	// 浮動小数点の描画先か
	constexpr bool IsFloat() const
	{
		return Format == ERenderTargetFormat::Float16 || Format == ERenderTargetFormat::Float32;
	}

	// 1 画素の色のバイト数（チャンネルの数 x 1 チャンネルのバイト数）
	constexpr int32 GetBytesPerPixel() const
	{
		return Channels * ( Format == ERenderTargetFormat::Float32 ? 4 : Format == ERenderTargetFormat::Float16 ? 2 : 1 );
	}
};

// 画像を保存するファイルの形式
enum class EImageFileFormat : uint8
{
	Png,    // 可逆圧縮。透明度（アルファ）も残る（既定）
	Bmp,    // 無圧縮。透明度は残らない
	Jpeg,   // 非可逆圧縮。透明度は残らない
};

// 画像の保存の設定
struct FImageSaveOptions
{
	EImageFileFormat Format = EImageFileFormat::Png;
	// Jpeg の品質（0〜100。範囲の外は収める）
	int32 JpegQuality = 80;
	// Png の圧縮の強さ（0〜9。-1 は既定。範囲の外は収める）
	int32 PngCompression = -1;
};
