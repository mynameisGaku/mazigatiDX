// SPDX-License-Identifier: MIT
#include "UIPanels.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	// Canvas の 1 つの軸の位置と大きさ。AnchorMin == AnchorMax なら点に置き、違えば広げる（FUILayoutProperties の説明を参照）
	void ResolveCanvasAxis( float32 AnchorMin, float32 AnchorMax, float32 OffsetStart, float32 OffsetEnd, float32 Pivot, float32 ParentStart, float32 ParentSize,
							float32 DesiredSize, float32 MinSize, float32 MaxSize, float32& OutPosition, float32& OutSize )
	{
		const float32 MinPoint = ParentStart + ParentSize * AnchorMin;
		const float32 MaxPoint = ParentStart + ParentSize * AnchorMax;
		if ( AnchorMin == AnchorMax )
		{
			OutSize = NMath::Clamp( OffsetEnd > 0.0f ? OffsetEnd : DesiredSize, MinSize, MaxSize );
			OutPosition = MinPoint + OffsetStart - OutSize * Pivot;
			return;
		}
		OutPosition = MinPoint + OffsetStart;
		OutSize = NMath::Clamp( NMath::Max( 0.0f, MaxPoint - OffsetEnd - OutPosition ), MinSize, MaxSize );
	}

	bool IsCollapsed( const CUIWidget& Widget )
	{
		return Widget.GetVisibility() == EUIVisibility::Collapsed;
	}
}

//-----------------------------------------------------------------------------
// CUICanvasPanel
//-----------------------------------------------------------------------------
FVector2 CUICanvasPanel::ComputeContentSize() const
{
	return FVector2( 0.0f, 0.0f );
}

void CUICanvasPanel::ArrangeChildren( const FUIRect& ContentRect )
{
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}

		const FUILayoutProperties& Layout = Child.GetLayout();
		// Canvas は、子の外側の余白を使わない。ほしい大きさは、余白を除いたもの
		const FVector2 Desired = Child.GetDesiredSize() - Layout.Margin.GetSize();
		FUIRect Final;
		ResolveCanvasAxis( Layout.AnchorMin.x, Layout.AnchorMax.x, Layout.Offsets.Left, Layout.Offsets.Right, Layout.Pivot.x, ContentRect.Position.x, ContentRect.Size.x,
						   Desired.x, Layout.MinSize.x, Layout.MaxSize.x, Final.Position.x, Final.Size.x );
		ResolveCanvasAxis( Layout.AnchorMin.y, Layout.AnchorMax.y, Layout.Offsets.Top, Layout.Offsets.Bottom, Layout.Pivot.y, ContentRect.Position.y, ContentRect.Size.y,
						   Desired.y, Layout.MinSize.y, Layout.MaxSize.y, Final.Position.y, Final.Size.y );
		Child.ArrangeExact( Final );
	}
}

//-----------------------------------------------------------------------------
// CUIStackPanel
//-----------------------------------------------------------------------------
void CUIStackPanel::SetOrientation( EUIOrientation InOrientation )
{
	if ( Orientation != InOrientation )
	{
		Orientation = InOrientation;
		MarkLayoutDirty();
	}
}

void CUIStackPanel::SetSpacing( float32 InSpacing )
{
	const float32 Safe = NMath::Max( 0.0f, InSpacing );
	if ( Spacing != Safe )
	{
		Spacing = Safe;
		MarkLayoutDirty();
	}
}

FVector2 CUIStackPanel::ComputeContentSize() const
{
	const bool bHorizontal = Orientation == EUIOrientation::Horizontal;
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();
	float32 Main = 0.0f;
	float32 Cross = 0.0f;
	int32 Count = 0;
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		const CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}
		const FVector2& Desired = Child.GetDesiredSize();
		Main += bHorizontal ? Desired.x : Desired.y;
		Cross = NMath::Max( Cross, bHorizontal ? Desired.y : Desired.x );
		++Count;
	}
	if ( Count > 1 )
	{
		Main += Spacing * static_cast<float32>( Count - 1 );
	}
	return bHorizontal ? FVector2( Main, Cross ) : FVector2( Cross, Main );
}

