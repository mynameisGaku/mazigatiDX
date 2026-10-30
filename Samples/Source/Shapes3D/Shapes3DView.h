// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shapes3D/Shapes3DDemoState.h"

//=============================================================================
// CShapes3DView
//   メインの 3D の画面。見せる範囲にクリップして、夜空を塗り、メインのカメラ（FCameraScope）で、広場（NShapes3DScenery）を描く。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShapes3DView : public CGameObject
{
public:
	explicit CShapes3DView( const TSharedPtr<FShapes3DDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FShapes3DDemoState> State;
};
