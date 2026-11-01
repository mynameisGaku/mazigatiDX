// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/PostEffects/PostEffectsDemoState.h"

//=============================================================================
// CPostEffectsSample
//   描画先とポストエフェクトの見本。夜の広場（光る玉・ネオンの溝）を、浮動小数点の描画先に描き、ブルーム・深度の霧・トーンマップを掛けて見せる:
//     浮動小数点の描画先（FRenderTargetDesc::Format = Float16 / Float32。1 を超える明るさが残る）、
//     複数の描画先への同時の書き出し（ShaderOutputTarget。色と深度を 1 回の描画で書く）、ポストエフェクトの連なり（抜き出す → ぼかす → 合成する）。
//   キーで切り替えて、8 ビットの描画先との違い・ブルーム・深度を確かめる。シーンは、描画先とシェーダーを用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CPostEffectsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FPostEffectsDemoState> State;
};
