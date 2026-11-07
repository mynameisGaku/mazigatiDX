#include "InventoryUi.h"

#include "Library/Framework/Render/Draw/DrawContext.h"

void NInventoryUi::DrawSlot( FDrawContext& CommandList, const FUiRect& Rect, const FItemStack& Stack, bool bHot )
{
	CommandList.Box( Rect.X, Rect.Y, Rect.X + Rect.Width, Rect.Y + Rect.Height, bHot ? FColor32( 70, 80, 110 ) : FColor32( 38, 42, 58 ), true );
	CommandList.Box( Rect.X, Rect.Y, Rect.X + Rect.Width, Rect.Y + Rect.Height, bHot ? FColor32( 190, 210, 255 ) : FColor32( 90, 96, 120 ), false, 2 );
	const FItemDefinition* Item = Stack.IsEmpty() ? nullptr : NItemDatabase::Find( Stack.Id );
	if ( Item == nullptr )
	{
		return;
	}
	// 絵の代わりに、色つきの四角形と、1 文字
	CommandList.Box( Rect.X + 10, Rect.Y + 10, Rect.X + Rect.Width - 10, Rect.Y + Rect.Height - 10, FColor32( Item->R, Item->G, Item->B ), true );
	CommandList.Box( Rect.X + 10, Rect.Y + 10, Rect.X + Rect.Width - 10, Rect.Y + Rect.Height - 10, FColor32( 20, 22, 30 ), false, 1 );
	CommandList.Text( Rect.X + Rect.Width / 2 - 4, Rect.Y + Rect.Height / 2 - 9, Item->Glyph, FColor32( 20, 22, 30 ) );
	if ( Stack.Count > 1 )
	{
		CommandList.TextF( Rect.X + Rect.Width - 26, Rect.Y + Rect.Height - 20, FColor32::White, "x%d", Stack.Count );
	}
}

void NInventoryUi::DrawButton( FDrawContext& CommandList, const FUiRect& Rect, const char* Label, bool bHot, bool bEnabled )
{
	const FColor32 Fill = !bEnabled ? FColor32( 42, 44, 52 ) : ( bHot ? FColor32( 80, 110, 180 ) : FColor32( 58, 72, 120 ) );
	CommandList.Box( Rect.X, Rect.Y, Rect.X + Rect.Width, Rect.Y + Rect.Height, Fill, true );
	CommandList.Box( Rect.X, Rect.Y, Rect.X + Rect.Width, Rect.Y + Rect.Height, FColor32( 120, 130, 170 ), false, 1 );
	CommandList.Text( Rect.X + 8, Rect.Y + Rect.Height / 2 - 9, Label, bEnabled ? FColor32::White : FColor32( 110, 112, 124 ) );
}

void NInventoryUi::DrawTitle( FDrawContext& CommandList, int32 X, int32 Y, const char* Text )
{
	CommandList.Text( X, Y, Text, FColor32( 255, 224, 140 ) );
}
