// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FWorldSampleState;
class CWorldExplorer;

//=============================================================================
// CWorldSample
//   ワールド（.gxworld）の見本。レベルをファイルで作り（位置・メッシュ・エフェクト・タグ・プロパティ）、コードは、
//   読み込んで置き（CAssetSubsystem::LoadWorld → NWorld::Spawn）、タグに自分のクラスを結び付ける（FWorldBindings）だけ。
//     - タグ "Coin"（プロパティ "Value"）→ CWorldCoin: 回って、触れると集まるコイン
//     - タグ "Crystal"（プロパティ "Speed"）→ CWorldCrystal: 置き物のメッシュを回す
//     - タグ "PlayerStart" → CWorldExplorer の開始の位置（FindFirst で探す）
//     - タグ "Pillar" → 主人公がよける柱（FindAll で探す）
//     - Effect 部品（炎・オーラ）と Mesh 部品（地面・柱・結晶）は、置くだけで、見た目になる
//   R キーで、置いたワールドを消して（Despawn）、もう一度置く。操作: W A S D = 動く、← → / Q E = カメラ、Shift = 走る、R = 読み直し
//=============================================================================
class CWorldSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	// ワールドを読んで置く。置いたものの一覧と、コインの数を、状態に入れる
	void SpawnStage();

	TSharedPtr<FWorldSampleState> State;
	TSharedPtr<CWorldExplorer> Explorer;
};
