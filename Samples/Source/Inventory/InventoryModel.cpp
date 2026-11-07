#include "InventoryModel.h"

#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 Version = 1;
	constexpr int32 BaseAttack = 5;
	constexpr int32 BaseDefense = 1;
}

FInventoryModel FInventoryModel::MakeStarter()
{
	FInventoryModel Model;
	Model.Gold = 250;
	Model.Hp = 40;
	Model.AddItem( 1, 3 );
	Model.AddItem( 3, 1 );
	Model.AddItem( 5, 1 );
	Model.AddItem( 8, 2 );
	return Model;
}

int32 FInventoryModel::GetAttack() const
{
	int32 Value = BaseAttack;
	for ( const FItemStack& Stack : Equipment )
	{
		if ( const FItemDefinition* Item = Stack.IsEmpty() ? nullptr : NItemDatabase::Find( Stack.Id ) )
		{
			Value += Item->Attack;
		}
	}
	return Value;
}

int32 FInventoryModel::GetDefense() const
{
	int32 Value = BaseDefense;
	for ( const FItemStack& Stack : Equipment )
	{
		if ( const FItemDefinition* Item = Stack.IsEmpty() ? nullptr : NItemDatabase::Find( Stack.Id ) )
		{
			Value += Item->Defense;
		}
	}
	return Value;
}

bool FInventoryModel::HasFreeSlot() const
{
	for ( const FItemStack& Stack : Slots )
	{
		if ( Stack.IsEmpty() )
		{
			return true;
		}
	}
	return false;
}

int32 FInventoryModel::AddItem( int32 Id, int32 Count )
{
	const FItemDefinition* Item = NItemDatabase::Find( Id );
	if ( Item == nullptr || Count <= 0 )
	{
		return Count > 0 ? Count : 0;
	}
	// まず、同じ道具の山に足す
	for ( FItemStack& Stack : Slots )
	{
		if ( Count > 0 && !Stack.IsEmpty() && Stack.Id == Id && Stack.Count < Item->MaxStack )
		{
			const int32 Moved = NMath::Min( Count, Item->MaxStack - Stack.Count );
			Stack.Count += Moved;
			Count -= Moved;
		}
	}
	// 残りは、空の枠へ
	for ( FItemStack& Stack : Slots )
	{
		if ( Count > 0 && Stack.IsEmpty() )
		{
			const int32 Moved = NMath::Min( Count, Item->MaxStack );
			Stack.Id = Id;
			Stack.Count = Moved;
			Count -= Moved;
		}
	}
	return Count;
}

bool FInventoryModel::UseSlot( int32 Index, FString& OutMessage )
{
	if ( Index < 0 || Index >= NumSlots || Slots[Index].IsEmpty() )
	{
		return false;
	}
	const FItemDefinition* Item = NItemDatabase::Find( Slots[Index].Id );
	if ( Item == nullptr )
	{
		return false;
	}
	if ( Item->Type == EItemType::Consumable )
	{
		if ( Hp >= MaxHp )
		{
			OutMessage = "HP is already full.";
			return false;
		}
		Hp = NMath::Min( MaxHp, Hp + Item->Heal );
		if ( --Slots[Index].Count <= 0 )
		{
			Slots[Index].Clear();
		}
		OutMessage = FString::Printf( "Used %s. HP %d / %d.", Item->Name, Hp, MaxHp );
		return true;
	}
	EEquipSlot Slot = EEquipSlot::Weapon;
	if ( NItemDatabase::GetEquipSlot( *Item, Slot ) )
	{
		return Equip( Index, Slot, OutMessage );
	}
	OutMessage = FString::Printf( "%s cannot be used.", Item->Name );
	return false;
}

bool FInventoryModel::Equip( int32 Index, EEquipSlot Slot, FString& OutMessage )
{
	FItemStack& Source = Slots[Index];
	FItemStack& Target = Equipment[static_cast<int32>( Slot )];
	const FItemStack Moved = Source;
	// 付いているものと、入れ替える（持ち物の、同じ枠へ）
	Source = Target;
	Target = Moved;
	const FItemDefinition* Item = NItemDatabase::Find( Moved.Id );
	OutMessage = FString::Printf( "Equipped %s. Attack %d, Defense %d.", Item != nullptr ? Item->Name : "?", GetAttack(), GetDefense() );
	return true;
}

bool FInventoryModel::Unequip( EEquipSlot Slot )
{
	FItemStack& Target = Equipment[static_cast<int32>( Slot )];
	if ( Target.IsEmpty() )
	{
		return false;
	}
	for ( FItemStack& Stack : Slots )
	{
		if ( Stack.IsEmpty() )
		{
			Stack = Target;
			Target.Clear();
			return true;
		}
	}
	return false;
}

