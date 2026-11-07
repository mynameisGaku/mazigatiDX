// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Source/Inventory/InventoryModel.h"

//=============================================================================
// FInventoryState
//   Inventory のオブジェクトが分け合う状態。ルール（FInventoryModel）、マウスで持ち上げている山（Held）、
//   マウスが指している道具（Hover。ツールチップ用）、メッセージの履歴。
//   パネルは、毎フレーム、Hover を決める（指していなければ 0）。CInventoryOverlay が、ツールチップを描く。
//=============================================================================
struct FInventoryState
{
	FInventoryModel Model = FInventoryModel::MakeStarter();

	// マウスで持ち上げている山（空なら、何も持っていない）
	FItemStack Held;

	// マウスが指している道具の番号（ツールチップ用）と、描く位置
	int32 HoverItemId = 0;
	int32 HoverX = 0;
	int32 HoverY = 0;

	// 新しい順ではなく、古い順に並べる（最後が、最新）
	TArray<FString> Messages;

	void Say( const FString& Text )
	{
		Messages.Add( Text );
		while ( Messages.Num() > 5 )
		{
			Messages.RemoveAt( 0 );
		}
	}

	// 持っている山を、持ち物の空の枠へ戻す（枠が無ければ、そのまま持つ）
	void ReturnHeld()
	{
		if ( !Held.IsEmpty() && Model.AddItem( Held.Id, Held.Count ) == 0 )
		{
			Held.Clear();
		}
	}
};
