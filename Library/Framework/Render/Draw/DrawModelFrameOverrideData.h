// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

struct FMatrix4x4;

// 描画命令へコピーするframe番号と親基準pose。行列は4行4列の行優先値を持つ。
struct FDrawModelFrameOverrideData
{
	int32 Frame = -1; // poseを適用するbone番号。
	float32 M[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }; // 行優先の親基準行列。

	// 初期化済みidentity poseを持つrecordを作る。
	FDrawModelFrameOverrideData() = default;
	// 指定骨の姿勢を描画命令用の値へ複写する。
	FDrawModelFrameOverrideData( int32 InFrame, const FMatrix4x4& LocalToParent );
};
