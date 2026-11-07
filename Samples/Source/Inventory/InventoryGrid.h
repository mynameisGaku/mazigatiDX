// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FInventoryState;

//=============================================================================
// CInventoryGrid
//   持ち物の枠（5 x 4）。左クリックで、山を持ち上げる / 置く（同じ道具は、山にまとまる。違う道具は、入れ替わる）。
//   右クリックで、使う（回復薬）/ 装備する。持ち上げた山は、CInventoryOverlay が、マウスについて描く。
//=============================================================================
class CInventoryGrid : public CGameObject
{
public:
	// InOrigin は、枠の並びの左上（画面の座標）
	CInventoryGrid( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	static constexpr int32 Columns = 5;

private:
	// 枠 Index の、画面の位置（左上）
	void GetSlotPosition( int32 Index, int32& OutX, int32& OutY ) const;

	// マウスの下の枠の番号。どの枠でもなければ -1
	int32 FindSlotUnderMouse( int32 MouseX, int32 MouseY ) const;

	TSharedPtr<FInventoryState> State;
	FVector2 Origin;
	int32 HotSlot = -1;
};
