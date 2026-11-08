// SPDX-License-Identifier: MIT
#include "UIScrollBox.h"

#include "Library/Core/Types/Numeric.h"

CUIScrollBox::CUIScrollBox()
{
	SetClipChildren( true );
}

void CUIScrollBox::SetOrientation( EUIOrientation InOrientation )
{
	if ( Orientation != InOrientation )
	{
		Orientation = InOrientation;
		ScrollOffset = 0.0f;
		MarkLayoutDirty();
	}
}

void CUIScrollBox::SetScrollOffset( float32 InOffset )
{
	const float32 Next = NMath::Clamp( InOffset, 0.0f, GetMaxScrollOffset() );
	if ( Next != ScrollOffset )
	{
		ScrollOffset = Next;
		// 子の置き場所が替わる
		MarkLayoutDirty();
	}
}

void CUIScrollBox::SetScrollBar( const FUIBrush& InTrack, const FUIBrush& InThumb, float32 InWidth )
{
	TrackBrush = InTrack;
	ThumbBrush = InThumb;
	const float32 Safe = NMath::Max( 0.0f, InWidth );
	if ( ScrollBarWidth != Safe )
	{
		ScrollBarWidth = Safe;
		// バーが出ると、子の幅（高さ）が狭くなる
		MarkLayoutDirty();
	}
}

FVector2 CUIScrollBox::ComputeContentSize() const
{
	float32 Cross = 0.0f;
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		const CUIWidget& Child = *Items[Index];
		if ( Child.GetVisibility() != EUIVisibility::Collapsed )
		{
			Cross = NMath::Max( Cross, IsVertical() ? Child.GetDesiredSize().x : Child.GetDesiredSize().y );
		}
	}
	return IsVertical() ? FVector2( Cross, 0.0f ) : FVector2( 0.0f, Cross );
}

void CUIScrollBox::ArrangeChildren( const FUIRect& ContentRect )
{
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();

	// 中身の主軸の大きさ = 子のほしい大きさの最大
	float32 Extent = 0.0f;
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		const CUIWidget& Child = *Items[Index];
		if ( Child.GetVisibility() != EUIVisibility::Collapsed )
		{
			Extent = NMath::Max( Extent, IsVertical() ? Child.GetDesiredSize().y : Child.GetDesiredSize().x );
		}
	}
	ContentExtent = Extent;
	ViewportExtent = IsVertical() ? ContentRect.Size.y : ContentRect.Size.x;

	// 範囲に収める（中身や枠の大きさが変わって、いまのスクロール量が行き過ぎたとき）
	ScrollOffset = NMath::Clamp( ScrollOffset, 0.0f, GetMaxScrollOffset() );

	// スクロールバーが出るなら、交差軸を、バーの分だけ狭くする
	const float32 BarSpace = IsScrollBarShown() ? ScrollBarWidth : 0.0f;
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		CUIWidget& Child = *Items[Index];
		if ( Child.GetVisibility() == EUIVisibility::Collapsed )
		{
			continue;
		}
		// 子は、ほしい大きさ（枠より小さければ、枠の大きさ）で、スクロールの量だけずらして置く
		if ( IsVertical() )
		{
			const float32 Height = NMath::Max( Child.GetDesiredSize().y, ContentRect.Size.y );
			Child.Arrange( FUIRect( ContentRect.Left(), ContentRect.Top() - ScrollOffset, NMath::Max( 0.0f, ContentRect.Size.x - BarSpace ), Height ) );
		}
		else
		{
			const float32 Width = NMath::Max( Child.GetDesiredSize().x, ContentRect.Size.x );
			Child.Arrange( FUIRect( ContentRect.Left() - ScrollOffset, ContentRect.Top(), Width, NMath::Max( 0.0f, ContentRect.Size.y - BarSpace ) ) );
		}
	}
}

bool CUIScrollBox::OnWheel( const FVector2&, float32 Delta )
{
	if ( GetMaxScrollOffset() <= 0.0f )
	{
		return false; // 収まっているので、外側のスクロール枠へ
	}
	SetScrollOffset( ScrollOffset - Delta * WheelStep );
	return true;
}

void CUIScrollBox::ScrollIntoView( const FUIRect& Target )
{
	const FUIRect View = GetRect().Deflate( GetLayout().Padding );
	if ( IsVertical() )
	{
		if ( Target.Top() < View.Top() )
		{
			SetScrollOffset( ScrollOffset - ( View.Top() - Target.Top() ) );
		}
		else if ( Target.Bottom() > View.Bottom() )
		{
			SetScrollOffset( ScrollOffset + ( Target.Bottom() - View.Bottom() ) );
		}
	}
	else
	{
		if ( Target.Left() < View.Left() )
		{
			SetScrollOffset( ScrollOffset - ( View.Left() - Target.Left() ) );
		}
		else if ( Target.Right() > View.Right() )
		{
			SetScrollOffset( ScrollOffset + ( Target.Right() - View.Right() ) );
		}
	}
}

void CUIScrollBox::OnChildFocused( CUIWidget& Child )
{
	ScrollIntoView( Child.GetRect() );
}

FUIRect CUIScrollBox::GetTrackRect() const
{
	if ( !IsScrollBarShown() )
	{
		return FUIRect( GetRect().Position, FVector2( 0.0f, 0.0f ) );
	}
	const FUIRect View = GetRect().Deflate( GetLayout().Padding );
	if ( IsVertical() )
	{
		return FUIRect( View.Right() - ScrollBarWidth, View.Top(), ScrollBarWidth, View.Size.y );
	}
	return FUIRect( View.Left(), View.Bottom() - ScrollBarWidth, View.Size.x, ScrollBarWidth );
}

FUIRect CUIScrollBox::GetThumbRect() const
{
	const FUIRect Track = GetTrackRect();
	if ( Track.Size.x <= 0.0f || Track.Size.y <= 0.0f || ContentExtent <= 0.0f )
	{
		return FUIRect( Track.Position, FVector2( 0.0f, 0.0f ) );
	}
	const float32 TrackLength = IsVertical() ? Track.Size.y : Track.Size.x;
	// つまみの長さは、見えている割合（短すぎると見えないので、最小を設ける）
	const float32 ThumbLength = NMath::Min( TrackLength, NMath::Max( 24.0f, TrackLength * ViewportExtent / ContentExtent ) );
	const float32 Travel = TrackLength - ThumbLength;
	const float32 Ratio = GetMaxScrollOffset() > 0.0f ? ScrollOffset / GetMaxScrollOffset() : 0.0f;
	if ( IsVertical() )
	{
		return FUIRect( Track.Left(), Track.Top() + Travel * Ratio, Track.Size.x, ThumbLength );
	}
	return FUIRect( Track.Left() + Travel * Ratio, Track.Top(), ThumbLength, Track.Size.y );
}

void CUIScrollBox::OnPaintOverlay( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( !IsScrollBarShown() )
	{
		return;
	}
	NUIBrush::Draw( CommandList, TrackBrush, GetTrackRect(), EffectiveOpacity );
	NUIBrush::Draw( CommandList, ThumbBrush, GetThumbRect(), EffectiveOpacity );
}
