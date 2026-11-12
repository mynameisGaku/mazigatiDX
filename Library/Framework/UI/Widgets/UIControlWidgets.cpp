// SPDX-License-Identifier: MIT
#include "UIControlWidgets.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

//-----------------------------------------------------------------------------
// CUIButton
//-----------------------------------------------------------------------------
CUIButton::CUIButton()
{
	SetInteractive( true );
	SetFocusable( true );
}

const FUIBrush& CUIButton::GetCurrentBrush() const
{
	if ( !IsEffectivelyEnabled() )
	{
		return Style.Disabled;
	}
	if ( IsPressed() )
	{
		return Style.Pressed;
	}
	if ( IsHovered() )
	{
		return Style.Hover;
	}
	return Style.Normal;
}

int32 CUIButton::GetStateIndex() const
{
	if ( !IsEffectivelyEnabled() )
	{
		return 3;
	}
	if ( IsPressed() )
	{
		return 2;
	}
	return IsHovered() ? 1 : 0;
}

const FUIBrush& CUIButton::GetStateBrush( int32 StateIndex ) const
{
	switch ( StateIndex )
	{
		case 1:
			return Style.Hover;
		case 2:
			return Style.Pressed;
		case 3:
			return Style.Disabled;
		default:
			return Style.Normal;
	}
}

void CUIButton::OnAdvanceTime( float32 DeltaTime )
{
	StateFade.Advance( GetStateIndex(), DeltaTime, Style.TransitionDuration );
	FocusFade.Advance( IsFocused(), DeltaTime, Style.TransitionDuration );
}

void CUIButton::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( Style.TransitionDuration > 0.0f && StateFade.IsStarted() )
	{
		// 切り替えを、時間をかけて混ぜる
		if ( StateFade.IsFading() )
		{
			NUIBrush::DrawFade( CommandList, GetStateBrush( StateFade.Previous ), GetStateBrush( StateFade.Current ), StateFade.Progress, GetRect(), EffectiveOpacity );
		}
		else
		{
			NUIBrush::Draw( CommandList, GetStateBrush( StateFade.Current ), GetRect(), EffectiveOpacity );
		}
		NUIBrush::Draw( CommandList, Style.FocusOverlay, GetRect(), EffectiveOpacity * FocusFade.Amount );
		return;
	}
	NUIBrush::Draw( CommandList, GetCurrentBrush(), GetRect(), EffectiveOpacity );
	if ( IsFocused() )
	{
		NUIBrush::Draw( CommandList, Style.FocusOverlay, GetRect(), EffectiveOpacity );
	}
}

//-----------------------------------------------------------------------------
// CUICheckBox
//-----------------------------------------------------------------------------
CUICheckBox::CUICheckBox()
{
	SetInteractive( true );
	SetFocusable( true );
	OnClicked.Add( [this]( CUIWidget& ) { SetChecked( !bChecked ); } );
}

void CUICheckBox::SetChecked( bool bInChecked, bool bNotify )
{
	if ( bChecked == bInChecked )
	{
		return;
	}
	bChecked = bInChecked;
	if ( bNotify )
	{
		OnToggled.Broadcast( *this, bChecked );
	}
}

void CUICheckBox::OnAdvanceTime( float32 DeltaTime )
{
	const bool bEnabled = IsEffectivelyEnabled();
	CheckFade.Advance( bChecked ? 1 : 0, DeltaTime, Style.TransitionDuration );
	HoverFade.Advance( bEnabled && IsHovered(), DeltaTime, Style.TransitionDuration );
	DisabledFade.Advance( !bEnabled, DeltaTime, Style.TransitionDuration );
	FocusFade.Advance( IsFocused(), DeltaTime, Style.TransitionDuration );
}

