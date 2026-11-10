// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// イージング（動きの緩急）
//   時間の進み具合 T（0〜1）を、値の進み具合へ直す。0 は 0、1 は 1 になる（途中で行き過ぎる Back・Elastic も、終点では 1）。
//   T は 0〜1 に丸めてから計算する。
//=============================================================================
enum class EUIEasing : uint8
{
	Linear,       // 一定の速さ
	QuadIn,       // ゆっくり始まる
	QuadOut,      // ゆっくり終わる
	QuadInOut,
	CubicIn,
	CubicOut,
	CubicInOut,
	SineInOut,    // なめらかな往復
	BackOut,      // 終点を少し行き過ぎて戻る
	ElasticOut,   // 終点でばねのように揺れる
	BounceOut,    // 終点で跳ねる
	Count
};

namespace NUIEasing
{
	// T（0〜1）に対する、値の進み具合
	float32 Evaluate( EUIEasing Easing, float32 T );
	// 名前（"CubicOut" など。マークアップ・ファイル用）
	const char* ToString( EUIEasing Easing );
	// 名前から読む。知らない名前なら false
	bool FromString( const char* Name, EUIEasing& OutEasing );
}
