// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// C__NAME__Sample
//   __TITLE__ のサンプル。
//   ここに、このサンプルで見せたいことを書く。シーンは、何をどこに置くかだけを決める。
//   描き方・動き方は、オブジェクトのクラス（このフォルダーの別のファイル。1 ファイルに 1 クラス）に分ける。
//=============================================================================
class C__NAME__Sample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
