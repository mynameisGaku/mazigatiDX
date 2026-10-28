// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 描画先（レンダーターゲット）にできる画像の、作り方と保存の形式
//   描画先は、描き込める画像。別の画像に描いておいて（ミニマップ・鏡・影響を受けない UI の部品など）、あとで、ふつうの画像として画面へ貼れる。
//   DxLib の型・定数は出さない（実装は Platform/DxLib）。
//=============================================================================

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

	// 大きさとマルチサンプル数が、作れる範囲か
	constexpr bool IsValid() const
	{
		return Width >= 1 && Width <= MaxSize && Height >= 1 && Height <= MaxSize && MultiSamples >= 0 && MultiSamples <= MaxMultiSamples;
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
