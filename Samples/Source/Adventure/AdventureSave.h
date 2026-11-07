// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Math.h"

struct FAdventureState;

//=============================================================================
// FAdventureSave
//   セーブされる、進み具合: 体力・回復薬・鍵・宝石・門を開けたか・開けた宝箱・倒した敵・拾ったもの・主人公の位置。
//   状態から取り出して（Capture）、状態へ戻す（Apply）。JSON との変換（ToJson / FromJson）は、形の悪い値を、安全な値に直して読む。
//   置き物は、定義の番号（Id）で覚える（名前を変えても、並べ替えても、変わらない）。
//=============================================================================
struct FAdventureSave
{
	int32 Hp = 100;
	int32 Potions = 1;
	int32 Keys = 0;
	int32 Gems = 0;
	bool bGateOpen = false;
	FVector3 Position;
	TArray<int32> OpenedChests;
	TArray<int32> DefeatedEnemies;
	TArray<int32> PickedUp;

	static FAdventureSave Capture( const FAdventureState& State );

	// 進み具合を、状態へ戻す（位置は、シーンが、主人公へ渡す）
	void Apply( FAdventureState& State ) const;

	NlohmannJson ToJson() const;
	static bool FromJson( const NlohmannJson& Json, FAdventureSave& Out );

	bool IsSameAs( const FAdventureSave& Other ) const;
};
