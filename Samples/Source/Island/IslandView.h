// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandView
//   メインの 3D の画面。見せる範囲にクリップして、時刻の色の空を塗り、カメラを決めて、舞台（NIslandScenery）を記録する。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CIslandView : public CGameObject
{
public:
	explicit CIslandView( const TSharedPtr<FIslandState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FIslandState> State;
};
