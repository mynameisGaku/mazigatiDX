// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// computeの提出済み処理を指す値。キューの再初期化前の値を新しい提出と混同しない。
struct FComputeSubmission
{
	uint64 Queue = 0; // 作成時に発行する提出先の識別番号。APIのポインターではない。
	uint64 Value = 0; // その提出先で増加する完了番号。0は無効。
	// 提出された処理の番号が設定されているか。
	constexpr bool IsValid() const { return Queue != 0 && Value != 0; }
};
