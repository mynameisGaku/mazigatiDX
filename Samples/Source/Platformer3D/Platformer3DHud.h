// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DHud
//   画面の上の表示（点数・コイン・残機・いまの姿・スターの残り時間・ゴール / ゲームオーバーの知らせ）。
//   表示の中身は、2D 版と共通（NPlatformerHud）。いちばん手前に描く。
//=============================================================================
class CPlatformer3DHud : public CGameObject
{
public:
	explicit CPlatformer3DHud( const TSharedPtr<FPlatformer3DState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer3DState> State;
};
