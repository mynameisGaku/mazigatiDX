// SPDX-License-Identifier: MIT
#include "UITextBox.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"

namespace
{
	// UTF-8 の、文字の先頭のバイトか（続きのバイト 10xxxxxx ではない）
	bool IsLeadByte( char Byte )
	{
		return ( static_cast<unsigned char>( Byte ) & 0xC0 ) != 0x80;
	}

	// 先頭 ByteOffset バイトに含まれる文字の数
	int32 CountChars( const FString& Text, int32 ByteOffset )
	{
		const char* Data = *Text;
		const int32 Limit = NMath::Min( ByteOffset, Text.Len() );
		int32 Count = 0;
		for ( int32 Index = 0; Index < Limit; ++Index )
		{
			Count += IsLeadByte( Data[Index] ) ? 1 : 0;
		}
		return Count;
	}

	// ByteOffset の次の文字の境目（末尾なら、末尾）
	int32 NextBoundary( const FString& Text, int32 ByteOffset )
	{
		const char* Data = *Text;
		int32 Index = NMath::Min( ByteOffset + 1, Text.Len() );
		while ( Index < Text.Len() && !IsLeadByte( Data[Index] ) )
		{
			++Index;
		}
		return Index;
	}

	// * を Count 個（パスワードの表示）
	FString Repeat( char Character, int32 Count )
	{
		FString Text;
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Text += Character;
		}
		return Text;
	}
}

CUITextBox::CUITextBox()
{
	SetInteractive( true );
	SetFocusable( true );
	// フォーカスを得たときの文字列を覚える（Escape で、ここへ戻す）。点滅も、見える所から始める
	OnFocusGained.Add( [this]( CUIWidget& )
	{
		TextAtFocus = Buffer.GetText();
		PaintCounter = 0;
	} );
	OnFocusLost.Add( [this]( CUIWidget& )
	{
		CompositionText.Empty();
		CompositionCursor = 0;
	} );
}

void CUITextBox::SetFont( const TSharedPtr<CFont>& InFont )
{
	Font = InFont;
	MarkLayoutDirty();
}

void CUITextBox::SetText( const FString& InText, bool bNotify )
{
	const FString Before = Buffer.GetText();
	Buffer.SetText( InText );
	ScrollX = 0.0f;
	PaintCounter = 0;
	if ( bNotify && Buffer.GetText() != Before )
	{
		OnTextChanged.Broadcast( *this, Buffer.GetText() );
	}
}

void CUITextBox::SetCursorByteOffset( int32 InOffset )
{
	Buffer.SetCursorByteOffset( InOffset );
	PaintCounter = 0;
}

bool CUITextBox::IsCaretVisible() const
{
	if ( !IsFocused() )
	{
		return false;
	}
	return CaretBlinkFrames <= 0 || ( ( PaintCounter / CaretBlinkFrames ) % 2 ) == 0;
}

FString CUITextBox::GetDisplayText( int32& OutCursorByteOffset ) const
{
	const FString& Text = Buffer.GetText();
	const int32 Cursor = Buffer.GetCursorByteOffset();
	FString Shown = Text;
	int32 ShownCursor = Cursor;
	if ( bPasswordMode )
	{
		// 文字ごとに * 1 つ（バイト数は、文字数と同じになる）
		Shown = Repeat( '*', CountChars( Text, Text.Len() ) );
		ShownCursor = CountChars( Text, Cursor );
	}
	if ( !CompositionText.IsEmpty() )
	{
		Shown = Shown.Left( ShownCursor ) + CompositionText + Shown.Mid( ShownCursor );
		ShownCursor += NMath::Clamp( CompositionCursor, 0, CompositionText.Len() );
	}
	OutCursorByteOffset = ShownCursor;
	return Shown;
}

float32 CUITextBox::PrefixWidth( int32 ByteOffset ) const
{
	if ( !Font || ByteOffset <= 0 )
	{
		return 0.0f;
	}
	const FString& Text = Buffer.GetText();
	const FString Prefix = bPasswordMode ? Repeat( '*', CountChars( Text, ByteOffset ) ) : Text.Left( ByteOffset );
	return static_cast<float32>( Font->MeasureWidth( Prefix ) );
}

FVector2 CUITextBox::ComputeContentSize() const
{
	if ( !Font )
	{
		return FVector2( 0.0f, 0.0f );
	}
	return FVector2( 0.0f, static_cast<float32>( Font->GetMetrics().GlyphHeight ) );
}

bool CUITextBox::OnTextInput( const TArray<FTextInputEvent>& Events, const FImeComposition* Ime )
{
	if ( Ime != nullptr && Ime->bActive )
	{
		CompositionText = Ime->Text;
		CompositionCursor = Ime->CursorByteOffset;
	}
	else
	{
		CompositionText.Empty();
		CompositionCursor = 0;
	}

	const FString Before = Buffer.GetText();
	const int32 CursorBefore = Buffer.GetCursorByteOffset();
	Buffer.ApplyAll( Events );
	if ( Buffer.GetText() != Before || Buffer.GetCursorByteOffset() != CursorBefore )
	{
		PaintCounter = 0; // 動いた直後は、カーソルを見せる
	}
	if ( Buffer.GetText() != Before )
	{
		OnTextChanged.Broadcast( *this, Buffer.GetText() );
	}

	// 確定・取り消し: どちらも、フォーカスを手放す
	const bool bSubmitted = Buffer.ConsumeSubmitted();
	const bool bCancelled = Buffer.ConsumeCancelled();
	if ( bCancelled )
	{
		const bool bRestore = Buffer.GetText() != TextAtFocus;
		Buffer.SetText( TextAtFocus );
		if ( bRestore )
		{
			OnTextChanged.Broadcast( *this, Buffer.GetText() );
		}
		OnCancelled.Broadcast( *this );
		return true;
	}
	if ( bSubmitted )
	{
		OnSubmitted.Broadcast( *this, Buffer.GetText() );
		return true;
	}
	return false;
}

