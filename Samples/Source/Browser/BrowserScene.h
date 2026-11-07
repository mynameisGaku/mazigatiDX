// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Catalog/SampleInfo.h"

//=============================================================================
// CBrowserScene
//   サンプルの一覧の画面。上下キー（W / S も）で選んで、Enter（Space も）で、そのサンプルを動かす。
//   一覧は 2 列で、左の列を上から順に、続けて右の列に並ぶ（サンプルが増えても画面に収まる）。左右キー（A / D も）で、もう一方の列へ動ける。
//   サンプルから Esc で戻ったときは、最後に選んでいたサンプルが選ばれている。
//   一覧は CSampleCatalog から取るので、サンプルを足しても、この画面のコードは変わらない。
//=============================================================================
class CBrowserScene : public CScene
{
protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;

private:
	// 選んでいる番号を、Delta だけ動かす（端で止まる）
	void MoveSelection( int32 Delta );

	// 1 列に並べる行の数（Entries を 2 列に分けた、長いほうの列）
	int32 GetRowsPerColumn() const;

	// 表示する順に並べた、サンプルの情報（入ったときの写し）
	TArray<FSampleInfo> Entries;

	// 選んでいる番号（Entries の中）
	int32 SelectedIndex = 0;
};
