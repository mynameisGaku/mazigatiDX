#include "InventoryGrid.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/InventoryUi.h"

CInventoryGrid::CInventoryGrid( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin )
	: State( InState )
	, Origin( InOrigin )
{}

void CInventoryGrid::GetSlotPosition( int32 Index, int32& OutX, int32& OutY ) const
{
	OutX = static_cast<int32>( Origin.x ) + ( Index % Columns ) * ( NInventoryUi::SlotSize + NInventoryUi::SlotGap );
	OutY = static_cast<int32>( Origin.y ) + ( Index / Columns ) * ( NInventoryUi::SlotSize + NInventoryUi::SlotGap );
}

int32 CInventoryGrid::FindSlotUnderMouse( int32 MouseX, int32 MouseY ) const
{
	for ( int32 Index = 0; Index < FInventoryModel::NumSlots; ++Index )
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

void CInventoryGrid::Tick( float32 DeltaTime )
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
	FItemStack& Slot = State->Model.Slots[HotSlot];
	if ( !Slot.IsEmpty() )
	{
		State->HoverItemId = Slot.Id;
		GetSlotPosition( HotSlot, State->HoverX, State->HoverY );
	}

	if ( Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		if ( State->Held.IsEmpty() )
		{
			// 持ち上げる
			State->Held = Slot;
			Slot.Clear();
		}
		else if ( Slot.IsEmpty() )
		{
			// 空の枠へ置く
			Slot = State->Held;
			State->Held.Clear();
		}
		else if ( Slot.Id == State->Held.Id )
		{
			// 同じ道具: 山にまとめる（入りきらない分は、持ったまま）
			const FItemDefinition* Item = NItemDatabase::Find( Slot.Id );
			const int32 Space = Item != nullptr ? Item->MaxStack - Slot.Count : 0;
			const int32 Moved = Space < State->Held.Count ? Space : State->Held.Count;
			Slot.Count += Moved;
			State->Held.Count -= Moved;
			if ( State->Held.Count <= 0 )
			{
				State->Held.Clear();
			}
		}
		else
		{
			// 違う道具: 入れ替える
			const FItemStack Swap = Slot;
			Slot = State->Held;
			State->Held = Swap;
		}
	}
	else if ( Input->IsMouseButtonPressed( EMouseButton::Right ) && State->Held.IsEmpty() )
	{
		FString Message;
		if ( State->Model.UseSlot( HotSlot, Message ) )
		{
			State->Say( Message );
		}
		else if ( !Message.IsEmpty() )
		{
			State->Say( Message );
		}
	}
}

void CInventoryGrid::Draw( FDrawContext& CommandList )
{
	NInventoryUi::DrawTitle( CommandList, static_cast<int32>( Origin.x ), static_cast<int32>( Origin.y ) - 28, "Inventory" );
	CommandList.Text( static_cast<int32>( Origin.x ), static_cast<int32>( Origin.y ) + 4 * ( NInventoryUi::SlotSize + NInventoryUi::SlotGap ), "Left click: pick up / put down    Right click: use / equip", FColor32( 150, 154, 176 ) );
	for ( int32 Index = 0; Index < FInventoryModel::NumSlots; ++Index )
	{
		int32 X = 0;
		int32 Y = 0;
		GetSlotPosition( Index, X, Y );
		NInventoryUi::DrawSlot( CommandList, FUiRect { X, Y, NInventoryUi::SlotSize, NInventoryUi::SlotSize }, State->Model.Slots[Index], Index == HotSlot );
	}
}
