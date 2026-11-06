// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/AssetUsageScanner.h"

//=============================================================================
// 「Used by」の表示（どのワールドが、このアセットを使っているか）
//   エフェクトエディタ・モデルのアニメーションエディタの中に出す（ツール同士の連携）。
//   ワールドを全部読むので、自動では走らせない（Scan ボタン。開いているファイルが変わると、結果は捨てる）。
//   ワールドごとの Open ボタンで、ワールドエディタで開ける（NDebugUI::RequestOpenAssetInEditor）。
//=============================================================================
struct FAssetUsesState
{
	FString ScannedPath;                  // 結果を作ったときの、アセットのパス（正規化したもの）
	bool bScanned = false;
	TArray<FWorldUsage> Results;
};

namespace NDebugUI
{
	// AssetPath を使っているワールドの一覧を出す（ウィンドウの中の、好きな場所に）。AssetPath が空なら、保存を促す
	void DrawAssetUsesPanel( FAssetUsesState& State, const FString& AssetPath );
}
