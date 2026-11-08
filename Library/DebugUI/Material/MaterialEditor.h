// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/DebugUI/AI/NodeGraph.h"
#include "Library/DebugUI/Material/MaterialPreviewSurface.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Material/MaterialDefinition.h"

//=============================================================================
// マテリアルエディタ（.gxmaterial。Docs/MATERIAL.md）
//   ノードのグラフを、直角の線とノードの箱（AI エディタと同じ FNodeGraphCanvas）で編集する。UE のマテリアルエディタに近い作り。
//     中央   グラフ: ノードをドラッグで動かす・出力のピン（右）から入力のピン（左）へドラッグでつなぐ・つないだ入力のピンを押して、はずす／つなぎ直す・
//            右クリックでノードを足す（検索つき）・Delete で消す・右端の「Result」が、ドメインごとの出力ピン。背景のドラッグでパン、ホイールでズーム。
//     左     ノードの一覧（検索つき。分類ごと。使えないドメインのものは出さない）・パラメーター（プレビューの値を変える）・診断（押すと、そのノードへ）
//     右     選んだノードの設定（名前・値・つないでいないピンの数）・プレビュー（球・立方体・板。UI・Particle は矩形、PostProcess は決まった場面）
//   変更するたびに、コンパイルし直して（CMaterial::Reload）、診断とプレビューを更新する。保存は JSON（.gxmaterial）。"Engine:/" のマテリアルは、読み取り専用（別名で保存する）。
//=============================================================================
struct FMaterialEditorState
{
	// 編集中の定義と、そのコンパイル結果
	FMaterialDefinition Definition;
	TSharedPtr<CMaterial> Material;
	TArray<FMaterialDiagnostic> Diagnostics;
	// 保存していない変更があるか
	bool bDirty = false;
	// 編集中のファイル（空なら、保存先が未定）と、「名前を付けて保存」の入力・開くファイルの選択
	FString FilePath;
	FString SaveAsPath;
	FString OpenSelection;
	TArray<FString> Files;
	double LastScanTime = -100.0;
	// 操作結果の文章
	FString StatusMessage;
	bool bStatusIsError = false;
	bool bInitialized = false;

	// グラフ
	FNodeGraphView View;
	int32 SelectedNode = -1;
	int32 MovingNode = -1;
	int32 DragFromNode = -1;       // つなぎ線を、出力のピンから引いている最中のノード
	bool bPanBlocked = false;
	ImVec2 PopupWorld = ImVec2( 0.0f, 0.0f );
	ImVec2 CanvasCenterWorld = ImVec2( 0.0f, 0.0f );
	char SearchBuffer[64] = {};

	// プレビュー
	FMaterialPreviewSurface Preview;
	int32 PreviewShape = 0;
	float32 PreviewYaw = 0.6f;
	float32 PreviewPitch = 0.35f;
	bool bPreviewSpin = true;
	FMaterialFrameState PreviewFrame;
};

namespace NMaterialEditorUI
{
	// 空のマテリアルから始める
	void NewMaterial( FMaterialEditorState& State, EMaterialDomain Domain, EMaterialShading Shading );
	// ファイル（アセットのパス。"Engine:/..." も読める）を開く。失敗したら false（StatusMessage に理由）
	bool OpenMaterial( FMaterialEditorState& State, const FString& Path );
	// 保存する（Path が空なら、FilePath へ）。読み取り専用の場所・書けない場所は false
	bool SaveMaterial( FMaterialEditorState& State, const FString& Path );
	// 定義を、コンパイルし直して、診断を更新する（変更のたびに呼ぶ）
	void Recompile( FMaterialEditorState& State );
	// ノードを、つながり方に合わせて、左から右へ並べ直す
	void ArrangeNodes( FMaterialEditorState& State );
	// ノードを足す（既定の名前・値つき）。番号を返す
	int32 AddNodeWithDefaults( FMaterialEditorState& State, EMaterialNodeKind Kind, const ImVec2& WorldPosition );
}

void DrawMaterialEditor( FMaterialEditorState& State, bool* bOpen );
