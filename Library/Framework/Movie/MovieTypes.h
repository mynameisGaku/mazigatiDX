// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ムービーの値の型（バックエンドに依らない）
//=============================================================================

// 開いたムービーの情報。分からない項目は 0
struct FMovieInfo
{
	// 映像の大きさ（ピクセル）
	int32 Width = 0;
	int32 Height = 0;
	// 長さ（秒）
	float32 DurationSeconds = 0.0f;
	// 1 秒あたりのコマ数（可変のときは平均）
	float32 FrameRate = 0.0f;
};
