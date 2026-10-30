// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Camera/CameraDemoState.h"

//=============================================================================
// CMainView
//   メインの 3D の画面。見せる範囲にクリップして、メインのカメラ（FCameraScope）で、地面と物を描く。
//   視錐台に入らない物は、描かない（FFrustum::IntersectsSphere。描画が減る）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CMainView : public CGameObject
{
public:
	explicit CMainView( const TSharedPtr<FCameraDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FCameraDemoState> State;
};
