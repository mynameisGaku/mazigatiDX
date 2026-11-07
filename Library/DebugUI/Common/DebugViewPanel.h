// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/DebugView/DebugViews.h"

namespace NDebugUI
{
	// デバッグビューの調整項目を、型に合うウィジェット（スイッチ・ドラッグ）で並べる。ImGui のウィンドウの中から呼ぶ。値は、項目が指す実体へ、そのまま書く
	void DrawDebugViewOptions( const FDebugView& View );
}
