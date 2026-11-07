// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FInventoryState;

//=============================================================================
// CInventoryOverlay
//   いちばん手前に描く: マウスについてくる、持ち上げた山、指している道具のツールチップ、メッセージの履歴。
//   毎フレームの最初に、ツールチップの対象を空にする（パネルが、その後に決める）。
//=============================================================================
class CInventoryOverlay : public CGameObject
{
public:
	explicit CInventoryOverlay( const TSharedPtr<FInventoryState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FInventoryState> State;
};
