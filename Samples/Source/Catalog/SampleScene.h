// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Scene/Scene.h"

class FImmediateDraw;

//=============================================================================
// CSampleScene
//   サンプルのシーンの基底。どのサンプルでも同じ動き（Esc で一覧へ戻る、画面の上に見出しを出す）を、ここで済ませる。
//   サンプルは、OnTick / OnDraw の代わりに、OnSampleTick / OnSampleDraw を実装する（基底の処理を呼び忘れないため）。
//   オブジェクトの生成は、CScene と同じく OnEnter で行う。
//
//   class CHelloSample : public CSampleScene
//   {
//   protected:
//       void OnEnter() override;
//       void OnSampleDraw( FImmediateDraw& Screen ) override;
//   };
//=============================================================================
class CSampleScene : public CScene
{
protected:
	// 毎フレーム、シーンの時間を進めるときに呼ばれる（既定は何もしない）
	virtual void OnSampleTick( float32 DeltaTime );

	// 毎フレーム、オブジェクトを描いたあとに呼ばれる。Screen に描くと、見出しより前（奥）に出る（既定は何もしない）
	virtual void OnSampleDraw( FImmediateDraw& Screen );

	// Esc で一覧へ戻る。そのあと OnSampleTick を呼ぶ
	void OnTick( float32 DeltaTime ) final;

	// OnSampleDraw を呼んだあと、見出し（サンプルの名前と、戻るキー）を描く
	void OnDraw() final;
};
