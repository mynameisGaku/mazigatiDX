// SPDX-License-Identifier: MIT
#include "UIListView.h"

#include "Library/Core/Types/Numeric.h"

CUIListView::CUIListView()
{
	SetClipChildren( true );
	SetInteractive( true );
	SetFocusable( true );
	// クリック・決定は、選んでいる行のクリックとして届ける
	OnClicked.Add( [this]( CUIWidget& )
	{
		if ( bIgnoreNextClick )
		{
			bIgnoreNextClick = false;
			return;
		}
		OnItemClicked.Broadcast( *this, SelectedIndex );
	} );
}

void CUIListView::SetItemFactory( const FCreateItem& InCreate, const FBindItem& InBind )
{
	ReleaseAll();
	Pool.Empty();
	CreateItem = InCreate;
	BindItem = InBind;
	MarkLayoutDirty();
}

void CUIListView::SetItemCount( int32 InCount )
{
	const int32 Safe = NMath::Max( 0, InCount );
	if ( Safe == ItemCount )
	{
		return;
	}
	ItemCount = Safe;
	bOffsetsValid = false;
	ContentExtent = TotalExtent();
	const int32 OldSelection = SelectedIndex;
	if ( SelectedIndex >= ItemCount )
	{
		SelectedIndex = ItemCount - 1;
	}
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );
	MarkLayoutDirty();
	if ( SelectedIndex != OldSelection )
	{
		OnSelectionChanged.Broadcast( *this, SelectedIndex );
	}
}

void CUIListView::SetItemHeight( float32 InHeight )
{
	const float32 Safe = NMath::Max( 1.0f, InHeight );
	if ( Safe == ItemHeight )
	{
		return;
	}
	ItemHeight = Safe;
	bOffsetsValid = false;
	ContentExtent = TotalExtent();
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );
	MarkLayoutDirty();
}

void CUIListView::SetItemSizeProvider( const FItemExtent& InProvider )
{
	SizeProvider = InProvider;
	InvalidateItemSizes();
}

void CUIListView::InvalidateItemSizes()
{
	bOffsetsValid = false;
	ContentExtent = TotalExtent();
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );
	MarkLayoutDirty();
}

void CUIListView::SetOrientation( EUIOrientation InOrientation )
{
	if ( Orientation == InOrientation )
	{
		return;
	}
	Orientation = InOrientation;
	ScrollOffset = 0.0f;
	Drag = NUIScrollBar::FDrag();
	MarkLayoutDirty();
}

void CUIListView::EnsureOffsets() const
{
	if ( bOffsetsValid || !SizeProvider )
	{
		return;
	}
	ItemOffsets.Reset();
	float32 Total = 0.0f;
	for ( int32 Index = 0; Index < ItemCount; ++Index )
	{
		ItemOffsets.Add( Total );
		Total += NMath::Max( 1.0f, SizeProvider( Index ) );
	}
	ItemOffsets.Add( Total );
	bOffsetsValid = true;
}

float32 CUIListView::TotalExtent() const
{
	if ( SizeProvider )
	{
		EnsureOffsets();
		return ItemOffsets.Last();
	}
	return static_cast<float32>( ItemCount ) * ItemHeight;
}

float32 CUIListView::GetItemOffset( int32 Index ) const
{
	if ( Index < 0 || Index >= ItemCount )
	{
		return TotalExtent();
	}
	if ( SizeProvider )
	{
		EnsureOffsets();
		return ItemOffsets[Index];
	}
	return static_cast<float32>( Index ) * ItemHeight;
}

float32 CUIListView::GetItemExtent( int32 Index ) const
{
	if ( Index < 0 || Index >= ItemCount )
	{
		return 0.0f;
	}
	if ( SizeProvider )
	{
		EnsureOffsets();
		return ItemOffsets[Index + 1] - ItemOffsets[Index];
	}
	return ItemHeight;
}

int32 CUIListView::IndexAtOffset( float32 Offset ) const
{
	if ( ItemCount <= 0 )
	{
		return -1;
	}
	if ( Offset <= 0.0f )
	{
		return 0;
	}
	if ( !SizeProvider )
	{
		return NMath::Min( ItemCount - 1, static_cast<int32>( NMath::Floor( Offset / ItemHeight ) ) );
	}
	EnsureOffsets();
	// ItemOffsets[Index] <= Offset となる、最後の Index
	int32 Low = 0;
	int32 High = ItemCount - 1;
	while ( Low < High )
	{
		const int32 Middle = ( Low + High + 1 ) / 2;
		if ( ItemOffsets[Middle] <= Offset )
		{
			Low = Middle;
		}
		else
		{
			High = Middle - 1;
		}
	}
	return Low;
}

CUIWidget* CUIListView::FindRealized( int32 Index ) const
{
	for ( int32 Position = 0; Position < RealizedIndices.Num(); ++Position )
	{
		if ( RealizedIndices[Position] == Index )
		{
			return GetChildren()[Position].Get();
		}
	}
	return nullptr;
}

