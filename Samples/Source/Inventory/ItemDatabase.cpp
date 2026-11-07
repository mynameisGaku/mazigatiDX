#include "InventoryTypes.h"

namespace
{
	// 道具の表（番号は、セーブデータに入るので、変えない・使い回さない）
	const FItemDefinition Items[] =
	{
		{ 1, "Potion", EItemType::Consumable, "P", 230, 80, 90, "Heals 30 HP.", 20, 9, 30, 0, 0 },
		{ 2, "Hi-Potion", EItemType::Consumable, "H", 240, 120, 200, "Heals 80 HP.", 60, 9, 80, 0, 0 },
		{ 3, "Wooden Sword", EItemType::Weapon, "w", 190, 150, 90, "A plain sword. Attack +3.", 40, 1, 0, 3, 0 },
		{ 4, "Iron Sword", EItemType::Weapon, "S", 190, 200, 230, "A sturdy sword. Attack +7.", 120, 1, 0, 7, 0 },
		{ 5, "Leather Armor", EItemType::Armor, "a", 170, 120, 70, "Light armor. Defense +2.", 50, 1, 0, 0, 2 },
		{ 6, "Iron Armor", EItemType::Armor, "A", 150, 170, 210, "Heavy armor. Defense +5.", 150, 1, 0, 0, 5 },
		{ 7, "Lucky Charm", EItemType::Charm, "C", 120, 220, 140, "Attack +1, Defense +1.", 80, 1, 0, 1, 1 },
		{ 8, "Gem", EItemType::Material, "G", 120, 230, 250, "Shiny. Sells well.", 200, 20, 0, 0, 0 },
	};
}

const FItemDefinition* NItemDatabase::Find( int32 Id )
{
	for ( const FItemDefinition& Item : Items )
	{
		if ( Item.Id == Id )
		{
			return &Item;
		}
	}
	return nullptr;
}

int32 NItemDatabase::Num()
{
	return static_cast<int32>( sizeof( Items ) / sizeof( Items[0] ) );
}

const FItemDefinition& NItemDatabase::GetAt( int32 Index )
{
	return Items[Index];
}

bool NItemDatabase::GetEquipSlot( const FItemDefinition& Item, EEquipSlot& OutSlot )
{
	switch ( Item.Type )
	{
	case EItemType::Weapon:
		OutSlot = EEquipSlot::Weapon;
		return true;
	case EItemType::Armor:
		OutSlot = EEquipSlot::Armor;
		return true;
	case EItemType::Charm:
		OutSlot = EEquipSlot::Charm;
		return true;
	default:
		return false;
	}
}
