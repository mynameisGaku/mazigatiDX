// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/RenderTargetTypes.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Math/Color.h"

//=============================================================================
// CRenderTarget
//   描画先（レンダーターゲット）。描き込める画像で、描いたあとは、ふつうの画像（CTexture / IImage）として、画面や別の描画先へ貼れる。
//   CAssetManager::CreateRenderTarget で作る。最後の参照が消えると、自動で解放される（CTexture と同じ）。
//
//   FRenderTargetDesc Desc;
//   Desc.Width = 256;
//   Desc.Height = 256;
//   Desc.bAlpha = true;                                                  // 描いていない所を透明のまま残す
//   TSharedPtr<CRenderTarget> Minimap = Assets->CreateRenderTarget( "Minimap", Desc );
//
//   // 描く: オブジェクトの Draw( FDrawContext& CommandList ) の中で、スコープの中だけ、描く先を替える
//   {
//       FTargetScope Scope( CommandList, Minimap );
//       CommandList.Clear( FColor32( 0, 0, 0, 0 ) );                     // 透明で消す
//       CommandList.Circle( 128, 128, 40, FColor32::Red );
//   }
//   CommandList.Image( 20.0f, 20.0f, Minimap );                         // ふつうの画像として画面へ貼る
//
//   // 読む・保存する（描いたあと。メインスレッドだけ。描き込みは、オブジェクトの Draw が実行されたあとに済んでいる）
//   FColor32 Color;
//   Minimap->ReadPixel( 128, 128, Color );
//   Minimap->Save( "Saved/Minimap.png" );
//=============================================================================
class CRenderTarget : public CTexture
{
public:
	CRenderTarget( const FString& InName, int32 InHandle, const FRenderTargetDesc& InDesc );

	// 作ったときの設定
	FORCEINLINE const FRenderTargetDesc& GetDesc() const { return Desc; }

	// 矩形 (X, Y, Width, Height) の画素を、左上から右へ、上から下へ、OutPixels（Width x Height 個）に読む。
	// 矩形が描画先の外にはみ出していたり、大きさが 0 以下なら、何もせず false。透明度を持たない描画先の A は 255。メインスレッドだけ
	bool ReadPixels( int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels ) const;
	// 1 画素を読む。(X, Y) が描画先の外なら false
	bool ReadPixel( int32 X, int32 Y, FColor32& OutColor ) const;
	// 全体を、CPU 側の画像（FPixelImage）に読む。読めなければ、空の画像（IsValid が false）
	FPixelImage ReadPixelImage() const;

	// 全体をファイルに保存する。形式は Options.Format（既定は Png。透明度も残る）。途中のフォルダーは作る。メインスレッドだけ
	bool Save( const FString& Path, const FImageSaveOptions& Options = FImageSaveOptions() ) const;
	// 矩形 (X, Y, Width, Height) だけを保存する。矩形が描画先の外にはみ出していたら、何もせず false
	bool SaveRegion( int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options = FImageSaveOptions() ) const;

	// 色（4 バイト）と、Z バッファ（4 バイト）、マルチサンプルの分
	int64 GetMemoryBytes() const override;
	FString Describe() const override;

private:
	// 矩形が、描画先の中に収まっているか
	bool IsInside( int32 X, int32 Y, int32 Width, int32 Height ) const;

	FRenderTargetDesc Desc;
};
