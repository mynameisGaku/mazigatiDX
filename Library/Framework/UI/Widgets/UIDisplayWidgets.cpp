// SPDX-License-Identifier: MIT
#include "UIDisplayWidgets.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"

namespace
{
	int32 FloorToInt( float32 Value )
	{
		return static_cast<int32>( std::floor( Value ) );
	}

	EDrawTextAlign ToTextAlign( EUIAlignment Alignment )
	{
		switch ( Alignment )
		{
		case EUIAlignment::Center:
			return EDrawTextAlign::Center;
		case EUIAlignment::End:
			return EDrawTextAlign::Right;
		case EUIAlignment::Start:
		case EUIAlignment::Stretch:
		default:
			return EDrawTextAlign::Left;
		}
	}
}

//-----------------------------------------------------------------------------
// CUIImage
//-----------------------------------------------------------------------------
void CUIImage::SetImage( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, EUIImageMode InMode )
{
	const FColor32 Tint = Content.Kind == EUIBrushKind::Image ? Content.Color : FColor32( 255, 255, 255, 255 );
	const FUIMargin Margin = Content.NineSliceMargin;
	Content = FUIBrush::MakeImage( InImage, InImageSize, InMode, Tint );
	Content.NineSliceMargin = Margin;
	MarkLayoutDirty();
}

void CUIImage::SetMode( EUIImageMode InMode )
{
	Content.ImageMode = InMode;
}

void CUIImage::SetNineSliceMargin( const FUIMargin& InMargin )
{
	Content.NineSliceMargin = InMargin;
}

void CUIImage::SetTint( const FColor32& InTint )
{
	Content.Color = InTint;
}

void CUIImage::SetContent( const FUIBrush& InContent )
{
	Content = InContent;
	MarkLayoutDirty();
}

FVector2 CUIImage::ComputeContentSize() const
{
	if ( Content.Kind == EUIBrushKind::Image && Content.Image )
	{
		return Content.ImageSize;
	}
	return FVector2( 0.0f, 0.0f );
}

void CUIImage::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	NUIBrush::Draw( CommandList, Content, GetRect().Deflate( GetLayout().Padding ), EffectiveOpacity );
}

//-----------------------------------------------------------------------------
// CUILabel
//-----------------------------------------------------------------------------
void CUILabel::SetText( const FString& InText )
{
	if ( Text != InText )
	{
		Text = InText;
		MarkLayoutDirty();
	}
}

void CUILabel::SetFont( const TSharedPtr<CFont>& InFont )
{
	Font = InFont;
	MarkLayoutDirty();
}

void CUILabel::SetMaxWidth( float32 InMaxWidth )
{
	const float32 Safe = NMath::Max( 0.0f, InMaxWidth );
	if ( MaxWidth != Safe )
	{
		MaxWidth = Safe;
		MarkLayoutDirty();
	}
}

void CUILabel::SetMaxLines( int32 InMaxLines )
{
	const int32 Safe = NMath::Max( 0, InMaxLines );
	if ( MaxLines != Safe )
	{
		MaxLines = Safe;
		MarkLayoutDirty();
	}
}

float32 CUILabel::GetBlockHeight( const FTextLayout& Layout ) const
{
	if ( Layout.Lines.IsEmpty() )
	{
		return 0.0f;
	}
	// 最後の行は、行の送り幅ではなく、文字の高さ（CFont::MeasureSize と同じ）
	return static_cast<float32>( ( Layout.Lines.Num() - 1 ) * Font->GetLineHeight() + Font->GetMetrics().GlyphHeight );
}

FTextLayout CUILabel::MakeLayout( float32 WrapWidth ) const
{
	FTextLayoutOptions Options;
	Options.MaxWidth = WrapWidth > 0.0f ? static_cast<int32>( WrapWidth ) : 0;
	Options.MaxLines = MaxLines;
	return Font->Layout( *Text, Options );
}

FVector2 CUILabel::ComputeContentSize() const
{
	if ( !Font || Text.IsEmpty() )
	{
		return FVector2( 0.0f, 0.0f );
	}
	const FTextLayout Layout = MakeLayout( MaxWidth );
	return FVector2( static_cast<float32>( Layout.Width ), GetBlockHeight( Layout ) );
}

void CUILabel::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( !Font || Text.IsEmpty() )
	{
		return;
	}
	const int32 Alpha = NUIBrush::ToAlpha( Color, EffectiveOpacity );
	if ( Alpha <= 0 )
	{
		return;
	}

	const FUIRect Box = GetRect().Deflate( GetLayout().Padding );
	// 折り返す幅は、指定があればそれ（箱が狭ければ、箱の幅）。指定が無ければ折り返さない
	const float32 WrapWidth = MaxWidth > 0.0f ? NMath::Min( MaxWidth, Box.Size.x ) : 0.0f;
	const FTextLayout Layout = MakeLayout( WrapWidth );
	if ( Layout.Lines.IsEmpty() )
	{
		return;
	}

	const float32 BlockHeight = GetBlockHeight( Layout );
	float32 Y = Box.Top();
	if ( VerticalTextAlignment == EUIAlignment::Center )
	{
		Y = Box.Top() + ( Box.Size.y - BlockHeight ) * 0.5f;
	}
	else if ( VerticalTextAlignment == EUIAlignment::End )
	{
		Y = Box.Bottom() - BlockHeight;
	}

	CommandList.Blend( EDrawBlend::Alpha, Alpha );
	Font->RecordLayout( CommandList, Layout, FloorToInt( Box.Left() ), FloorToInt( Y ), FloorToInt( Box.Size.x ), Color, ToTextAlign( HorizontalTextAlignment ), EdgeColor );
}

//-----------------------------------------------------------------------------
// CUIProgressBar
//-----------------------------------------------------------------------------
FUIRect CUIProgressBar::GetFillRect() const
{
	const FUIRect Area = GetRect().Deflate( FillInset );
	if ( Orientation == EUIOrientation::Horizontal )
	{
		return FUIRect( Area.Position.x, Area.Position.y, Area.Size.x * Value, Area.Size.y );
	}
	const float32 Height = Area.Size.y * Value;
	return FUIRect( Area.Position.x, Area.Bottom() - Height, Area.Size.x, Height );
}

void CUIProgressBar::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	NUIBrush::Draw( CommandList, BackBrush, GetRect(), EffectiveOpacity );
	if ( Value > 0.0f )
	{
		NUIBrush::Draw( CommandList, FillBrush, GetFillRect(), EffectiveOpacity );
	}
}
