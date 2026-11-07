#include "InventoryOverlay.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/InventoryUi.h"

CInventoryOverlay::CInventoryOverlay( const TSharedPtr<FInventoryState>& InState )
	: State( InState )
{}

void CInventoryOverlay::Draw( FDrawContext& CommandList )
{
	// メッセージの履歴（下）
	CommandList.Box( 40, 596, 1240, 712, FColor32( 22, 24, 34 ), true );
	CommandList.Box( 40, 596, 1240, 712, FColor32( 70, 76, 100 ), false, 1 );
	for ( int32 Index = 0; Index < State->Messages.Num(); ++Index )
	{
		const bool bNewest = Index == State->Messages.Num() - 1;
		CommandList.Text( 52, 604 + Index * 21, *State->Messages[Index], bNewest ? FColor32::White : FColor32( 150, 154, 176 ) );
	}

	// ツールチップ（指している道具）
	if ( const FItemDefinition* Item = NItemDatabase::Find( State->HoverItemId ) )
	{
		const int32 X = State->HoverX + 20;
		const int32 Y = State->HoverY + 40;
		CommandList.Box( X, Y, X + 330, Y + 78, FColor32( 14, 16, 24 ), true );
		CommandList.Box( X, Y, X + 330, Y + 78, FColor32( 200, 210, 255 ), false, 1 );
		CommandList.Text( X + 10, Y + 8, Item->Name, FColor32( 255, 224, 140 ) );
		CommandList.Text( X + 10, Y + 30, Item->Description, FColor32::White );
		CommandList.TextF( X + 10, Y + 52, FColor32( 170, 176, 200 ), "Price %d  (sells for %d)", Item->Price, Item->Price / 2 );
	}
	State->HoverItemId = 0;

	// 持ち上げた山（マウスについてくる）
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && !State->Held.IsEmpty() )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		NInventoryUi::DrawSlot( CommandList, FUiRect { Mouse.X - 24, Mouse.Y - 24, 48, 48 }, State->Held, true );
	}
}