void CUIListView::BindIndex( int32 Index )
{
	CUIWidget* Item = FindRealized( Index );
	if ( Item != nullptr && BindItem )
	{
		BindItem( *Item, Index, Index == SelectedIndex );
	}
}

void CUIListView::ReleaseAll()
{
	while ( !RealizedIndices.IsEmpty() )
	{
		const TSharedPtr<CUIWidget> Item = GetChildren().Last();
		RemoveChild( Item.Get() );
		Pool.Add( Item );
		RealizedIndices.RemoveAt( RealizedIndices.Num() - 1 );
	}
}

void CUIListView::Refresh()
{
	bOffsetsValid = false;
	ContentExtent = TotalExtent();
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );
	for ( int32 Position = 0; Position < RealizedIndices.Num(); ++Position )
	{
		BindIndex( RealizedIndices[Position] );
	}
	MarkLayoutDirty();
}

void CUIListView::SetSelectedIndex( int32 InIndex, bool bNotify )
{
	const int32 Next = ItemCount == 0 ? -1 : NMath::Clamp( InIndex, -1, ItemCount - 1 );
	if ( Next == SelectedIndex )
	{
		return;
	}
	const int32 Old = SelectedIndex;
	SelectedIndex = Next;
	// 選択の見た目を、入れ直す（前の行と、新しい行）
	BindIndex( Old );
	BindIndex( Next );
	if ( Next >= 0 )
	{
		ScrollToIndex( Next );
	}
	if ( bNotify )
	{
		OnSelectionChanged.Broadcast( *this, SelectedIndex );
	}
}

void CUIListView::SetScrollOffset( float32 InOffset )
{
	const float32 Next = NMath::Clamp( InOffset, 0.0f, GetMaxScrollOffset() );
	if ( Next != ScrollOffset )
	{
		ScrollOffset = Next;
		MarkLayoutDirty();
	}
}

void CUIListView::ScrollToIndex( int32 Index )
{
	if ( Index < 0 || Index >= ItemCount )
	{
		return;
	}
	// 見える範囲は、配置のあとの値。配置の前（まだ 0）なら、最大を超えない範囲で、先頭へ寄せる
	const float32 Top = GetItemOffset( Index );
	const float32 Bottom = Top + GetItemExtent( Index );
	if ( Top < ScrollOffset )
	{
		SetScrollOffset( Top );
	}
	else if ( ViewportExtent > 0.0f && Bottom > ScrollOffset + ViewportExtent )
	{
		SetScrollOffset( Bottom - ViewportExtent );
	}
}

void CUIListView::SetScrollBar( const FUIBrush& InTrack, const FUIBrush& InThumb, float32 InWidth )
{
	TrackBrush = InTrack;
	ThumbBrush = InThumb;
	const float32 Safe = NMath::Max( 0.0f, InWidth );
	if ( ScrollBarWidth != Safe )
	{
		ScrollBarWidth = Safe;
		MarkLayoutDirty();
	}
}

FUIRect CUIListView::GetItemRect( int32 Index ) const
{
	const FUIRect View = GetViewRect();
	if ( Index < 0 || Index >= ItemCount )
	{
		return FUIRect( View.Position, FVector2( 0.0f, 0.0f ) );
	}
	const float32 BarSpace = IsScrollBarShown() ? ScrollBarWidth : 0.0f;
	const float32 Extent = GetItemExtent( Index );
	if ( IsVertical() )
	{
		return FUIRect( View.Left(), View.Top() + GetItemOffset( Index ) - ScrollOffset, NMath::Max( 0.0f, View.Size.x - BarSpace ), Extent );
	}
	return FUIRect( View.Left() + GetItemOffset( Index ) - ScrollOffset, View.Top(), Extent, NMath::Max( 0.0f, View.Size.y - BarSpace ) );
}

int32 CUIListView::GetIndexAt( const FVector2& Position ) const
{
	// スクロールバーの上は、行ではない
	FUIRect Area = GetViewRect();
	if ( IsScrollBarShown() )
	{
		Area = IsVertical() ? FUIRect( Area.Left(), Area.Top(), NMath::Max( 0.0f, Area.Size.x - ScrollBarWidth ), Area.Size.y ) : FUIRect( Area.Left(), Area.Top(), Area.Size.x, NMath::Max( 0.0f, Area.Size.y - ScrollBarWidth ) );
	}
	if ( !Area.Contains( Position ) )
	{
		return -1;
	}
	const float32 Along = IsVertical() ? Position.y - Area.Top() : Position.x - Area.Left();
	const float32 Offset = Along + ScrollOffset;
	if ( Offset < 0.0f || Offset >= TotalExtent() )
	{
		return -1;
	}
	return IndexAtOffset( Offset );
}

