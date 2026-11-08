// SPDX-License-Identifier: MIT
#include "UIListView.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"

CUIListView::CUIListView()
{
	SetClipChildren( true );
	SetInteractive( true );
	SetFocusable( true );
	// クリック・決定は、選んでいる行のクリックとして届ける
	OnClicked.Add( [this]( CUIWidget& ) { OnItemClicked.Broadcast( *this, SelectedIndex ); } );
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
	ContentExtent = static_cast<float32>( ItemCount ) * ItemHeight;
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
	ContentExtent = static_cast<float32>( ItemCount ) * ItemHeight;
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );
	MarkLayoutDirty();
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
	const float32 Top = static_cast<float32>( Index ) * ItemHeight;
	const float32 Bottom = Top + ItemHeight;
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
	return FUIRect( View.Left(), View.Top() + static_cast<float32>( Index ) * ItemHeight - ScrollOffset, NMath::Max( 0.0f, View.Size.x - BarSpace ), ItemHeight );
}

int32 CUIListView::GetIndexAt( const FVector2& Position ) const
{
	const FUIRect View = GetViewRect();
	if ( !View.Contains( Position ) )
	{
		return -1;
	}
	const int32 Index = static_cast<int32>( std::floor( ( Position.y - View.Top() + ScrollOffset ) / ItemHeight ) );
	return ( Index >= 0 && Index < ItemCount ) ? Index : -1;
}

void CUIListView::ArrangeChildren( const FUIRect& ContentRect )
{
	ContentExtent = static_cast<float32>( ItemCount ) * ItemHeight;
	ViewportExtent = ContentRect.Size.y;
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );

	// 見える行の範囲
	int32 First = 0;
	int32 Last = -1;
	if ( ItemCount > 0 && ViewportExtent > 0.0f && CreateItem )
	{
		First = static_cast<int32>( std::floor( ScrollOffset / ItemHeight ) );
		Last = NMath::Min( ItemCount - 1, static_cast<int32>( std::floor( ( ScrollOffset + ViewportExtent - 0.001f ) / ItemHeight ) ) );
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

	// 置く: 行ごとに、高さは ItemHeight、幅はリストの幅（バーが出ていれば、その分だけ狭い）
	for ( int32 Position = 0; Position < RealizedIndices.Num(); ++Position )
	{
		CUIWidget& Item = *GetChildren()[Position];
		const FUIRect Slot = GetItemRect( RealizedIndices[Position] );
		Item.Arrange( FUIRect( Slot.Left(), Slot.Top(), Slot.Size.x, Slot.Size.y ) );
	}
}

void CUIListView::OnPointerPressed( const FVector2& Position )
{
	const int32 Index = GetIndexAt( Position );
	if ( Index >= 0 )
	{
		SetSelectedIndex( Index );
	}
}

bool CUIListView::OnNavigate( EUINavDirection Direction )
{
	if ( ItemCount == 0 || ( Direction != EUINavDirection::Up && Direction != EUINavDirection::Down ) )
	{
		return false;
	}
	const int32 Step = Direction == EUINavDirection::Down ? 1 : -1;
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
	if ( !IsScrollBarShown() )
	{
		return FUIRect( GetRect().Position, FVector2( 0.0f, 0.0f ) );
	}
	const FUIRect View = GetViewRect();
	return FUIRect( View.Right() - ScrollBarWidth, View.Top(), ScrollBarWidth, View.Size.y );
}

FUIRect CUIListView::GetThumbRect() const
{
	const FUIRect Track = GetTrackRect();
	if ( Track.Size.x <= 0.0f || ContentExtent <= 0.0f )
	{
		return FUIRect( Track.Position, FVector2( 0.0f, 0.0f ) );
	}
	const float32 ThumbLength = NMath::Min( Track.Size.y, NMath::Max( 24.0f, Track.Size.y * ViewportExtent / ContentExtent ) );
	const float32 Ratio = GetMaxScrollOffset() > 0.0f ? ScrollOffset / GetMaxScrollOffset() : 0.0f;
	return FUIRect( Track.Left(), Track.Top() + ( Track.Size.y - ThumbLength ) * Ratio, Track.Size.x, ThumbLength );
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
