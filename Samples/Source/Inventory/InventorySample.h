// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FInventoryState;

//=============================================================================
// CInventorySample
//   ゲームの UI とセーブの見本（マウスで遊ぶ）。持ち物の枠・装備・店・セーブのスロット。
//     - ルール（FInventoryModel）と UI（パネル）を分ける。パネルは、マウスの入力と描画だけで、ルールの関数を呼ぶ
//     - 持ち上げる / 置く / 入れ替える / 山にまとめる、使う、装備する、買う、売る、ツールチップ
//     - セーブは、CSaveGameStorage（壊れにくい保存）。中身は JSON（形の悪い値は、安全な値に直して読む）
//   起動の引数 --selftest で、保存・読み込み・買う・売るの検査を、ログに出す（Inventory self-test: PASS）。
//   Esc で一覧へ戻る。
//=============================================================================
class CInventorySample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnExit() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FInventoryState> State;
};