void CUIListView::ArrangeChildren( const FUIRect& ContentRect )
{
	ContentExtent = TotalExtent();
	ViewportExtent = IsVertical() ? ContentRect.Size.y : ContentRect.Size.x;
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );

	// 見える行の範囲
	int32 First = 0;
	int32 Last = -1;
	if ( ItemCount > 0 && ViewportExtent > 0.0f && CreateItem )
	{
		First = IndexAtOffset( ScrollOffset );
		Last = IndexAtOffset( ScrollOffset + ViewportExtent - 0.001f );
	}
	FirstIndex = First;

	// 範囲の外へ出た行を、使い回しの置き場へ
	for ( int32 Position = RealizedIndices.Num() - 1; Position >= 0; --Position )
	{
		const int32 Index = RealizedIndices[Position];
		if ( Index < First || Index > Last )
		{
			const TSharedPtr<CUIWidget> Item = GetChildren()[Position];
			RemoveChild( Item.Get() );
			Pool.Add( Item );
			RealizedIndices.RemoveAt( Position );
		}
	}

	// 範囲に入った行を、置き場から出す（無ければ作る）。新しい行は、ここで、ほしい大きさを決める（配置の前の、測る段階は、済んでいるため）
	for ( int32 Index = First; Index <= Last; ++Index )
	{
		if ( FindRealized( Index ) != nullptr )
		{
			continue;
		}
		TSharedPtr<CUIWidget> Item;
		if ( !Pool.IsEmpty() )
		{
			Item = Pool.Last();
			Pool.RemoveAt( Pool.Num() - 1 );
		}
		else
		{
			Item = CreateItem();
			++NumCreated;
		}
		if ( !Item )
		{
			continue;
		}
		AddChild( Item );
		RealizedIndices.Add( Index );
		BindIndex( Index );
		Item->Measure();
	}

	// 置く: 行ごとに、主軸は行の大きさ、交差軸はリストの幅（高さ。バーが出ていれば、その分だけ狭い）
	for ( int32 Position = 0; Position < RealizedIndices.Num(); ++Position )
	{
		CUIWidget& Item = *GetChildren()[Position];
		const FUIRect Slot = GetItemRect( RealizedIndices[Position] );
		Item.Arrange( FUIRect( Slot.Left(), Slot.Top(), Slot.Size.x, Slot.Size.y ) );
	}
}

bool CUIListView::OwnsPointerAt( const FVector2& Position ) const
{
	return IsScrollBarShown() && GetTrackRect().Contains( Position );
}

void CUIListView::OnPointerPressed( const FVector2& Position )
{
	bIgnoreNextClick = false;
	// スクロールバー（つまみのつかみ・溝のクリック）が先。行を選ばず、クリックにもしない
	float32 Offset = ScrollOffset;
	if ( NUIScrollBar::Press( GetTrackRect(), GetThumbRect(), Orientation, Position, ScrollOffset, ViewportExtent, GetMaxScrollOffset(), Drag, Offset ) )
	{
		SetScrollOffset( Offset );
		bIgnoreNextClick = true;
		return;
	}
	const int32 Index = GetIndexAt( Position );
	if ( Index >= 0 )
	{
		SetSelectedIndex( Index );
	}
}

void CUIListView::OnPointerDragged( const FVector2& Position )
{
	if ( Drag.bActive )
	{
		SetScrollOffset( NUIScrollBar::DragOffset( GetTrackRect(), GetThumbRect(), Orientation, Position, Drag, GetMaxScrollOffset() ) );
	}
}

void CUIListView::OnPointerReleased( const FVector2& )
{
	Drag = NUIScrollBar::FDrag();
}

bool CUIListView::OnNavigate( EUINavDirection Direction )
{
	const EUINavDirection Backward = IsVertical() ? EUINavDirection::Up : EUINavDirection::Left;
	const EUINavDirection Forward = IsVertical() ? EUINavDirection::Down : EUINavDirection::Right;
	if ( ItemCount == 0 || ( Direction != Backward && Direction != Forward ) )
	{
		return false;
	}
	const int32 Step = Direction == Forward ? 1 : -1;
	// 選択が無いときの最初の矢印は、先頭（上なら末尾）を選ぶ
	const int32 Target = SelectedIndex < 0 ? ( Step > 0 ? 0 : ItemCount - 1 ) : SelectedIndex + Step;
	if ( Target < 0 || Target >= ItemCount )
	{
		return false; // 端では、フォーカスを外へ動かす
	}
	SetSelectedIndex( Target );
	return true;
}

bool CUIListView::OnWheel( const FVector2&, float32 Delta )
{
	if ( GetMaxScrollOffset() <= 0.0f )
	{
		return false;
	}
	SetScrollOffset( ScrollOffset - Delta * WheelStep );
	return true;
}

FUIRect CUIListView::GetTrackRect() const
{
	return NUIScrollBar::MakeTrack( GetViewRect(), Orientation, ScrollBarWidth, IsScrollBarShown() );
}

FUIRect CUIListView::GetThumbRect() const
{
	return NUIScrollBar::MakeThumb( GetTrackRect(), Orientation, ContentExtent, ViewportExtent, ScrollOffset, GetMaxScrollOffset() );
}

void CUIListView::OnPaintOverlay( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( !IsScrollBarShown() )
	{
		return;
	}
	NUIBrush::Draw( CommandList, TrackBrush, GetTrackRect(), EffectiveOpacity );
	NUIBrush::Draw( CommandList, ThumbBrush, GetThumbRect(), EffectiveOpacity );
}