void CUICheckBox::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	if ( Style.TransitionDuration > 0.0f && CheckFade.IsStarted() )
	{
		// 切り替えを、時間をかけて混ぜる
		const FUIBrush& Current = CheckFade.Current == 1 ? Style.On : Style.Off;
		if ( CheckFade.IsFading() )
		{
			NUIBrush::DrawFade( CommandList, CheckFade.Previous == 1 ? Style.On : Style.Off, Current, CheckFade.Progress, GetRect(), EffectiveOpacity );
		}
		else
		{
			NUIBrush::Draw( CommandList, Current, GetRect(), EffectiveOpacity );
		}
		NUIBrush::Draw( CommandList, Style.DisabledOverlay, GetRect(), EffectiveOpacity * DisabledFade.Amount );
		NUIBrush::Draw( CommandList, Style.HoverOverlay, GetRect(), EffectiveOpacity * HoverFade.Amount );
		NUIBrush::Draw( CommandList, Style.FocusOverlay, GetRect(), EffectiveOpacity * FocusFade.Amount );
		return;
	}
	NUIBrush::Draw( CommandList, bChecked ? Style.On : Style.Off, GetRect(), EffectiveOpacity );
	if ( !IsEffectivelyEnabled() )
	{
		NUIBrush::Draw( CommandList, Style.DisabledOverlay, GetRect(), EffectiveOpacity );
	}
	else if ( IsHovered() )
	{
		NUIBrush::Draw( CommandList, Style.HoverOverlay, GetRect(), EffectiveOpacity );
	}
	if ( IsFocused() )
	{
		NUIBrush::Draw( CommandList, Style.FocusOverlay, GetRect(), EffectiveOpacity );
	}
}

//-----------------------------------------------------------------------------
// CUISlider
//-----------------------------------------------------------------------------
CUISlider::CUISlider()
{
	SetInteractive( true );
	SetFocusable( true );
}

void CUISlider::SetRange( float32 InMinimum, float32 InMaximum )
{
	Minimum = InMinimum;
	Maximum = NMath::Max( InMinimum, InMaximum );
	SetValue( Value );
}

float32 CUISlider::GetNormalizedValue() const
{
	return Maximum > Minimum ? NMath::Clamp( ( Value - Minimum ) / ( Maximum - Minimum ), 0.0f, 1.0f ) : 0.0f;
}

void CUISlider::SetValue( float32 InValue, bool bNotify )
{
	float32 Next = NMath::Clamp( InValue, Minimum, Maximum );
	if ( Step > 0.0f )
	{
		// Minimum から Step ずつの刻みに丸める（最後が Maximum に届かない刻みでも、Maximum を超えない）
		Next = Minimum + NMath::Round( ( Next - Minimum ) / Step ) * Step;
		Next = NMath::Clamp( Next, Minimum, Maximum );
	}
	if ( Next == Value )
	{
		return;
	}
	Value = Next;
	if ( bNotify )
	{
		OnValueChanged.Broadcast( *this, Value );
	}
}

FUIRect CUISlider::GetHandleRect() const
{
	const FUIRect Box = GetRect();
	const float32 T = GetNormalizedValue();
	const FVector2 Size = Style.HandleSize;
	if ( Orientation == EUIOrientation::Horizontal )
	{
		const float32 Usable = NMath::Max( 0.0f, Box.Size.x - Size.x );
		return FUIRect( Box.Left() + T * Usable, Box.Top() + ( Box.Size.y - Size.y ) * 0.5f, Size.x, Size.y );
	}
	const float32 Usable = NMath::Max( 0.0f, Box.Size.y - Size.y );
	return FUIRect( Box.Left() + ( Box.Size.x - Size.x ) * 0.5f, Box.Bottom() - Size.y - T * Usable, Size.x, Size.y );
}

FUIRect CUISlider::GetFillRect() const
{
	const FUIRect Area = GetRect().Deflate( Style.TrackInset ).Deflate( Style.FillInset );
	const FVector2 Center = GetHandleRect().GetCenter();
	if ( Orientation == EUIOrientation::Horizontal )
	{
		return FUIRect( Area.Left(), Area.Top(), NMath::Clamp( Center.x - Area.Left(), 0.0f, Area.Size.x ), Area.Size.y );
	}
	const float32 Height = NMath::Clamp( Area.Bottom() - Center.y, 0.0f, Area.Size.y );
	return FUIRect( Area.Left(), Area.Bottom() - Height, Area.Size.x, Height );
}

