// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// インベントリのサンプルの、基本の型
//=============================================================================

// 道具の種類
enum class EItemType : uint8
{
	Consumable,   // 使うと、なくなる（回復薬）
	Weapon,
	Armor,
	Charm,
	Material,     // 売るだけ
};

// 装備の枠（EItemType の Weapon / Armor / Charm に、順に対応する）
enum class EEquipSlot : uint8
{
	Weapon,
	Armor,
	Charm,
	Count,
};

// 道具 1 種類の、定義（変わらないデータ）
struct FItemDefinition
{
	int32 Id = 0;
	const char* Name = "";
	EItemType Type = EItemType::Material;
	const char* Glyph = "?";            // 絵の代わりに、枠の中に描く 1 文字
	uint8 R = 200;
	uint8 G = 200;
	uint8 B = 200;
	const char* Description = "";
	int32 Price = 0;                    // 買う値段（売るときは、半分）
	int32 MaxStack = 1;
	int32 Heal = 0;                     // 回復薬: 回復する量
	int32 Attack = 0;                   // 装備: 足される攻撃・防御
	int32 Defense = 0;
};

// 持ち物の 1 つの山（道具の番号と、個数。Id が 0 なら、空）
struct FItemStack
{
	int32 Id = 0;
	int32 Count = 0;

	bool IsEmpty() const { return Id == 0 || Count <= 0; }
	void Clear()
	{
		Id = 0;
		Count = 0;
	}
};

namespace NItemDatabase
{
	// 番号の道具。無ければ nullptr（0 も nullptr）
	const FItemDefinition* Find( int32 Id );

	// 定義の数と、番号順の、n 番目（店の品ぞろえに使う）
	int32 Num();
	const FItemDefinition& GetAt( int32 Index );

	// 装備の枠に入れられる道具か（Weapon / Armor / Charm のうち、対応する枠）
	bool GetEquipSlot( const FItemDefinition& Item, EEquipSlot& OutSlot );
}
