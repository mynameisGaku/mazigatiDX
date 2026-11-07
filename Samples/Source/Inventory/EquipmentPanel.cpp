#include "EquipmentPanel.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/InventoryUi.h"

namespace
{
	const char* const SlotNames[] = { "Weapon", "Armor", "Charm" };
}

CEquipmentPanel::CEquipmentPanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin )
	: State( InState )
	, Origin( InOrigin )
{}

void CEquipmentPanel::GetSlotPosition( int32 Index, int32& OutX, int32& OutY ) const
{
	OutX = static_cast<int32>( Origin.x ) + Index * ( NInventoryUi::SlotSize + NInventoryUi::SlotGap + 18 );
	OutY = static_cast<int32>( Origin.y );
}

int32 CEquipmentPanel::FindSlotUnderMouse( int32 MouseX, int32 MouseY ) const
{
	for ( int32 Index = 0; Index < static_cast<int32>( EEquipSlot::Count ); ++Index )
	{
		int32 X = 0;
		int32 Y = 0;
		GetSlotPosition( Index, X, Y );
		if ( FUiRect { X, Y, NInventoryUi::SlotSize, NInventoryUi::SlotSize }.Contains( MouseX, MouseY ) )
		{
			return Index;
		}
	}
	return -1;
}

void CEquipmentPanel::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	HotSlot = FindSlotUnderMouse( Mouse.X, Mouse.Y );
	if ( HotSlot < 0 )
	{
		return;
	}
	FItemStack& Slot = State->Model.Equipment[HotSlot];
	if ( !Slot.IsEmpty() )
	{
		State->HoverItemId = Slot.Id;
		GetSlotPosition( HotSlot, State->HoverX, State->HoverY );
	}
	if ( !Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		return;
	}
	if ( State->Held.IsEmpty() )
	{
		// 外して、持ち上げる
		State->Held = Slot;
		Slot.Clear();
		return;
	}
	// 持っている山が、この枠に入る種類なら、装備する（付いていたものは、手に持つ）
	const FItemDefinition* Item = NItemDatabase::Find( State->Held.Id );
	EEquipSlot Wanted = EEquipSlot::Weapon;
	if ( Item != nullptr && NItemDatabase::GetEquipSlot( *Item, Wanted ) && static_cast<int32>( Wanted ) == HotSlot )
	{
		const FItemStack Old = Slot;
		Slot.Id = State->Held.Id;
		Slot.Count = 1;
		State->Held = Old;
		State->Say( FString::Printf( "Equipped %s. Attack %d, Defense %d.", Item->Name, State->Model.GetAttack(), State->Model.GetDefense() ) );
	}
	else
	{
		State->Say( "That does not fit this slot." );
	}
}

void CEquipmentPanel::Draw( FDrawContext& CommandList )
{
	NInventoryUi::DrawTitle( CommandList, static_cast<int32>( Origin.x ), static_cast<int32>( Origin.y ) - 28, "Equipment" );
	for ( int32 Index = 0; Index < static_cast<int32>( EEquipSlot::Count ); ++Index )
	{
		int32 X = 0;
		int32 Y = 0;
		GetSlotPosition( Index, X, Y );
		NInventoryUi::DrawSlot( CommandList, FUiRect { X, Y, NInventoryUi::SlotSize, NInventoryUi::SlotSize }, State->Model.Equipment[Index], Index == HotSlot );
		CommandList.Text( X, Y + NInventoryUi::SlotSize + 4, SlotNames[Index], FColor32( 170, 176, 200 ) );
	}
	// 能力
	const FInventoryModel& Model = State->Model;
	const int32 TextY = static_cast<int32>( Origin.y ) + NInventoryUi::SlotSize + 34;
	CommandList.TextF( static_cast<int32>( Origin.x ), TextY, FColor32::White, "HP      %d / %d", Model.Hp, FInventoryModel::MaxHp );
	// 体力の帯
	const int32 BarX = static_cast<int32>( Origin.x ) + 150;
	CommandList.Box( BarX, TextY + 2, BarX + 150, TextY + 16, FColor32( 50, 30, 30 ), true );
	CommandList.Box( BarX, TextY + 2, BarX + 150 * Model.Hp / FInventoryModel::MaxHp, TextY + 16, FColor32( 220, 70, 70 ), true );
	CommandList.TextF( static_cast<int32>( Origin.x ), TextY + 24, FColor32::White, "Attack  %d", Model.GetAttack() );
	CommandList.TextF( static_cast<int32>( Origin.x ), TextY + 48, FColor32::White, "Defense %d", Model.GetDefense() );
	CommandList.TextF( static_cast<int32>( Origin.x ), TextY + 72, FColor32( 255, 224, 140 ), "Gold    %d", Model.Gold );
}
