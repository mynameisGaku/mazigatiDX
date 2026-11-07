// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Json/JsonObject.h"
#include "Source/Inventory/InventoryTypes.h"

//=============================================================================
// FInventoryModel
//   持ち物・装備・お金・体力の、ルール（描かない・入力を知らない）。画面の UI は、この関数を呼ぶだけ。
//   セーブデータの形（JSON）も、ここが決める（ToJson / FromJson）。
//   - 持ち物: 20 の枠。同じ道具は、MaxStack まで、山にまとまる
//   - 装備: 武器・防具・お守りの 3 つの枠。攻撃・防御は、装備で変わる
//   - 買う / 売る: 売る値段は、買う値段の半分
//=============================================================================
class FInventoryModel
{
public:
	static constexpr int32 NumSlots = 20;
	static constexpr int32 MaxHp = 100;

	// 最初の持ち物（回復薬・剣・鎧・宝石・お金。体力は半分より少し下）
	static FInventoryModel MakeStarter();

	FItemStack Slots[NumSlots];
	FItemStack Equipment[static_cast<int32>( EEquipSlot::Count )];
	int32 Gold = 0;
	int32 Hp = MaxHp;

	int32 GetAttack() const;
	int32 GetDefense() const;

	// 道具を、Count 個足す（山にまとめ、足りなければ、空の枠へ）。入りきらなかった個数を返す（0 なら、全部入った）
	int32 AddItem( int32 Id, int32 Count );

	// 空の枠があるか
	bool HasFreeSlot() const;

	// 持ち物の枠 Index の道具を使う / 装備する（回復薬は使う、装備品は付ける）。結果の文を OutMessage に入れる。できなければ false
	bool UseSlot( int32 Index, FString& OutMessage );

	// 装備の枠から外して、持ち物の空の枠へ入れる。空きが無い・装備が無いなら false
	bool Unequip( EEquipSlot Slot );

	// 買う（お金が足りない・入りきらないなら、何も変えずに false）
	bool Buy( int32 Id );

	// 道具の山を売る（山ごと）。売れた金額を返す。売れない（空・存在しない道具）なら 0
	int32 Sell( const FItemStack& Stack );

	// セーブデータ（JSON）と、その読み込み。読み込みは、形の悪い値（存在しない番号・多すぎる個数・負の値）を、安全な値へ直す。
	// 形が違いすぎて読めなければ false（Out は変えない）
	NlohmannJson ToJson() const;
	static bool FromJson( const NlohmannJson& Json, FInventoryModel& Out );

	// 中身が同じか（検査用）
	bool IsSameAs( const FInventoryModel& Other ) const;

private:
	// 装備品を、Slot の枠へ付ける（前に付いていたものは、持ち物の Index の枠へ入れ替える）
	bool Equip( int32 Index, EEquipSlot Slot, FString& OutMessage );
};
