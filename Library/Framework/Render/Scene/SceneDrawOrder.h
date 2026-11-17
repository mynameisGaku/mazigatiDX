// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/DrawSpan.h"

struct FDrawCommandView;

namespace NSceneDrawOrder
{
	// 姿勢や描画状態を変更せず単独で移動できる固定長3D描画かを返す。不正な長さはfalse。
	bool IsIndependent( const FDrawCommandView& Command );
	// シーン内の連続した分類付き3D描画を並べる。状態変更・未分類描画・2Dは越えない。
	// 入力は実行終了まで不変で保持する。入れ子・不正なキー・長さ・上限超過では元の順序を返す。
	// Storageは入力と別の呼出し専用領域。bValidは不正なシーンを検出した場合falseになる。
	FDrawSpan Prepare( const FDrawSpan& Input, TArray<uint32>& Storage, bool& bValid );
}
