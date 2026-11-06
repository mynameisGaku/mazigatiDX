// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CAISample
//   AI（状態管理）の見本。主人公（ステートマシン）と見張り（ビヘイビアツリー）を、データ（JSON）で動かす。
//     主人公  Samples/Assets/AI/Hero.gxstate / Hero.gxblackboard   A / D か 左右キーで歩き、Space か J で殴る
//     見張り  Samples/Assets/AI/Guard.gxbt / Guard.gxblackboard    巡回 -> 主人公を見つけたら追う -> 近づいたら殴る -> 殴られたら怯む
//   遷移や優先順位は、コードでなく JSON にある。F1 のウィンドウで「AI debugger」を開く（起動引数 --tool=ai でも開く）と、
//   動いているステート・ノード・ブラックボードの値・遷移の履歴を追えて、一時停止やステップ実行もできる。
//=============================================================================
class CAISample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
