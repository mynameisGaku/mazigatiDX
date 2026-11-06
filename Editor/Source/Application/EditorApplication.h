// SPDX-License-Identifier: MIT
#pragma once

#include "Library/DebugUI/Asset/AssetTools.h"
#include "Library/DebugUI/DebugOverlay.h"
#include "Library/Framework/GameInstance/GameInstance.h"

//=============================================================================
// CEditorApplication
//   エディタ専用の実行ファイル（Editor.exe）。ゲームの実行ファイルにエディタを含めないための、別のアプリ。
//   メニューバー（Tools）から、アセットの画面・オーディオ・スプライトシートエディタ・モデルのアニメーションエディタ・
//   ワールドエディタ・エフェクトエディタ・AI エディタ・AI デバッガを開く。ゲームのシーンは持たない（空の画面）。
//
//   起動引数:
//     --assets=フォルダー    編集するゲームのアセットのルートフォルダー（無ければ、作業フォルダーの Assets）
//     --tool=assets|audio|sprite|model|world|effect|ai|aieditor   最初に開くツール（カンマ区切り。AssetTools.h の起動引数も使える:
//                            --ai-file=AI/Guard.gxbt、--world=...、など）。何も指定しなければ、アセットの画面を開く
//   デバッグ表示（F2: プロファイラー、F3: コンソール、F11: フルスクリーン）も使える。
//=============================================================================
class CEditorApplication : public CGameInstance
{
protected:
	void OnInit() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnTerm() override;

private:
	void DrawMenuBar();

	CDebugOverlay Overlay;
	FAssetToolsState Tools;
};
