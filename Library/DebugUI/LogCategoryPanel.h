#pragma once

namespace NDebugUI
{
	// ログカテゴリの一覧を ImGui で表示し、カテゴリごとに Verbosity を実行時に変更できるようにする（ImGui のウィンドウ内で呼ぶ）。
	// Verbose にすると、オブジェクトの生成・破棄、ファイルの読み書き、入力デバイスの操作など細かい動作がログに出る。
	void DrawLogCategories();
}
