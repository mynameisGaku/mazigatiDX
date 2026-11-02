// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 各層の重みと骨の親番号から、全身および骨ごとの有限なブレンド率を求める。
struct FAnimationBlendLayer
{
	float32 Weight = 0.0f;	// この層の重み
	int32 PartFrame = -1;		// 一部の動きを始める骨の番号（-1 なら全身）
};

// 骨単位で設定する率。親子で重なる指定も、最終的な各骨の率として返す。
struct FAnimationFrameRate
{
	int32 Layer = -1;		// 層の番号（入力の並び）
	int32 Frame = -1;		// 率を設定する骨の番号
	float32 Rate = 0.0f;	// この骨に適用する割合
};

struct FAnimationBlendResult
{
	// 全身層の割合（入力と同じ並び）
	TArray<float32> WholeRates;
	// 骨ごとの割合（骨番号、層番号の順）
	TArray<FAnimationFrameRate> FrameRates;
};

namespace NAnimationBlend
{
	// 親番号はフレーム番号順。範囲外・循環・自己参照があれば false。
	bool IsValidHierarchy( TArrayView<const int32> Parents );
	// 親子が不正なら全身率だけを返す。PreviouslyAffectedFrames の骨にも現在の全層率を出して、前に設定した率を戻す。
	FAnimationBlendResult Compute( TArrayView<const FAnimationBlendLayer> Layers, TArrayView<const int32> Parents, TArrayView<const uint8> PreviouslyAffectedFrames = TArrayView<const uint8>() );
}
