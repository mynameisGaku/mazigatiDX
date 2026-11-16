// SPDX-License-Identifier: MIT
#pragma once
#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CCharacterSample
//   PMX（MikuMikuDance のモデル）を読み込んで、骨をコードで動かす（モーションのファイルは使わない）。
//   モデルは Assets/Models/Faputa/Faputa.pmx（利用者が置く。リポジトリには入れない）。無ければ、置き場所を画面に出す。
//=============================================================================
class CCharacterSample : public CSampleScene
{
protected:
	// モデルの読み込みと、骨を動かす描画を持つオブジェクトを置く。
	void OnEnter() override;
};
