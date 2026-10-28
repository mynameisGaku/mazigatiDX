// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CInputSample
//   入力のサンプル。キーボード（CKeyboardView）・マウス（CMouseView）・ゲームパッド（CGamepadView）の状態を、それぞれのオブジェクトが描く。
//   シーンは、3 つを画面のどこに置くかを決めるだけ。入力の読み方は、各オブジェクトのクラスにある。
//=============================================================================
class CInputSample : public CSampleScene
{
protected:
	void OnEnter() override;
};
