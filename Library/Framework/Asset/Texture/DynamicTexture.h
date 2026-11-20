// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Asset/Texture/Texture.h"

//=============================================================================
// CDynamicTexture
//   CPU 側の画素（FPixelImage）から作った画像。CTexture でもあるので、IImage として、ふつうの画像と同じように描ける。
//   中身（画素）は、あとから Update で替えられる（ハンドルは変わらないので、貼ってある場所は、そのまま新しい絵になる）。
//   CAssetSubsystem::CreateDynamicTexture で作る。最後の参照が消えると、自動で解放される（CTexture と同じ）。
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
	// 既存ハンドルを受け取る。最初にUpdateが成功するまで画素の版は取得できない。
	CDynamicTexture( const FString& InName, int32 InHandle, int32 InWidth, int32 InHeight );
	// 既に登録された画像と同じ画素を、版の作成用に保持する。
	CDynamicTexture( const FString& InName, int32 InHandle, const FPixelImage& Image );

	// 中身を、画素に替える。画像と同じ大きさ（幅・高さ）でなければ、何もせず false。メインスレッドだけ。
	// 全体を作り直すので、毎フレームでも、小さな画像（数百ピクセル四方まで）に向く
	bool Update( const FPixelImage& Image );

	// ゲームスレッドで最後にCPUから反映した画素を持つ画像を返す。同じ版の生存中は共有する。
	// 画素が未取得・資源作成失敗では空。GPU側で書き換えた画素は取り込まない。
	TSharedPtr<CTexture> CaptureCpuImageVersion() const;
	// GPU登録の推定と、保持するCPU画素の合計。
	int64 GetMemoryBytes() const override;
	FString Describe() const override;

private:
	FPixelImage CurrentPixels; // 作成または最後に受理されたUpdateの画素。
	mutable TWeakPtr<CTexture> CurrentVersion; // 捕捉側が保持している間だけ再利用する。
};
