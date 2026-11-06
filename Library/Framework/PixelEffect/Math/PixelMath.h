// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// NPixelMath
//   ピクセルアート演出で使う決定論的なハッシュ・ノイズ・補間・イージング関数群。
//   すべて入力だけで結果が決まる純粋な計算になっており、時刻 t が同じなら同じ値になる。
//=============================================================================
namespace NPixelMath
{
	// 床関数・天井関数の整数版
	FORCEINLINE int32 FloorToInt( float Value )
	{
		return static_cast<int32>( NMath::Floor( Value ) );
	}

	FORCEINLINE int32 CeilToInt( float Value )
	{
		return static_cast<int32>( NMath::Ceil( Value ) );
	}

	// 1〜3 個の整数、または整数の配列から 32 ビットのハッシュ値を求める
	uint32 Hash( int32 KeyA );
	uint32 Hash( int32 KeyA, int32 KeyB );
	uint32 Hash( int32 KeyA, int32 KeyB, int32 KeyC );
	uint32 HashArray( const int32* Keys, int32 Count );

	// 整数キーから [0, 1) の決定論的な乱数を求める
	float RandomUnit( int32 KeyA );
	float RandomUnit( int32 KeyA, int32 KeyB );
	float RandomUnit( int32 KeyA, int32 KeyB, int32 KeyC );

	// 0〜1 に切り詰める
	FORCEINLINE float Clamp01( float Value )
	{
		return NMath::Clamp( Value, 0.0f, 1.0f );
	}

	// 線形補間と逆線形補間・範囲の写像
	FORCEINLINE float Lerp( float Start, float End, float Alpha )
	{
		return Start + ( End - Start ) * Alpha;
	}

	float InverseLerp( float Start, float End, float Value );
	float Remap( float Value, float InStart, float InEnd, float OutStart, float OutEnd );

	// 小数部分 [0, 1) と、正の周期 Period での折り返し [0, Period)
	float Fraction( float Value );
	float Wrap( float Value, float Period );
	int32 WrapInt( int32 Value, int32 Period );

	// 区間 [StartTime, EndTime] における Time の進み具合を 0〜1 で返す
	float SegmentProgress( float Time, float StartTime, float EndTime );

	// エルミート補間による滑らかな 0〜1 遷移（SmoothStep）
	float SmoothStep( float Edge0, float Edge1, float Value );

	// 区間 [StartTime, EndTime] で 0 → 1 → 0 と盛り上がる正弦波の山
	float SineBump( float Time, float StartTime, float EndTime );

	// 0 → 1 → 0 を周期 2 で往復する三角波
	float PingPong( float Value );

	// 2 点間の距離と向きの角度（ラジアン）
	float Distance( float X0, float Y0, float X1, float Y1 );
	float AngleBetween( float X0, float Y0, float X1, float Y1 );

	// イージング曲線（入力 0〜1 → 出力 0〜1。Back と Elastic は前後へ少しはみ出す）
	float EaseLinear( float T );
	float EaseInQuad( float T );
	float EaseOutQuad( float T );
	float EaseInOutQuad( float T );
	float EaseInCubic( float T );
	float EaseOutCubic( float T );
	float EaseInOutCubic( float T );
	float EaseInQuart( float T );
	float EaseOutQuart( float T );
	float EaseInExpo( float T );
	float EaseOutExpo( float T );
	float EaseInSine( float T );
	float EaseOutSine( float T );
	float EaseInOutSine( float T );
	float EaseInBack( float T, float Overshoot = 1.70158f );
	float EaseOutBack( float T, float Overshoot = 1.70158f );
	float EaseOutElastic( float T );
	float EaseOutBounce( float T );

	// 滑らかな 1 次元・2 次元の値ノイズ（[-1, 1]）
	float Noise1D( float X, int32 Seed = 0 );
	float Noise2D( float X, float Y, int32 Seed = 0 );

	// フラクタル 2 次元ノイズ（[-1, 1] 付近）
	float FractalNoise2D( float X, float Y, int32 Seed = 0, int32 Octaves = 3 );

	// 格子が PeriodX・PeriodY ごとに繰り返す 2 次元値ノイズ（0 なら繰り返さない）
	float TileNoise2D( float X, float Y, int32 PeriodX = 0, int32 PeriodY = 0, int32 Seed = 0 );
	float TileFractalNoise2D( float X, float Y, int32 PeriodX = 0, int32 PeriodY = 0, int32 Seed = 0, int32 Octaves = 3 );

	// Phase = 0 と Phase = 1 で同じ値に繋がる円周上のループノイズ（[-1, 1]）
	float LoopNoise( float Phase, float Radius = 1.0f, int32 Seed = 0 );
}
