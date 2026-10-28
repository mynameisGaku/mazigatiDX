// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Asset/Texture.h"

//=============================================================================
// CDynamicTexture
//   CPU 側の画素（FPixelImage）から作った画像。CTexture でもあるので、IImage として、ふつうの画像と同じように描ける。
//   中身（画素）は、あとから Update で替えられる（ハンドルは変わらないので、貼ってある場所は、そのまま新しい絵になる）。
//   CAssetManager::CreateDynamicTexture で作る。最後の参照が消えると、自動で解放される（CTexture と同じ）。
//
//   FPixelImage Canvas( 128, 128, FColor32( 0, 0, 0, 255 ) );
//   TSharedPtr<CDynamicTexture> Texture = Assets->CreateDynamicTexture( "Canvas", Canvas );
//   // 毎フレーム: 画素を書き換えて、反映する
//   Canvas.Set( X, Y, FColor32( 255, 255, 255 ) );
//   Texture->Update( Canvas );
//   CommandList.Image( 10.0f, 10.0f, Texture );
//=============================================================================
class CDynamicTexture : public CTexture
{
public:
	CDynamicTexture( const FString& InName, int32 InHandle, int32 InWidth, int32 InHeight );

	// 中身を、画素に替える。画像と同じ大きさ（幅・高さ）でなければ、何もせず false。メインスレッドだけ。
	// 全体を作り直すので、毎フレームでも、小さな画像（数百ピクセル四方まで）に向く
	bool Update( const FPixelImage& Image );

	FString Describe() const override;
};
