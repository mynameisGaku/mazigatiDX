// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CAudioSample
//   音の見本。音のファイルは使わず、計算で作った音を鳴らす。音は、操作したときだけ鳴る（始めは無音）。
//     - 鍵盤（CToneKeys）: 1 つの音を、Pitch と Pan を変えて鳴らす。音量・高さ・左右（FSoundPlayParams）
//     - 動く球（CPanBall）: ずっと鳴る音（ループ）を、球の位置でパンさせる。フェードで止める
//     - シンセ（CSynthPad）: 計算した波形を、CPcmStream に次々に足して、その場で音を作る
//   上下キーで、全体の音量（ミキサーのマスターバス。CAudioSubsystem::SetMasterVolume）を変える。
//=============================================================================
class CAudioSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnExit() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	// 入る前の全体の音量（出るときに戻す）
	float32 SavedMasterVolume = 1.0f;
};
