// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FAdventureState;

//=============================================================================
// CAdventureHud
//   画面の表示: 体力の帯・回復薬・鍵・宝石、近くの調べられるものの案内、メッセージの履歴、いまの目的、倒れた / 出口に着いたの知らせ。
//   いちばん手前に描く（3D のあとの 2D は、別のオブジェクトにする）。
//=============================================================================
class CAdventureHud : public CGameObject
{
public:
	explicit CAdventureHud( const TSharedPtr<FAdventureState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FAdventureState> State;
};
