// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FEffectsState;

//=============================================================================
// CEffectsSample
//   エフェクト（.gxeffect。内蔵のパーティクル）の見本。夜の舞台に、台座を並べて、炎・火花・オーラ・雷・尾を引く帯を再生する。
//   エフェクトは、ファイルから再生を作る（FEffectBackendRegistry::Play）だけ。位置・大きさ・雷の終点は、再生に渡す。
//   エディタで作ったエフェクトの、ゲームでの使い方の見本。ゲームでは、オブジェクトに CEffectComponent を付けるのが、手軽。
//   操作: ← → = 選ぶ、Space = 最初から、↑ ↓ = 大きさ、マウス = 雷の先と帯の先頭（選んでいるもの）
//=============================================================================
class CEffectsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FEffectsState> State;
};
