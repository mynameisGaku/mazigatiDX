// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FAdventureState;
class CAdventurePlayer;

//=============================================================================
// CAdventureSample
//   3D 探索アクションの参照ゲーム「遺跡からの脱出」。ここまでのサンプルの機能を、1 つのゲームにまとめたもの。
//     - 面: ワールド（Worlds/ruins.gxworld）。壁・柱・門・焚き火・宝箱・敵の置き場所・出口は、ファイル（タグとプロパティ）で決める
//     - 主人公: ステートマシン（Player.gxstate）。待つ・走る・斬る・転がる（無敵）・のけぞる・倒れる。入力は、ブラックボードへ
//     - 敵: 体の状態はステートマシン（Enemy.gxstate）、頭脳はビヘイビアツリー（Enemy.gxbt）。スライムとスケルトン
//     - 持ち物と UI: 回復薬・鍵・宝石、体力の帯、メッセージ、調べられるものの案内
//     - セーブ: 焚き火で休む（回復 + セーブ）、F5 でセーブ、F9 でロード（CSaveGameStorage）
//     - 見た目: メッシュ・炎のエフェクト・斬ったときの火花、効果音（計算で作った短い音）
//   目的: 敵を倒しながら、宝箱の鍵を見つけて、門を開け、奥の祭壇（出口）へ行く。
//   起動の引数: --selftest（セーブデータの検査をログへ）、--start=2（2 つ目の部屋から）/ --start=3（門を開けて、3 つ目の部屋から）
//=============================================================================
class CAdventureSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnExit() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	// ワールドを読んで置く（置き直すときは、先に消す）。通れない四角形と、出口の位置を、状態へ入れる
	void SpawnStage();

	// 進み具合を、最初に戻す（やり直し）
	void ResetProgress();

	void QuickSave( const char* Reason );
	void QuickLoad();

	TSharedPtr<FAdventureState> State;
	TSharedPtr<CAdventurePlayer> Player;
};
