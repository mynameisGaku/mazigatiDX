// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FPixelRandom
//   シードから毎回同じ乱数列を返す乱数生成器（Mulberry32）。
//   粒子やエフェクトごとに FPixelRandom::FromKeys( Seed, ParticleIndex ) を作れば、
//   どのフレームで描き直しても同じ乱数列が得られる。
//=============================================================================
class FPixelRandom
{
public:
	// シード（0 のときは 1 として扱う）を指定して初期化する
	explicit FPixelRandom( uint32 InSeed = 1 );

	// 複数の整数キー（シード・粒子番号・系列番号など）を混ぜて乱数生成器を作る
	static FPixelRandom FromKeys( int32 KeyA, int32 KeyB = 0, int32 KeyC = 0 );

	// [0, 1) の一様乱数を返す
	float NextFloat();

	// [MinValue, MaxValue) の一様乱数を返す
	float Range( float MinValue, float MaxValue );

	// [MinInclusive, MaxInclusive] の整数乱数を返す（MinInclusive > MaxInclusive のときは入れ替える）
	int32 RangeInt( int32 MinInclusive, int32 MaxInclusive );

	// Probability（0〜1）の確率で true を返す
	bool Chance( float Probability );

	// 1/2 の確率で -1 または +1 を返す
	int32 Sign();

	// 平均 0・標準偏差およそ 1 の山なりの乱数を返す
	float Gaussian();

	// 内部の状態を読む
	FORCEINLINE uint32 GetState() const
	{
		return State;
	}

private:
	uint32 State = 1;
};
