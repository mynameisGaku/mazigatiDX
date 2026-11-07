// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FWorldSampleState;

//=============================================================================
// CWorldSampleHud
//   画面の上の表示: 集めたコイン・点数・置いたワールドの名前と数。全部集めたときの知らせ。いちばん手前に描く。
//=============================================================================
class CWorldSampleHud : public CGameObject
{
public:
	explicit CWorldSampleHud( const TSharedPtr<FWorldSampleState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FWorldSampleState> State;
};