void CUIStackPanel::ArrangeChildren( const FUIRect& ContentRect )
{
	const bool bHorizontal = Orientation == EUIOrientation::Horizontal;
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();

	// 重みの無い子の大きさと間隔を引いた残りを、重みのある子で分ける
	float32 FixedMain = 0.0f;
	float32 TotalWeight = 0.0f;
	int32 Count = 0;
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		const CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}
		++Count;
		const float32 Weight = Child.GetLayout().Weight;
		if ( Weight > 0.0f )
		{
			TotalWeight += Weight;
		}
		else
		{
			FixedMain += bHorizontal ? Child.GetDesiredSize().x : Child.GetDesiredSize().y;
		}
	}
	const float32 AvailableMain = bHorizontal ? ContentRect.Size.x : ContentRect.Size.y;
	const float32 SpacingTotal = Count > 1 ? Spacing * static_cast<float32>( Count - 1 ) : 0.0f;
	const float32 Remaining = NMath::Max( 0.0f, AvailableMain - FixedMain - SpacingTotal );

	float32 Cursor = bHorizontal ? ContentRect.Left() : ContentRect.Top();
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}
		const float32 Weight = Child.GetLayout().Weight;
		const float32 SlotMain = ( Weight > 0.0f && TotalWeight > 0.0f ) ? Remaining * Weight / TotalWeight : ( bHorizontal ? Child.GetDesiredSize().x : Child.GetDesiredSize().y );
		const FUIRect Slot = bHorizontal ? FUIRect( Cursor, ContentRect.Top(), SlotMain, ContentRect.Size.y ) : FUIRect( ContentRect.Left(), Cursor, ContentRect.Size.x, SlotMain );
		Child.Arrange( Slot );
		Cursor += SlotMain + Spacing;
	}
}

//-----------------------------------------------------------------------------
// CUIGridPanel
//-----------------------------------------------------------------------------
void CUIGridPanel::SetColumns( const TArray<FUITrack>& InColumns )
{
	Columns = InColumns;
	MarkLayoutDirty();
}

void CUIGridPanel::SetRows( const TArray<FUITrack>& InRows )
{
	Rows = InRows;
	MarkLayoutDirty();
}

TArray<FUITrack> CUIGridPanel::MakeEffectiveTracks( const TArray<FUITrack>& Tracks )
{
	if ( Tracks.IsEmpty() )
	{
		TArray<FUITrack> Single;
		Single.Add( FUITrack::MakeWeight( 1.0f ) );
		return Single;
	}
	return Tracks;
}

void CUIGridPanel::ComputeContentSizes( bool bColumns, int32 TrackCount, TArray<float32>& OutSizes ) const
{
	OutSizes.Empty();
	for ( int32 Index = 0; Index < TrackCount; ++Index )
	{
		OutSizes.Add( 0.0f );
	}
	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		const CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}
		const FUILayoutProperties& Layout = Child.GetLayout();
		const int32 Span = bColumns ? Layout.ColumnSpan : Layout.RowSpan;
		if ( Span > 1 )
		{
			continue;
		}
		const int32 Track = NMath::Clamp( bColumns ? Layout.Column : Layout.Row, 0, TrackCount - 1 );
		OutSizes[Track] = NMath::Max( OutSizes[Track], bColumns ? Child.GetDesiredSize().x : Child.GetDesiredSize().y );
	}
}

FVector2 CUIGridPanel::ComputeContentSize() const
{
	const TArray<FUITrack> ColumnTracks = MakeEffectiveTracks( Columns );
	const TArray<FUITrack> RowTracks = MakeEffectiveTracks( Rows );
	TArray<float32> ColumnContent;
	TArray<float32> RowContent;
	ComputeContentSizes( true, ColumnTracks.Num(), ColumnContent );
	ComputeContentSizes( false, RowTracks.Num(), RowContent );

	float32 Width = 0.0f;
	for ( int32 Index = 0; Index < ColumnTracks.Num(); ++Index )
	{
		Width += ColumnTracks[Index].Kind == EUITrackKind::Fixed ? ColumnTracks[Index].Value : ColumnContent[Index];
	}
	float32 Height = 0.0f;
	for ( int32 Index = 0; Index < RowTracks.Num(); ++Index )
	{
		Height += RowTracks[Index].Kind == EUITrackKind::Fixed ? RowTracks[Index].Value : RowContent[Index];
	}
	return FVector2( Width, Height );
}

