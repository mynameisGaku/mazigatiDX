#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/ModelAnimationEditor.h"
#include "Library/DebugUI/SpriteSheetEditor.h"
#include "Library/DebugUI/WorldEditor.h"

//=============================================================================
// アセット周りのデバッグ画面・エディタをまとめたもの
//   - Assets: 読み込んでいるアセットの一覧（種類・パス・参照の数・メモリ・読み込み時間）、種類ごとの集計、ビデオメモリなど
//   - Audio : 鳴っている音の一覧、バスの音量・ミュート・一時停止、リスナー、数の上限
//   - Sprite Sheet Editor / Model Animation Editor: データアセット（.sheet.json / .modelanim.json）の編集
//   - World Editor: ワールド（.world）のレベルエディタ（WorldEditor.h）
//   - Effect Editor: 内蔵の粒子定義（.effect.json）の編集と保存前の試し再生
//
//   ゲーム側は FAssetToolsState を 1 つ持ち、毎フレーム DrawAssetTools を呼ぶだけ。
//   メニューの出し方は DrawAssetToolsMenu（デバッグウィンドウの中に置く）。
//=============================================================================
struct FAssetToolsState
{
	bool bShowAssets = false;
	bool bShowAudio = false;
	bool bShowSpriteSheetEditor = false;
	bool bShowModelAnimationEditor = false;
	bool bShowWorldEditor = false;
	// 内蔵エフェクトの定義編集画面を表示するか。
	bool bShowEffectEditor = false;

	FSpriteSheetEditorState SpriteSheet;
	FModelAnimationEditorState ModelAnimation;
	FWorldEditorState WorldEditor;
	// 文書とプレビューの寿命を、ほかのツールと同じ所有元にまとめる。
	FEffectEditorState EffectEditor;
	// 配置済みのプレビューへ最後に反映した、エフェクト保存の成功番号。
	uint32 ObservedEffectSaveSerial = 0;

	// Assets 画面
	char AssetFilter[128] = {};
	int32 AssetTypeFilter = 0;      // 0 で全部（1.. は EAssetType + 1）
	int32 AssetSortColumn = 3;
	bool bAssetSortAscending = false;
	bool bAssetStatsDirty = true;
	FString ExportStatusMessage;
	bool bExportStatusError = false;

	// 起動引数 --tool=... で開いたものを、最初の 1 回だけ設定したか
	bool bCommandLineApplied = false;
};

namespace NDebugUI
{
	// 開いているウィンドウを描く（毎フレーム）
	void DrawAssetTools( FAssetToolsState& State );

	// デバッグウィンドウの中に置く、ツールを開くボタン
	void DrawAssetToolsButtons( FAssetToolsState& State );

	// 起動引数: --tool=assets|audio|sprite|model|world|effect（カンマ区切りで複数）、--sprite=Sprites/Hero.sheet.json、--modelanim=Models/Robot.modelanim.json、--world=Levels/Stage1.world、--effect=Effects/campfire.effect.json。
	// ワールドを開いたあとの確認用: --world-select=3,4 --world-mode=move|rotate|scale --world-colliders=off|all --world-camera=yaw,pitch,距離[,x,y,z]。
	// 最初に 1 回呼ぶ（スクリーンショットでの確認や、すぐ編集を始めたいとき用）
	void ApplyAssetToolsCommandLine( FAssetToolsState& State );

	// アプリの終了前に、ツールが持っている資源（3D プレビューの画面など）を片付ける
	void ShutdownAssetTools( FAssetToolsState& State );
}
