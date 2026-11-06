// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"

//=============================================================================
// ITextureBackend
//   画像（テクスチャ）・描画先（レンダーターゲット）・CPU 側の画素との行き来を行う窓口。画像の読み込みを使う側（CTextureLoader / CPixelImageCodec）が知る
//   すべての関数はメインスレッドだけから呼ばれる（バックエンドがスレッドセーフとは限らない）。実装は IAssetBackend を通して持つ
//=============================================================================
class ITextureBackend
{
public:
	virtual ~ITextureBackend() = default;

	//-------------------------------------------------------------------------
	// 画像
	//-------------------------------------------------------------------------
	// 読み込む。bAsync なら、読み込みの完了を待たずにハンドルを返す（完了は IsLoading で調べる）。失敗したら InvalidHandle
	virtual int32 LoadTexture( const FString& Path, bool bAsync ) = 0;
	// ファイルの中身（BMP / PNG など）から作る
	virtual int32 CreateTextureFromMemory( const void* FileImage, usize Size ) = 0;
	// 元画像の矩形を共有する派生画像を作る。非対応の実装は InvalidHandle を返す。
	virtual int32 CreateTextureRegion( int32 /*Source*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/ ) { return NAsset::InvalidHandle; }
	// 3Dポリゴンへ渡す、派生領域の半texel内側UVを返す。
	virtual bool GetTextureUvBounds( int32 /*Texture*/, float32 ( & /*OutUV*/ )[4] ) { return false; }
	// 画像の大きさ。読み込み中・無効なハンドルなら false
	virtual bool GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight ) = 0;
	virtual void DeleteTexture( int32 Texture ) = 0;

	//-------------------------------------------------------------------------
	// 描画先（レンダーターゲット）。描き込める画像。解放は DeleteTexture、大きさは GetTextureSize
	//-------------------------------------------------------------------------
	// 作る。Desc が作れない値（IsValid が false）・作れなかったときは InvalidHandle。非対応の実装も InvalidHandle
	virtual int32 CreateRenderTarget( const FRenderTargetDesc& /*Desc*/ ) { return NAsset::InvalidHandle; }
	// 描画先の矩形 (X, Y, Width, Height) の画素を、左上から右へ、上から下へ、OutPixels（Width x Height 個）に読む。
	// 呼ぶ側が、矩形を描画先の中に収めてある。透明度を持たない描画先の A は 255。描画先でない・読めなかったら false
	virtual bool ReadRenderTargetPixels( int32 /*Target*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// 描画先の矩形 (X, Y, Width, Height) を、Path のファイルに保存する。呼ぶ側が、矩形を描画先の中に収めてある。失敗したら false
	virtual bool SaveRenderTarget( int32 /*Target*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/, const FString& /*Path*/, const FImageSaveOptions& /*Options*/ ) { return false; }

	//-------------------------------------------------------------------------
	// CPU 側の画像（画素）との行き来。画素は FColor32（透明度は、乗算されていない普通のアルファ）で、左上から右へ、上から下へ並ぶ
	//-------------------------------------------------------------------------
	// 画素（Width x Height 個）から画像を作る。大きさが 1〜16384 の外・Pixels が nullptr・作れなかったときは InvalidHandle。解放は DeleteTexture
	virtual int32 CreateTextureFromPixels( int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/ ) { return NAsset::InvalidHandle; }
	// CreateTextureFromPixels で作った画像の中身を、画素に替える。画像と同じ大きさでなければ false。ハンドルは変わらない
	virtual bool UpdateTexturePixels( int32 /*Texture*/, int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/ ) { return false; }
	// ファイルの中身（PNG・BMP・JPEG など）を、画素に展開する。透明度を持たない形式は、不透明（A = 255）。壊れたデータ・読めない形式なら false
	virtual bool DecodeImage( const void* /*FileImage*/, usize /*Size*/, int32& /*OutWidth*/, int32& /*OutHeight*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// ファイル（フルパス）を、画素に展開する。読めなければ false
	virtual bool DecodeImageFile( const FString& /*Path*/, int32& /*OutWidth*/, int32& /*OutHeight*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// 画素（Width x Height 個）を、Path のファイルに保存する。呼ぶ側が、大きさを 1 以上にしてある。失敗したら false
	virtual bool SavePixels( int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/, const FString& /*Path*/, const FImageSaveOptions& /*Options*/ ) { return false; }
};