namespace
{
	// 1 つの軸の、列（行）ごとの大きさを決める。Fixed は画素、Auto は内容、Weight は残りを重みで分ける
	void ResolveTrackSizes( const TArray<FUITrack>& Tracks, const TArray<float32>& ContentSizes, float32 Available, TArray<float32>& OutSizes )
	{
		OutSizes.Empty();
		float32 Used = 0.0f;
		float32 TotalWeight = 0.0f;
		for ( int32 Index = 0; Index < Tracks.Num(); ++Index )
		{
			float32 Size = 0.0f;
			if ( Tracks[Index].Kind == EUITrackKind::Fixed )
			{
				Size = NMath::Max( 0.0f, Tracks[Index].Value );
			}
			else if ( Tracks[Index].Kind == EUITrackKind::Auto )
			{
				Size = ContentSizes[Index];
			}
			else
			{
				TotalWeight += NMath::Max( 0.0f, Tracks[Index].Value );
			}
			Used += Size;
			OutSizes.Add( Size );
		}
		const float32 Remaining = NMath::Max( 0.0f, Available - Used );
		for ( int32 Index = 0; Index < Tracks.Num(); ++Index )
		{
			if ( Tracks[Index].Kind == EUITrackKind::Weight && TotalWeight > 0.0f )
			{
				OutSizes[Index] = Remaining * NMath::Max( 0.0f, Tracks[Index].Value ) / TotalWeight;
			}
		}
	}

	// 列（行）の先頭の位置（先頭からの累積）
	void ComputeTrackStarts( const TArray<float32>& Sizes, float32 Origin, TArray<float32>& OutStarts )
	{
		OutStarts.Empty();
		float32 Cursor = Origin;
		for ( int32 Index = 0; Index < Sizes.Num(); ++Index )
		{
			OutStarts.Add( Cursor );
			Cursor += Sizes[Index];
		}
	}
}

void CUIGridPanel::ArrangeChildren( const FUIRect& ContentRect )
{
	const TArray<FUITrack> ColumnTracks = MakeEffectiveTracks( Columns );
	const TArray<FUITrack> RowTracks = MakeEffectiveTracks( Rows );
	TArray<float32> ColumnContent;
	TArray<float32> RowContent;
	ComputeContentSizes( true, ColumnTracks.Num(), ColumnContent );
	ComputeContentSizes( false, RowTracks.Num(), RowContent );

	TArray<float32> ColumnSizes;
	TArray<float32> RowSizes;
	ResolveTrackSizes( ColumnTracks, ColumnContent, ContentRect.Size.x, ColumnSizes );
	ResolveTrackSizes( RowTracks, RowContent, ContentRect.Size.y, RowSizes );
	TArray<float32> ColumnStarts;
	TArray<float32> RowStarts;
	ComputeTrackStarts( ColumnSizes, ContentRect.Left(), ColumnStarts );
	ComputeTrackStarts( RowSizes, ContentRect.Top(), RowStarts );

	const TArray<TSharedPtr<CUIWidget>>& Items = GetChildren();
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		CUIWidget& Child = *Items[Index];
		if ( IsCollapsed( Child ) )
		{
			continue;
		}
		const FUILayoutProperties& Layout = Child.GetLayout();
		const int32 FirstColumn = NMath::Clamp( Layout.Column, 0, ColumnSizes.Num() - 1 );
		const int32 LastColumn = NMath::Clamp( FirstColumn + NMath::Max( 1, Layout.ColumnSpan ) - 1, FirstColumn, ColumnSizes.Num() - 1 );
		const int32 FirstRow = NMath::Clamp( Layout.Row, 0, RowSizes.Num() - 1 );
		const int32 LastRow = NMath::Clamp( FirstRow + NMath::Max( 1, Layout.RowSpan ) - 1, FirstRow, RowSizes.Num() - 1 );

		const float32 X = ColumnStarts[FirstColumn];
		const float32 Y = RowStarts[FirstRow];
		const float32 Width = ColumnStarts[LastColumn] + ColumnSizes[LastColumn] - X;
		const float32 Height = RowStarts[LastRow] + RowSizes[LastRow] - Y;
		Child.Arrange( FUIRect( X, Y, Width, Height ) );
	}
}
