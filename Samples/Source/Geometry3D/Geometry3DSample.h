// SPDX-License-Identifier: MIT
#pragma once
#include "Source/Catalog/SampleScene.h"
#include "Library/Framework/Render/Scene/SceneDrawSnapshot.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

// 共通命令によるカメラ・頂点色・深度の見本を配置する。
class CGeometry3DSample : public CSampleScene
{
protected:
	// 描画の責務を持つオブジェクトを一つ置く。
	void OnEnter() override;
	// 比較モードでは、確定済みの一覧か手動で並べた独立した項目を再生する。
	void OnSampleDraw( FImmediateDraw& Screen ) override;
private:
	FSceneDrawSnapshot Snapshot; // カメラ・項目・画像の所有参照を固定した一覧。
	TArray<FDrawBuffer> ReferencePackets; // 並べ替え処理を使わない手動基準の命令。
	bool bSnapshotMode = false; // 一覧の比較モードか。
	bool bReferenceMode = false; // 手動で正しい順に記録した基準を使うか。
};