void CUISlider::SetFromPosition( const FVector2& Position )
{
	const FUIRect Box = GetRect();
	float32 T = 0.0f;
	if ( Orientation == EUIOrientation::Horizontal )
	{
		const float32 Usable = Box.Size.x - Style.HandleSize.x;
		T = Usable > 0.0f ? ( Position.x - Box.Left() - Style.HandleSize.x * 0.5f ) / Usable : 0.0f;
	}
	else
	{
		const float32 Usable = Box.Size.y - Style.HandleSize.y;
		T = Usable > 0.0f ? 1.0f - ( Position.y - Box.Top() - Style.HandleSize.y * 0.5f ) / Usable : 0.0f;
	}
	T = NMath::Clamp( T, 0.0f, 1.0f );
	SetValue( Minimum + T * ( Maximum - Minimum ) );
}

void CUISlider::OnPointerPressed( const FVector2& Position )
{
	SetFromPosition( Position );
}

void CUISlider::OnPointerDragged( const FVector2& Position )
{
	SetFromPosition( Position );
}

bool CUISlider::OnNavigate( EUINavDirection Direction )
{
	const bool bHorizontal = Orientation == EUIOrientation::Horizontal;
	float32 Sign = 0.0f;
	if ( bHorizontal )
	{
		Sign = Direction == EUINavDirection::Right ? 1.0f : ( Direction == EUINavDirection::Left ? -1.0f : 0.0f );
	}
	else
	{
		Sign = Direction == EUINavDirection::Up ? 1.0f : ( Direction == EUINavDirection::Down ? -1.0f : 0.0f );
	}
	if ( Sign == 0.0f )
	{
		return false; // 向きが違う矢印は、フォーカスの移動へ
	}
	const float32 Amount = Step > 0.0f ? Step : ( Maximum - Minimum ) * 0.05f;
	SetValue( Value + Sign * Amount );
	return true;
}

void CUISlider::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	const FUIRect Track = GetRect().Deflate( Style.TrackInset );
	NUIBrush::Draw( CommandList, Style.Track, Track, EffectiveOpacity );

	const FUIRect Handle = GetHandleRect();
	if ( !Style.Fill.IsEmpty() )
	{
		// 中身は、溝の端から、つまみの中心まで（溝の枠の内側）
		NUIBrush::Draw( CommandList, Style.Fill, GetFillRect(), EffectiveOpacity );
	}

	if ( Style.TransitionDuration > 0.0f && HandleFade.IsStarted() )
	{
		// つまみの切り替えを、時間をかけて混ぜる
		const FUIBrush& Current = HandleFade.Current == 1 ? Style.HandleHover : Style.Handle;
		if ( HandleFade.IsFading() )
		{
			NUIBrush::DrawFade( CommandList, HandleFade.Previous == 1 ? Style.HandleHover : Style.Handle, Current, HandleFade.Progress, Handle, EffectiveOpacity );
		}
		else
		{
			NUIBrush::Draw( CommandList, Current, Handle, EffectiveOpacity );
		}
		return;
	}
	NUIBrush::Draw( CommandList, GetHandleStateIndex() == 1 ? Style.HandleHover : Style.Handle, Handle, EffectiveOpacity );
}

int32 CUISlider::GetHandleStateIndex() const
{
	return ( ( IsHovered() || IsPressed() ) && !Style.HandleHover.IsEmpty() ) ? 1 : 0;
}

void CUISlider::OnAdvanceTime( float32 DeltaTime )
{
	HandleFade.Advance( GetHandleStateIndex(), DeltaTime, Style.TransitionDuration );
}
