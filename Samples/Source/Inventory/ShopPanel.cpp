#include "ShopPanel.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Inventory/InventoryState.h"
#include "Source/Inventory/InventoryUi.h"

namespace
{
	constexpr int32 RowWidth = 330;
	constexpr int32 RowHeight = 30;
	constexpr int32 SellWidth = 330;
	constexpr int32 SellHeight = 44;
}

CShopPanel::CShopPanel( const TSharedPtr<FInventoryState>& InState, const FVector2& InOrigin )
	: State( InState )
	, Origin( InOrigin )
{}

void CShopPanel::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	HotRow = -1;
	for ( int32 Row = 0; Row < NItemDatabase::Num(); ++Row )
	{
		if ( FUiRect { X, Y + Row * RowHeight, RowWidth, RowHeight - 2 }.Contains( Mouse.X, Mouse.Y ) )
		{
			HotRow = Row;
			State->HoverItemId = NItemDatabase::GetAt( Row ).Id;
			State->HoverX = X + RowWidth - 40;
			State->HoverY = Y + Row * RowHeight;
		}
	}
	const FUiRect SellRect { X, Y + NItemDatabase::Num() * RowHeight + 10, SellWidth, SellHeight };
	bHotSell = SellRect.Contains( Mouse.X, Mouse.Y );

	if ( !Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		return;
	}
	if ( bHotSell && !State->Held.IsEmpty() )
	{
		const FItemDefinition* Item = NItemDatabase::Find( State->Held.Id );
		const int32 Count = State->Held.Count;
		const int32 Total = State->Model.Sell( State->Held );
		State->Say( FString::Printf( "Sold %s x%d for %d gold.", Item != nullptr ? Item->Name : "?", Count, Total ) );
		State->Held.Clear();
	}
	else if ( HotRow >= 0 && State->Held.IsEmpty() )
	{
		const FItemDefinition& Item = NItemDatabase::GetAt( HotRow );
		if ( State->Model.Buy( Item.Id ) )
		{
			State->Say( FString::Printf( "Bought %s for %d gold.", Item.Name, Item.Price ) );
		}
		else if ( State->Model.Gold < Item.Price )
		{
			State->Say( "Not enough gold." );
		}
		else
		{
			State->Say( "The inventory is full." );
		}
	}
}

void CShopPanel::Draw( FDrawContext& CommandList )
{
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	NInventoryUi::DrawTitle( CommandList, X, Y - 28, "Shop  (click: buy one)" );
	for ( int32 Row = 0; Row < NItemDatabase::Num(); ++Row )
	{
		const FItemDefinition& Item = NItemDatabase::GetAt( Row );
		const bool bAffordable = State->Model.Gold >= Item.Price;
		const FUiRect Rect { X, Y + Row * RowHeight, RowWidth, RowHeight - 2 };
		CommandList.Box( Rect.X, Rect.Y, Rect.X + Rect.Width, Rect.Y + Rect.Height, Row == HotRow ? FColor32( 70, 80, 110 ) : FColor32( 38, 42, 58 ), true );
		CommandList.Box( Rect.X + 6, Rect.Y + 5, Rect.X + 22, Rect.Y + Rect.Height - 5, FColor32( Item.R, Item.G, Item.B ), true );
		CommandList.Text( Rect.X + 32, Rect.Y + 5, Item.Name, bAffordable ? FColor32::White : FColor32( 120, 122, 134 ) );
		CommandList.TextF( Rect.X + RowWidth - 80, Rect.Y + 5, bAffordable ? FColor32( 255, 224, 140 ) : FColor32( 120, 100, 90 ), "%4d G", Item.Price );
	}
	const FUiRect SellRect { X, Y + NItemDatabase::Num() * RowHeight + 10, SellWidth, SellHeight };
	const bool bCanSell = !State->Held.IsEmpty();
	CommandList.Box( SellRect.X, SellRect.Y, SellRect.X + SellRect.Width, SellRect.Y + SellRect.Height, bHotSell && bCanSell ? FColor32( 110, 70, 50 ) : FColor32( 48, 36, 34 ), true );
	CommandList.Box( SellRect.X, SellRect.Y, SellRect.X + SellRect.Width, SellRect.Y + SellRect.Height, FColor32( 160, 120, 100 ), false, 1 );
	CommandList.Text( SellRect.X + 10, SellRect.Y + 12, bCanSell ? "Sell: click here to sell what you hold" : "Sell: pick up an item, then click here", FColor32( 220, 190, 170 ) );
}
