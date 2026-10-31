// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shaders/ShadersDemoState.h"

//=============================================================================
// CShadersView
//   メインの 3D の画面。2 段で描く:
//   ① 場面（地面と物）を、自分で書いたシェーダーで、描画先（State->SceneTarget）へ描く。頂点シェーダーが、物の行列とカメラの行列で位置を決め、
//      ピクセルシェーダーが、トゥーン・法線の色・奥行き・波紋・市松模様の色を決める。定数（カメラの行列・光・時間・物の色）は ShaderConstants で渡す。
//   ② 描画先の絵を、ポストエフェクトのピクセルシェーダー（白黒・周りを暗く・モザイク・ゆがみ）を掛けて、見せる範囲に貼る（ShaderRect）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShadersView : public CGameObject
{
public:
	explicit CShadersView( const TSharedPtr<FShadersDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 場面（地面と物）を、いま決まっているシェーダーで記録する
	void RecordScene( FDrawContext& CommandList ) const;

	TSharedPtr<FShadersDemoState> State;
};