void CUITextBox::OnPointerPressed( const FVector2& Position )
{
	if ( !Font )
	{
		return;
	}
	// クリックした位置にいちばん近い、文字の境目へ
	const FString& Text = Buffer.GetText();
	const float32 X = Position.x - ( GetTextArea().Left() - ScrollX );
	int32 Best = 0;
	float32 BestDistance = NMath::Abs( X );
	int32 Offset = 0;
	while ( Offset < Text.Len() )
	{
		Offset = NextBoundary( Text, Offset );
		const float32 Distance = NMath::Abs( PrefixWidth( Offset ) - X );
		if ( Distance < BestDistance )
		{
			BestDistance = Distance;
			Best = Offset;
		}
	}
	Buffer.SetCursorByteOffset( Best );
	PaintCounter = 0;
}

void CUITextBox::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	const bool bEnabled = IsEffectivelyEnabled();
	NUIBrush::Draw( CommandList, !bEnabled ? Style.Disabled : ( IsFocused() ? Style.Focused : Style.Normal ), GetRect(), EffectiveOpacity );
	if ( !Font )
	{
		return;
	}

	const FUIRect Area = GetTextArea();
	if ( Area.Size.x <= 0.0f || Area.Size.y <= 0.0f )
	{
		return;
	}
	int32 DisplayCursor = 0;
	const FString Display = GetDisplayText( DisplayCursor );
	const float32 GlyphHeight = static_cast<float32>( Font->GetMetrics().GlyphHeight );
	const float32 TextY = Area.Top() + ( Area.Size.y - GlyphHeight ) * 0.5f;

	// カーソルの位置（欄の左端から。ずらす前）を測って、見える位置まで、横へずらす
	const float32 CaretOffset = Display.IsEmpty() ? 0.0f : static_cast<float32>( Font->MeasureWidth( Display.Left( DisplayCursor ) ) );
	if ( IsFocused() )
	{
		if ( CaretOffset - ScrollX > Area.Size.x - 2.0f )
		{
			ScrollX = CaretOffset - ( Area.Size.x - 2.0f );
		}
		if ( CaretOffset < ScrollX )
		{
			ScrollX = CaretOffset;
		}
		ScrollX = NMath::Max( 0.0f, ScrollX );
	}
	else
	{
		ScrollX = 0.0f;
	}

	// 文字・下線・カーソルは、欄の矩形でクリップ
	FClipScope Clip( CommandList, static_cast<int32>( std::floor( Area.Left() ) ), static_cast<int32>( std::floor( Area.Top() ) ), static_cast<int32>( std::ceil( Area.Right() ) ),
					 static_cast<int32>( std::ceil( Area.Bottom() ) ) );
	const float32 OriginX = Area.Left() - ScrollX;

	// 文字（空で、フォーカスも無ければ、薄い案内の文字）
	const bool bShowPlaceholder = Display.IsEmpty() && !IsFocused() && !Placeholder.IsEmpty();
	const FString& Shown = bShowPlaceholder ? Placeholder : Display;
	const FColor32 TextColor = !bEnabled ? Style.DisabledTextColor : ( bShowPlaceholder ? Style.PlaceholderColor : Style.TextColor );
	if ( !Shown.IsEmpty() )
	{
		const int32 Alpha = NUIBrush::ToAlpha( TextColor, EffectiveOpacity );
		if ( Alpha > 0 )
		{
			CommandList.Blend( EDrawBlend::Alpha, Alpha );
			FTextLayoutOptions Options;
			const FTextLayout Layout = Font->Layout( *Shown, Options );
			Font->RecordLayout( CommandList, Layout, static_cast<int32>( std::floor( OriginX ) ), static_cast<int32>( std::floor( TextY ) ), Font->MeasureWidth( Shown ) + 2, TextColor, EDrawTextAlign::Left,
								FColor32( 0, 0, 0, 0 ) );
		}
	}

	// かな漢字変換の途中の文字の下線
	if ( !CompositionText.IsEmpty() && IsFocused() )
	{
		const int32 CompositionStart = Buffer.GetCursorByteOffset();
		const FString Masked = bPasswordMode ? Repeat( '*', CountChars( Buffer.GetText(), CompositionStart ) ) : Buffer.GetText().Left( CompositionStart );
		const float32 StartX = OriginX + static_cast<float32>( Font->MeasureWidth( Masked ) );
		const float32 EndX = StartX + static_cast<float32>( Font->MeasureWidth( CompositionText ) );
		const int32 Alpha = NUIBrush::ToAlpha( Style.TextColor, EffectiveOpacity );
		CommandList.Blend( EDrawBlend::Alpha, Alpha );
		CommandList.BoxAA( StartX, TextY + GlyphHeight, EndX, TextY + GlyphHeight + 1.0f, Style.TextColor, true );
	}

	// カーソル
	if ( IsFocused() && bEnabled )
	{
		if ( IsCaretVisible() )
		{
			const int32 Alpha = NUIBrush::ToAlpha( Style.CaretColor, EffectiveOpacity );
			if ( Alpha > 0 )
			{
				const float32 CaretX = OriginX + CaretOffset;
				CommandList.Blend( EDrawBlend::Alpha, Alpha );
				CommandList.BoxAA( CaretX, TextY, CaretX + 2.0f, TextY + GlyphHeight, Style.CaretColor, true );
			}
		}
		++PaintCounter;
	}
}
