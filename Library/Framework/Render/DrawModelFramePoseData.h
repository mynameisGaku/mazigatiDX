// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/DrawModelFrameOverrideData.h"

struct FDrawCommandView;

// 一回の通常モデル描画と、その間だけ有効にするframe pose群を持つcommand header。
struct FDrawModelFramePoseData
{
	int32 Handle = -1; // native model handle。
	float32 M[12] = {}; // modelのlocal-to-world affine値。
	int32 AnimIndex = -1; // 描画前に時刻を設定するattach番号。
	float32 AnimTime = 0.0f; // 描画前に設定するanimation時刻。
	int32 NumOverrides = 0; // 末尾に続くpose record数。
};

// ModelFramePose3D commandの値と可変長pose記録を検査する。
namespace NDrawModelFramePose
{
	constexpr int32 MaxOverrides = 256; // 1命令に保存できるframe poseの上限。

	// モデル、変換、再生時間の固定値が描画に使えるか調べる。
	bool IsValidHeader( const FDrawModelFramePoseData& Data );
	// frame番号の重複、行列の有限性とaffine条件を調べる。
	bool IsValidOverrides( const FDrawModelFrameOverrideData* Overrides, int32 NumOverrides );
	// command flag、payload長、headerと全poseを検査してheaderを返す。
	bool IsValidCommand( const FDrawCommandView& Command, FDrawModelFramePoseData& OutData );
	// 検査済みcommandから1つのposeをaligned localへ複写する。
	bool ReadOverride( const FDrawCommandView& Command, int32 Index, FDrawModelFrameOverrideData& OutOverride );
}
