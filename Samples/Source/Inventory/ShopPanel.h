// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FInventoryState;

//=============================================================================
// CShopPanel
//   店。品ぞろえの行をクリックすると、1 個買う（お金が足りない・持ち物がいっぱいなら、買えない）。
//   山を持っているとき、「Sell」の枠へ置く（左クリック）と、半値で売れる。
//=============================================================================
class CShopPanel : public CGameObject
{
public:
	CShopPanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FInventoryState> State;
	FVector2 Origin;
	int32 HotRow = -1;
	bool bHotSell = false;
};
