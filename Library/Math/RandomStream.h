// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FRandomStream
//   種（Seed）で列が決まる乱数のクラス。同じ種なら、環境（バックエンド・OS・ビルド）に依らず、同じ列になる。
//   リプレイ・検査・手続き的な生成（ダンジョン、粒子のばらつき）で、「毎回同じ結果」にしたいときに使う。
//   アルゴリズムは PCG32（O'Neill 氏の公開された方式。種 42・列 54 の最初の 6 つが 0xa15c02b7 0x7b47f409 0xba1d3330 0x83d2f293 0xbfa4784b 0xcbed606e）。
//
//   FRandomStream Random( 12345 );
//   const float32 Roll = Random.NextFloat();                 // 0 以上 1 未満
//   const int32 Damage = Random.NextInt( 8, 12 );            // 8 以上 12 以下
//   const FVector3 Direction = Random.NextUnitVector();
//   Random.Shuffle( Deck );
//
//   - 暗号には使えない（予測できる）。使うスレッドは 1 つ（並列で使うときは、スレッドごとに別の FRandomStream を持つ）
//   - 状態は GetState / SetState で保存・復元できる（セーブデータ・リプレイの途中から再開）
//=============================================================================
class FRandomStream
{
public:
	// 保存・復元できる状態
	struct FState
	{
		uint64 State = 0;
		uint64 Increment = 0;
	};

	// 種 0・列 54 で始める（毎回同じ列）
	FRandomStream();

	// Seed: 列を決める種。Sequence: 同じ種でも別の列にするための番号
	explicit FRandomStream( uint64 Seed, uint64 Sequence = 54 );

	// 種と列を決め直して、最初から始める
	void Reset( uint64 Seed, uint64 Sequence = 54 );

	FState GetState() const;
	void SetState( const FState& InState );

	// 0 以上 0xFFFFFFFF 以下の一様な整数
	uint32 NextUInt32();

	// 0 以上 Bound 未満の一様な整数（偏りなし）。Bound が 0 なら 0
	uint32 NextUInt32( uint32 Bound );

	// Min 以上 MaxInclusive 以下の一様な整数。Min > MaxInclusive なら、入れ替えて扱う
	int32 NextInt( int32 Min, int32 MaxInclusive );

	// 0 以上 1 未満
	float32 NextFloat();

	// Min 以上 Max 未満（Min と Max が逆でも使える）
	float32 NextFloat( float32 Min, float32 Max );

	// 確率 TrueProbability（0〜1。範囲の外は切る）で true
	bool NextBool( float32 TrueProbability = 0.5f );

	// 正規分布（平均 Mean・標準偏差 StdDev）
	float32 NextGaussian( float32 Mean = 0.0f, float32 StdDev = 1.0f );

	// 長さ 1 の向き（球面上に一様）
	FVector3 NextUnitVector();

	// 長さ 1 の向き（円周上に一様）
	FVector2 NextUnitVector2();

	// 並びをかき混ぜる（Fisher-Yates。要素の並びが等しい確率で入れ替わる）
	template <typename T>
	void Shuffle( TArray<T>& Items )
	{
		for ( int32 Index = Items.Num() - 1; Index > 0; --Index )
		{
			const int32 Other = static_cast<int32>( NextUInt32( static_cast<uint32>( Index + 1 ) ) );
			if ( Other != Index )
			{
				T Temp = Items[Index];
				Items[Index] = Items[Other];
				Items[Other] = Temp;
			}
		}
	}

private:
	uint64 State = 0;
	uint64 Increment = 0;
};
