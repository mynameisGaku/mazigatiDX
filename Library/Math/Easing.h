// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// イージング（動きの緩急）
//   時間の進み具合 T（0〜1）を、値の進み具合へ直す。どれも、0 は 0、1 は 1 になる（途中で範囲の外へ出る Back・Elastic も、端では 0 と 1）。
//   T は 0〜1 に丸めてから計算する。UI・カメラ・ゲームの演出など、どこからでも使える（Math だけに依存する）。
//
//   const float32 Amount = NEasing::Evaluate( EEasing::CubicOut, Elapsed / Duration );
//   const FVector3 Position = NEasing::Interpolate( Start, Goal, EEasing::BackOut, Elapsed / Duration );
//
//   In は「ゆっくり始まる」、Out は「ゆっくり終わる」、InOut は「両端がゆっくり」。
//=============================================================================
enum class EEasing : uint8
{
	Linear,       // 一定の速さ
	QuadIn,
	QuadOut,
	QuadInOut,
	CubicIn,
	CubicOut,
	CubicInOut,
	QuartIn,
	QuartOut,
	QuartInOut,
	SineIn,
	SineOut,
	SineInOut,
	ExpoIn,       // とても急に始まる
	ExpoOut,      // とても急に止まる
	ExpoInOut,
	BackIn,       // 始めに、少し逆へ引いてから進む
	BackOut,      // 終点を少し行き過ぎて戻る
	BackInOut,
	ElasticOut,   // 終点でばねのように揺れる
	BounceOut,    // 終点で跳ねる
	Count
};

namespace NEasing
{
	// T（0〜1）に対する、値の進み具合
	float32 Evaluate( EEasing Easing, float32 T );
	// 名前（"CubicOut" など。ファイル・エディタ用）
	const char* ToString( EEasing Easing );
	// 名前から読む。知らない名前・null なら false（OutEasing は変えない）
	bool FromString( const char* Name, EEasing& OutEasing );

	// From から To へ、緩急を付けて補間する（数・ベクトル・色など、足し算と float32 倍ができる型）
	template<typename TValue>
	TValue Interpolate( const TValue& From, const TValue& To, EEasing Easing, float32 T )
	{
		return From + ( To - From ) * Evaluate( Easing, T );
	}
}
