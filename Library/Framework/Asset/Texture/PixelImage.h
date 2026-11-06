// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

//=============================================================================
// EPixelBlend
//   画像の上に、別の画像を重ねるときの、色の合わせ方
//=============================================================================
enum class EPixelBlend : uint8
{
	Copy,    // 重ねる画素で、そのまま置き換える（透明度も、そのまま写る）
	Alpha,   // 重ねる画素の透明度で、下の画素と混ぜる（アルファブレンド。普通の重ね方）
};

//=============================================================================
// FPixelImage
//   CPU 側の画像（画素の並び）。DxLib に依存しない値で、描く・塗る・切り出す・重ねる・反転するなどが、ここで済む。
//   GPU に載せるには CAssetManager::CreateDynamicTexture（作る）/ CDynamicTexture::Update（中身を替える）、
//   ファイルとの行き来は CAssetManager::LoadPixelImage / DecodePixelImage / SavePixelImage を使う。
//
//   FPixelImage Canvas( 64, 64, FColor32( 0, 0, 0, 255 ) );             // 黒で塗った 64 x 64
//   Canvas.FillRect( 8, 8, 16, 16, FColor32( 255, 0, 0 ) );             // 赤い四角
//   Canvas.Set( 3, 4, FColor32( 0, 255, 0 ) );                          // 1 画素
//   Canvas.Blit( Icon, 40, 10, EPixelBlend::Alpha );                    // 別の画像を、透明度で混ぜて重ねる
//   TSharedPtr<CDynamicTexture> Texture = Assets->CreateDynamicTexture( "Canvas", Canvas );
//
//   - 画素は、左上から右へ、上から下へ並ぶ。色は FColor32（透明度は、乗算されていない普通のアルファ）
//   - 範囲の外を読むと透明な黒、書くと何もしない（false を返すものもある）。矩形は、はみ出す分を切る
//   - 大きさは 1〜MaxSize（画素の総数は MaxPixels まで）。範囲の外の大きさで作ると、空の画像（IsValid が false）になる
//=============================================================================
class FPixelImage
{
public:
	// 幅・高さの上限（ピクセル。画像を GPU に載せられる大きさ）と、画素の総数の上限（メモリは 4 バイト x 画素の数）
	static constexpr int32 MaxSize = 16384;
	static constexpr int64 MaxPixels = 67108864;

	// 空の画像（IsValid が false）
	FPixelImage() = default;
	// Width x Height の画像を、FillColor（既定は透明な黒）で塗って作る。大きさが 1〜MaxSize の外・画素の総数が MaxPixels を超えるときは、空の画像
	FPixelImage( int32 InWidth, int32 InHeight, const FColor32& FillColor = FColor32( 0, 0, 0, 0 ) );
	// 画素の並び（Width x Height 個）から作る。大きさが範囲の外・Pixels が nullptr なら、空の画像
	static FPixelImage FromPixels( int32 InWidth, int32 InHeight, const FColor32* Pixels );

	// 作れた（大きさが 1 以上で、画素の数が合っている）か
	FORCEINLINE bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height; }
	FORCEINLINE int32 GetWidth() const { return Width; }
	FORCEINLINE int32 GetHeight() const { return Height; }
	// 画素の並び（左上から右へ、上から下へ。Width x Height 個）
	FORCEINLINE const TArray<FColor32>& GetPixels() const { return Pixels; }
	FORCEINLINE const FColor32* GetData() const { return Pixels.GetData(); }

	// (X, Y) が、画像の中か
	FORCEINLINE bool IsInside( int32 X, int32 Y ) const { return X >= 0 && Y >= 0 && X < Width && Y < Height; }

	// 1 画素を読む。範囲の外は、透明な黒
	FColor32 Get( int32 X, int32 Y ) const;
	// 1 画素を書く。範囲の外なら、何もせず false
	bool Set( int32 X, int32 Y, const FColor32& Color );

	// 全体を、1 色で塗る
	void Fill( const FColor32& Color );
	// 矩形 (X, Y, W, H) を、1 色で塗る。画像からはみ出す分は切る。幅・高さが 0 以下なら、何もしない
	void FillRect( int32 X, int32 Y, int32 W, int32 H, const FColor32& Color );

	// Source の矩形 (SrcX, SrcY, SrcW, SrcH) を、この画像の (DestX, DestY) へ重ねる。Source の外・この画像の外へはみ出す分は切る（DestX / DestY は負でもよい）。
	// Source が自分自身でもよい（重なっていても、重ねる前の絵が写る）
	void Blit( const FPixelImage& Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, EPixelBlend Blend = EPixelBlend::Copy );
	// Source の全体を、(DestX, DestY) へ重ねる
	void Blit( const FPixelImage& Source, int32 DestX, int32 DestY, EPixelBlend Blend = EPixelBlend::Copy );

	// 矩形 (X, Y, W, H) を切り出した、新しい画像。画像の外にはみ出す分は、透明な黒。幅・高さが範囲の外なら、空の画像
	FPixelImage Crop( int32 X, int32 Y, int32 W, int32 H ) const;

	// 左右・上下を入れ替える
	void FlipHorizontal();
	void FlipVertical();

	// 大きさも全画素も同じか（空どうしは同じ）
	bool operator==( const FPixelImage& Other ) const;
	FORCEINLINE bool operator!=( const FPixelImage& Other ) const { return !( *this == Other ); }

	// 2 つの色を、Blend で合わせる（Dest の上に Source を重ねた色）。Alpha は、乗算されていないアルファの「Source を Dest の上に」（Porter-Duff の over）。
	// 結果の透明度は Source.a + Dest.a x ( 1 - Source.a )、色は透明度で重みをつけた平均。四捨五入する
	static FColor32 Combine( const FColor32& Dest, const FColor32& Source, EPixelBlend Blend );

private:
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> Pixels;
};
