// SPDX-License-Identifier: NOASSERTION
#pragma once

namespace NAssetTest
{
	// 確認結果を収集する自己検査の判定役。
	struct FChecker;
}

// 実アセット読込テストが借りるasset管理役。
class CAssetManager;
// 実preview命令を描く既存backendの窓口。
class IDrawExecutor;

namespace NWorldTest
{
	// 独立した ImGui 入力で力場の編集・選択・履歴を確かめる。
	void RunEffectForceFieldEditorInputTests( NAssetTest::FChecker& C );
	// 実描画先へ力場ガイドを重ね、非表示時の従来画素と復元・解放を確かめる。
	void RunEffectForceFieldPreviewRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor );
}
