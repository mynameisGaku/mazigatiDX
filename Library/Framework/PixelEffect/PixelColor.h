// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/PixelEffect/PixelEffectTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

//=============================================================================
// NPixelColor
//   16 進数の色文字列・色の線形補間・明るさ変換・色の段（カラーランプ）の段数調整とサンプリング
//=============================================================================
namespace NPixelColor
{
	// よく使う基本色
	FORCEINLINE FColor32 White()
	{
		return FColor32( 255, 255, 255, 255 );
	}

	FORCEINLINE FColor32 Black()
	{
		return FColor32( 0, 0, 0, 255 );
	}

	FORCEINLINE FColor32 Ink()
	{
		return FColor32( 11, 10, 16, 255 );
	}

	// "#rgb" または "#rrggbb"（'#' なしも可）を不透明な FColor32 に変換する。不正な文字は 0 として扱う
	FColor32 FromHex( const char* HexText );
	FColor32 FromHex( const FString& HexText );

	// FColor32 の RGB を "#rrggbb"（小文字 16 進数）に変換する
	FString ToHex( const FColor32& Color );

	// 2 つの色を Alpha（0〜1）で線形に混ぜる（不透明度は 255）
	FColor32 Mix( const FColor32& ColorA, const FColor32& ColorB, float Alpha );

	// 色の明るさに Factor（0 以上）を掛け、0〜255 に収める（不透明度は 255）
	FColor32 ScaleBrightness( const FColor32& Color, float Factor );

	// 知覚的な明るさ（0〜1）を返す
	float GetLuminance( const FColor32& Color );

	// 16 進文字列の配列から色の段（暗い順→明るい順）を作る
	TArray<FColor32> MakeRamp( const char* const* HexStrings, int32 Count );
	TArray<FColor32> MakeRamp( const TArray<FString>& HexStrings );

	// 任意の長さの色の段 Ramp を、両端（最暗・最明）を保ったまま StepCount 段に揃え直す
	TArray<FColor32> FitRamp( const TArray<FColor32>& Ramp, int32 StepCount );

	// Normalized（0〜1）に対応する段の色を補間なしで選ぶ（ピクセルアートの段塗り用）
	FColor32 SampleStep( const TArray<FColor32>& Ramp, float Normalized );

	// Normalized（0〜1）に対応する色を隣り合う段の線形補間で求める（光のグラデーション用）
	FColor32 SampleBlend( const TArray<FColor32>& Ramp, float Normalized );

	// 既定のパレットを取得する（名前が見つからないときは Fire を返す）
	TArray<FColor32> GetPalette( EPixelPalette Palette );
	TArray<FColor32> GetPaletteByName( const FString& Name );
}
