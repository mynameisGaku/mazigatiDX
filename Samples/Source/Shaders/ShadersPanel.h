// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shaders/ShadersDemoState.h"

//=============================================================================
// CShadersPanel
//   3D の上に重ねる、画面の座標の表示（2D）。切り替えられる設定の一覧（数字キーと、いまの値）。
//   ポストエフェクトの影響を受けないよう、場面を貼ったあとに描く。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShadersPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CShadersPanel( const TSharedPtr<FShadersDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FShadersDemoState> State;
	TSharedPtr<CFont> Font;
};
