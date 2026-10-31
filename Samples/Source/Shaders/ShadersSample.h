// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Shaders/ShadersDemoState.h"

//=============================================================================
// CShadersSample
//   シェーダーの見本。庭に球と柱を置き、自分で書いた HLSL のシェーダーで描く:
//     頂点シェーダー（物の行列とカメラの行列で位置を決める）、ピクセルシェーダー（トゥーン・法線の色・奥行き・波紋・市松模様）、
//     定数（ShaderConstants）、描画先への描画（PushTarget）、ポストエフェクト（描画先の絵を、ピクセルシェーダーを掛けて貼る。ShaderRect）。
//   数字キーで切り替えて、見え方の違いを確かめる。シーンは、シェーダーと描画先を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CShadersSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FShadersDemoState> State;
};
