// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FInventoryState;

//=============================================================================
// CEquipmentPanel
//   装備の 3 つの枠（武器・防具・お守り）と、能力（体力・攻撃・防御・お金）。
//   枠をクリック: 持っていなければ、装備を持ち上げる。山を持っていて、種類が合えば、装備する（付いていたものと入れ替え）。
//=============================================================================
class CEquipmentPanel : public CGameObject
{
public:
	CEquipmentPanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 枠 Index の、画面の位置（左上）
	void GetSlotPosition( int32 Index, int32& OutX, int32& OutY ) const;
	int32 FindSlotUnderMouse( int32 MouseX, int32 MouseY ) const;

	TSharedPtr<FInventoryState> State;
	FVector2 Origin;
	int32 HotSlot = -1;
};
