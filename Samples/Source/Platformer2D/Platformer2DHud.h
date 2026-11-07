// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DHud
//   画面の上の表示: 点数・コイン・残機・いまの姿（ちいさい / おおきい / ファイア）・スターの残り時間。
//   ゴールしたとき・ゲームオーバーのときの、大きな知らせ。いちばん手前に描く。
//=============================================================================
class CPlatformer2DHud : public CGameObject
{
public:
	explicit CPlatformer2DHud( const TSharedPtr<FPlatformer2DState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer2DState> State;
};
