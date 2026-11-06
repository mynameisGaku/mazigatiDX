// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Data/SpriteSheet.h"
#include "Library/Framework/Asset/Loading/AssetCache.h"
#include "Library/Framework/Asset/Loading/AssetLoadContext.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Asset/Texture/Texture.h"

//=============================================================================
// CTextureLoader
//   画像（テクスチャ）を読み込む・作る人。ファイルから読み込む、ファイルの中身（メモリ）から作る、描画先を作る、CPU 側の画像（FPixelImage）から作る、
//   元の画像の一部を共有する派生の画像を作る、格子で分ける、までを受け持つ。読み込んだものは、同じ指定なら共有する。
//   CPU 側の画像そのものを読み書きするのは CPixelImageCodec。使うときは、CAssetSubsystem::LoadTexture などから呼ばれる。
//=============================================================================
class CTextureLoader
{
public:
	explicit CTextureLoader( CAssetLoadContext& InContext );

	// ファイルから。同じファイルは共有する。bAsync なら、読み込みの完了を待たずに返す（IsReady が true になるまで使わない）。読めなければ nullptr
	TSharedPtr<CTexture> Load( const FString& Path, bool bAsync );

	// ファイルの中身（BMP / PNG など）から作る。Name が同じなら共有する
	TSharedPtr<CTexture> CreateFromMemory( const FString& Name, const void* FileImage, usize Size );

	// 描画先（描き込める画像）を作る。Name が同じで、設定も同じものがまだ使われていれば共有する（設定が違えば、警告を出して新しく作る）。作れなければ nullptr
	TSharedPtr<CRenderTarget> CreateRenderTarget( const FString& Name, const FRenderTargetDesc& Desc );

	// CPU 側の画像（FPixelImage）から作る。Name が同じで、まだ使われているものがあれば共有する（中身は替えない）。作れなければ nullptr
	TSharedPtr<CDynamicTexture> CreateDynamic( const FString& Name, const FPixelImage& Image );

	// 元画像の矩形を親と共有する派生の画像を返す。同じ親と矩形は再利用する。矩形が画像の外・元画像が使えないなら nullptr
	TSharedPtr<CTexture> CreateRegion( const TSharedPtr<CTexture>& Source, int32 X, int32 Y, int32 Width, int32 Height );

	// 画像を、格子（余白・隙間・コマの大きさ・並べ方）で分けて、1 コマずつの派生の画像の並びにする。画像の外へはみ出すコマは nullptr（番号は、格子のコマの番号のまま）
	TArray<TSharedPtr<CTexture>> Split( const TSharedPtr<CTexture>& Source, const FSpriteSheetGrid& Grid );

	// 覚えている画像（生きているもの）の情報を足す / 手放されたものの記録を片付ける / 全部忘れる
	void CollectInfos( TArray<FAssetInfo>& Out ) const;
	void Prune();
	void Clear();

private:
	CAssetLoadContext& Context;
	TAssetCache<CTexture> Cache;
};
