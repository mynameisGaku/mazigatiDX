// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"

struct FImageSaveOptions;

//=============================================================================
// CPixelImageCodec
//   CPU 側の画像（FPixelImage）を、画像ファイル（PNG・BMP・JPEG など）から読み出す・展開する・保存する人。
//   バックエンドの画像の読み書きを使うが、GPU の画像（CTexture）は作らない（それは CTextureLoader）。キャッシュはしない。
//   使うときは、CAssetSubsystem::LoadPixelImage / DecodePixelImage / SavePixelImage から呼ばれる。
//=============================================================================
class CPixelImageCodec
{
public:
	explicit CPixelImageCodec( CAssetLoadContext& InContext );

	// 画像ファイル（パスはアセットのパス）を、CPU 側の画像に読み込む。読めなければ、ログを出して空の画像
	FPixelImage Load( const FString& Path ) const;

	// ファイルの中身（PNG・BMP・JPEG など）を、CPU 側の画像に展開する。壊れたデータなら、ログを出して空の画像
	FPixelImage Decode( const void* FileImage, usize Size ) const;

	// CPU 側の画像をファイルに保存する（形式は Options.Format。相対パスはルートフォルダーの下で、途中のフォルダーは作る）。空の画像・保存できなければ false
	bool Save( const FPixelImage& Image, const FString& Path, const FImageSaveOptions& Options ) const;

private:
	CAssetLoadContext& Context;
};
