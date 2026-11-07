// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DLevelView
//   面の見た目: 空・遠くの丘と雲（カメラの半分の速さで動く）・タイル・ゴールのポール。画面に見える範囲のタイルだけを描く。
//   叩かれたブロックは、ぽんと持ち上がる（FPlatformer2DLevel::Bumps）。いちばん奥に描く。
//=============================================================================
class CPlatformer2DLevelView : public CGameObject
{
public:
	explicit CPlatformer2DLevelView( const TSharedPtr<FPlatformer2DState>& InState );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	void DrawBackground( FDrawContext& CommandList ) const;
	void DrawTile( FDrawContext& CommandList, int32 TileX, int32 TileY, char Tile ) const;

	TSharedPtr<FPlatformer2DState> State;
};
