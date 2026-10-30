// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shapes3D/Shapes3DDemoState.h"

//=============================================================================
// CShapes3DPanel
//   3D の上に重ねる、画面の座標の表示（2D）。
//   - 見どころの名札: 世界の点が、画面のどこに見えるか（FCamera::WorldToScreen）から、位置を決める。
//   - 切り替えられる設定の一覧（数字キーと、いまの値）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShapes3DPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CShapes3DPanel( const TSharedPtr<FShapes3DDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FShapes3DDemoState> State;
	TSharedPtr<CFont> Font;
};