bool FInventoryModel::Buy( int32 Id )
{
	const FItemDefinition* Item = NItemDatabase::Find( Id );
	if ( Item == nullptr || Gold < Item->Price )
	{
		return false;
	}
	// 入りきらないときは、何も変えない（入れてみて、残ったら、戻す）
	FInventoryModel Backup = *this;
	if ( AddItem( Id, 1 ) > 0 )
	{
		*this = Backup;
		return false;
	}
	Gold -= Item->Price;
	return true;
}

int32 FInventoryModel::Sell( const FItemStack& Stack )
{
	const FItemDefinition* Item = Stack.IsEmpty() ? nullptr : NItemDatabase::Find( Stack.Id );
	if ( Item == nullptr )
	{
		return 0;
	}
	const int32 Total = Item->Price / 2 * Stack.Count;
	Gold += Total;
	return Total;
}

NlohmannJson FInventoryModel::ToJson() const
{
	NlohmannJson Json;
	Json["Version"] = Version;
	Json["Gold"] = Gold;
	Json["Hp"] = Hp;
	NlohmannJson SlotsJson = NlohmannJson::array();
	for ( const FItemStack& Stack : Slots )
	{
		SlotsJson.push_back( NlohmannJson::array( { Stack.IsEmpty() ? 0 : Stack.Id, Stack.IsEmpty() ? 0 : Stack.Count } ) );
	}
	Json["Slots"] = SlotsJson;
	NlohmannJson EquipJson = NlohmannJson::array();
	for ( const FItemStack& Stack : Equipment )
	{
		EquipJson.push_back( Stack.IsEmpty() ? 0 : Stack.Id );
	}
	Json["Equipment"] = EquipJson;
	return Json;
}

bool FInventoryModel::FromJson( const NlohmannJson& Json, FInventoryModel& Out )
{
	if ( !Json.is_object() || !Json.contains( "Slots" ) || !Json["Slots"].is_array() )
	{
		return false;
	}
	FInventoryModel Result;
	Result.Gold = NMath::Max( 0, Json.value( "Gold", 0 ) );
	Result.Hp = NMath::Clamp( Json.value( "Hp", MaxHp ), 1, MaxHp );
	const NlohmannJson& SlotsJson = Json["Slots"];
	for ( int32 Index = 0; Index < NumSlots && Index < static_cast<int32>( SlotsJson.size() ); ++Index )
	{
		const NlohmannJson& Entry = SlotsJson[static_cast<size_t>( Index )];
		if ( !Entry.is_array() || Entry.size() < 2 || !Entry[0].is_number_integer() || !Entry[1].is_number_integer() )
		{
			continue;
		}
		const FItemDefinition* Item = NItemDatabase::Find( Entry[0].get<int32>() );
		if ( Item != nullptr )
		{
			// 多すぎる個数は、山の上限へ。0 以下は、空
			const int32 Count = NMath::Clamp( Entry[1].get<int32>(), 0, Item->MaxStack );
			if ( Count > 0 )
			{
				Result.Slots[Index].Id = Item->Id;
				Result.Slots[Index].Count = Count;
			}
		}
	}
	if ( Json.contains( "Equipment" ) && Json["Equipment"].is_array() )
	{
		const NlohmannJson& EquipJson = Json["Equipment"];
		for ( int32 Index = 0; Index < static_cast<int32>( EEquipSlot::Count ) && Index < static_cast<int32>( EquipJson.size() ); ++Index )
		{
			if ( !EquipJson[static_cast<size_t>( Index )].is_number_integer() )
			{
				continue;
			}
			// その枠に入れられる道具だけ（武器の枠に、鎧は入らない）
			const FItemDefinition* Item = NItemDatabase::Find( EquipJson[static_cast<size_t>( Index )].get<int32>() );
			EEquipSlot Slot = EEquipSlot::Weapon;
			if ( Item != nullptr && NItemDatabase::GetEquipSlot( *Item, Slot ) && static_cast<int32>( Slot ) == Index )
			{
				Result.Equipment[Index].Id = Item->Id;
				Result.Equipment[Index].Count = 1;
			}
		}
	}
	Out = Result;
	return true;
}

bool FInventoryModel::IsSameAs( const FInventoryModel& Other ) const
{
	if ( Gold != Other.Gold || Hp != Other.Hp )
	{
		return false;
	}
	for ( int32 Index = 0; Index < NumSlots; ++Index )
	{
		if ( Slots[Index].IsEmpty() != Other.Slots[Index].IsEmpty() || ( !Slots[Index].IsEmpty() && ( Slots[Index].Id != Other.Slots[Index].Id || Slots[Index].Count != Other.Slots[Index].Count ) ) )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < static_cast<int32>( EEquipSlot::Count ); ++Index )
	{
		if ( Equipment[Index].IsEmpty() != Other.Equipment[Index].IsEmpty() || ( !Equipment[Index].IsEmpty() && Equipment[Index].Id != Other.Equipment[Index].Id ) )
		{
			return false;
		}
	}
	return true;
}
