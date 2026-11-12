// SPDX-License-Identifier: MIT
#include "UITextBox.h"

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
		bDragSelecting = false;
		Buffer.ClearSelection();
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
	ScrollY = 0.0f;
	MarkCaretMoved();
	if ( bNotify && Buffer.GetText() != Before )
	{
		OnTextChanged.Broadcast( *this, Buffer.GetText() );
	}
}

void CUITextBox::SetSelection( int32 InAnchorByteOffset, int32 InCursorByteOffset )
{
	Buffer.SetSelection( InAnchorByteOffset, InCursorByteOffset );
	MarkCaretMoved();
}

void CUITextBox::SelectAll()
{
	Buffer.SelectAll();
	MarkCaretMoved();
}

bool CUITextBox::CopySelection()
{
	// パスワードの欄は、中身をクリップボードへ出さない
	if ( Clipboard == nullptr || bPasswordMode || !Buffer.HasSelection() )
	{
		return false;
	}
	return Clipboard->SetText( Buffer.GetSelectedText() );
}

void CUITextBox::SetCursorByteOffset( int32 InOffset )
{
	Buffer.SetCursorByteOffset( InOffset );
	MarkCaretMoved();
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

void CUITextBox::SetMultiline( bool bInMultiline )
{
	Buffer.SetMultiline( bInMultiline );
	ScrollY = 0.0f;
	MarkCaretMoved();
}

void CUITextBox::MarkCaretMoved()
{
	PaintCounter = 0;
	bScrollToCaret = true;
}

void CUITextBox::BuildLines( TArray<FLine>& OutLines ) const
{
	OutLines.Reset();
	const FString& Text = Buffer.GetText();
	FLine Line;
	if ( Buffer.IsMultiline() )
	{
		for ( int32 Index = 0; Index < Text.Len(); ++Index )
		{
			if ( Text[Index] == '\n' )
			{
				Line.End = Index;
				OutLines.Add( Line );
				Line.Start = Index + 1;
			}
		}
	}
	Line.End = Text.Len();
	OutLines.Add( Line );
}

FString CUITextBox::GetShownRange( int32 Start, int32 End ) const
{
	const FString& Text = Buffer.GetText();
	if ( End <= Start )
	{
		return FString();
	}
	// パスワードは、文字ごとに * 1 つ
	return bPasswordMode ? Repeat( '*', CountChars( Text, End ) - CountChars( Text, Start ) ) : Text.Mid( Start, End - Start );
}

float32 CUITextBox::MeasureRange( int32 Start, int32 End ) const
{
	if ( !Font || End <= Start )
	{
		return 0.0f;
	}
	return static_cast<float32>( Font->MeasureWidth( GetShownRange( Start, End ) ) );
}

float32 CUITextBox::GetLineHeight() const
{
	if ( !Font )
	{
		return 0.0f;
	}
	const FFontMetrics Metrics = Font->GetMetrics();
	return static_cast<float32>( Metrics.LineHeight > 0 ? Metrics.LineHeight : Metrics.GlyphHeight );
}

float32 CUITextBox::GetLineTop( int32 LineIndex ) const
{
	const FUIRect Area = GetTextArea();
	if ( Buffer.IsMultiline() )
	{
		return Area.Top() - ScrollY + static_cast<float32>( LineIndex ) * GetLineHeight();
	}
	const float32 GlyphHeight = Font ? static_cast<float32>( Font->GetMetrics().GlyphHeight ) : 0.0f;
	return Area.Top() + ( Area.Size.y - GlyphHeight ) * 0.5f;
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
	const int32 SelectionBefore = Buffer.GetSelectionStart();
	const int32 SelectionEndBefore = Buffer.GetSelectionEnd();
	for ( const FTextInputEvent& Event : Events )
	{
		if ( Event.bCommand && Event.Command == ETextEditCommand::Copy )
		{
			CopySelection();
		}
		else if ( Event.bCommand && Event.Command == ETextEditCommand::Cut )
		{
			if ( CopySelection() )
			{
				Buffer.DeleteSelection();
			}
		}
		else if ( Event.bCommand && Event.Command == ETextEditCommand::Paste )
		{
			FString Pasted;
			if ( Clipboard != nullptr && Clipboard->GetText( Pasted ) )
			{
				// 1 行の欄では、改行・タブは、空白にする。複数行の欄では、改行を残す（CR LF と CR は、LF にする）
				const bool bKeepLines = Buffer.IsMultiline();
				FString Line;
				for ( int32 Index = 0; Index < Pasted.Len(); ++Index )
				{
					const char Character = Pasted[Index];
					if ( Character == '\r' && Index + 1 < Pasted.Len() && Pasted[Index + 1] == '\n' )
					{
						continue;
					}
					if ( Character == '\r' || Character == '\n' )
					{
						Line += bKeepLines ? '\n' : ' ';
					}
					else
					{
						Line += Character == '\t' ? ' ' : Character;
					}
				}
				Buffer.InsertText( Line );
			}
		}
		else if ( Event.bCommand && ( Event.Command == ETextEditCommand::Up || Event.Command == ETextEditCommand::Down || Event.Command == ETextEditCommand::SelectUp || Event.Command == ETextEditCommand::SelectDown ) )
		{
			// 上下の行へ（複数行の欄だけ。文字の幅で、同じ横の位置を探す）
			const bool bUp = Event.Command == ETextEditCommand::Up || Event.Command == ETextEditCommand::SelectUp;
			MoveCaretVertically( bUp ? -1 : 1, Event.Command == ETextEditCommand::SelectUp || Event.Command == ETextEditCommand::SelectDown );
		}
		else
		{
			Buffer.Apply( Event );
		}
	}
	if ( Buffer.GetText() != Before || Buffer.GetCursorByteOffset() != CursorBefore || Buffer.GetSelectionStart() != SelectionBefore || Buffer.GetSelectionEnd() != SelectionEndBefore )
	{
		MarkCaretMoved(); // 動いた直後は、カーソルを見せる
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

int32 CUITextBox::ByteOffsetInLine( const FLine& Line, float32 LocalX ) const
{
	// 位置にいちばん近い、文字の境目へ
	const FString& Text = Buffer.GetText();
	int32 Best = Line.Start;
	float32 BestDistance = NMath::Abs( LocalX );
	int32 Offset = Line.Start;
	while ( Offset < Line.End )
	{
		Offset = NMath::Min( NextBoundary( Text, Offset ), Line.End );
		const float32 Distance = NMath::Abs( MeasureRange( Line.Start, Offset ) - LocalX );
		if ( Distance < BestDistance )
		{
			BestDistance = Distance;
			Best = Offset;
		}
	}
	return Best;
}

int32 CUITextBox::ByteOffsetAt( const FVector2& Position ) const
{
	if ( !Font )
	{
		return 0;
	}
	TArray<FLine> Lines;
	BuildLines( Lines );
	int32 LineIndex = 0;
	const float32 LineHeight = GetLineHeight();
	if ( Buffer.IsMultiline() && LineHeight > 0.0f )
	{
		LineIndex = NMath::Clamp( static_cast<int32>( NMath::Floor( ( Position.y - GetLineTop( 0 ) ) / LineHeight ) ), 0, Lines.Num() - 1 );
	}
	return ByteOffsetInLine( Lines[LineIndex], Position.x - ( GetTextArea().Left() - ScrollX ) );
}

void CUITextBox::MoveCaretVertically( int32 Direction, bool bSelect )
{
	if ( !Buffer.IsMultiline() || !Font )
	{
		return;
	}
	TArray<FLine> Lines;
	BuildLines( Lines );
	const int32 Cursor = Buffer.GetCursorByteOffset();
	int32 LineIndex = 0;
	for ( int32 Index = 0; Index < Lines.Num(); ++Index )
	{
		if ( Lines[Index].Start <= Cursor )
		{
			LineIndex = Index;
		}
	}
	const int32 Target = LineIndex + Direction;
	int32 Moved = Cursor;
	if ( Target < 0 )
	{
		Moved = 0; // いちばん上の行から上は、先頭へ
	}
	else if ( Target >= Lines.Num() )
	{
		Moved = Buffer.GetText().Len(); // いちばん下の行から下は、末尾へ
	}
	else
	{
		Moved = ByteOffsetInLine( Lines[Target], MeasureRange( Lines[LineIndex].Start, Cursor ) );
	}
	if ( bSelect )
	{
		// 動かない端は、いまの範囲の、カーソルと反対の端（範囲が無ければ、いまのカーソル）
		const int32 Anchor = Buffer.HasSelection() ? ( Buffer.GetSelectionStart() == Cursor ? Buffer.GetSelectionEnd() : Buffer.GetSelectionStart() ) : Cursor;
		Buffer.SetSelection( Anchor, Moved );
	}
	else
	{
		Buffer.SetCursorByteOffset( Moved );
	}
}

bool CUITextBox::OnNavigate( EUINavDirection Direction )
{
	// 複数行の欄では、上下の矢印は、行の移動に使う（フォーカスは動かさない）
	return Buffer.IsMultiline() && ( Direction == EUINavDirection::Up || Direction == EUINavDirection::Down );
}

bool CUITextBox::OnWheel( const FVector2&, float32 Delta )
{
	if ( !Buffer.IsMultiline() || !Font )
	{
		return false;
	}
	TArray<FLine> Lines;
	BuildLines( Lines );
	const float32 LineHeight = GetLineHeight();
	const float32 MaxScroll = NMath::Max( 0.0f, static_cast<float32>( Lines.Num() ) * LineHeight - GetTextArea().Size.y );
	if ( MaxScroll <= 0.0f )
	{
		return false;
	}
	// 1 段で、3 行。カーソルの位置へは、戻さない
	ScrollY = NMath::Clamp( ScrollY - Delta * LineHeight * 3.0f, 0.0f, MaxScroll );
	bScrollToCaret = false;
	return true;
}

void CUITextBox::OnPointerPressed( const FVector2& Position )
{
	if ( !Font )
	{
		return;
	}
	// 続けて、同じ所を押した回数（時間が進んでいて、間隔の中で、ほぼ同じ位置）
	const bool bRepeated = DoubleClickSeconds > 0.0f && ClickCount > 0 && TimeSincePress > 0.0f && TimeSincePress <= DoubleClickSeconds && NMath::Abs( Position.x - LastPressPosition.x ) <= 4.0f
						   && NMath::Abs( Position.y - LastPressPosition.y ) <= 4.0f;
	ClickCount = bRepeated ? ( ClickCount % 3 ) + 1 : 1;
	TimeSincePress = 0.0f;
	LastPressPosition = Position;

	const int32 Offset = ByteOffsetAt( Position );
	bDragByWord = false;
	if ( ClickCount == 2 )
	{
		// ダブルクリック: 単語を選ぶ。そのままドラッグすると、単語ごとに広がる
		Buffer.GetWordRange( Offset, DragWordStart, DragWordEnd );
		Buffer.SetSelection( DragWordStart, DragWordEnd );
		bDragSelecting = true;
		bDragByWord = true;
	}
	else if ( ClickCount == 3 )
	{
		// 3 回目: 全体（複数行の欄では、その行）を選ぶ
		int32 Start = 0;
		int32 End = 0;
		Buffer.GetLineRange( Offset, Start, End );
		Buffer.SetSelection( Start, End );
		bDragSelecting = false;
	}
	else
	{
		// クリックした位置へカーソルを置き、ここを起点に、ドラッグで範囲を選べるようにする
		DragAnchor = Offset;
		Buffer.SetSelection( DragAnchor, DragAnchor );
		bDragSelecting = true;
	}
	MarkCaretMoved();
}

void CUITextBox::OnPointerDragged( const FVector2& Position )
{
	if ( !bDragSelecting || !Font )
	{
		return;
	}
	const int32 Offset = ByteOffsetAt( Position );
	const int32 CursorBefore = Buffer.GetCursorByteOffset();
	if ( bDragByWord )
	{
		// ポインターの所の単語まで、単語ごとに広げる（最初の単語は、いつも入る）
		int32 Start = 0;
		int32 End = 0;
		Buffer.GetWordRange( Offset, Start, End );
		if ( Offset < DragWordStart )
		{
			Buffer.SetSelection( DragWordEnd, Start );
		}
		else
		{
			Buffer.SetSelection( DragWordStart, NMath::Max( End, DragWordEnd ) );
		}
	}
	else
	{
		Buffer.SetSelection( DragAnchor, Offset );
	}
	if ( Buffer.GetCursorByteOffset() != CursorBefore )
	{
		MarkCaretMoved();
	}
	else
	{
		PaintCounter = 0;
	}
}

void CUITextBox::OnPointerReleased( const FVector2& )
{
	bDragSelecting = false;
	bDragByWord = false;
}

int32 CUITextBox::GetBackgroundStateIndex() const
{
	if ( !IsEffectivelyEnabled() )
	{
		return 2;
	}
	return IsFocused() ? 1 : 0;
}

const FUIBrush& CUITextBox::GetBackgroundBrush( int32 StateIndex ) const
{
	return StateIndex == 2 ? Style.Disabled : ( StateIndex == 1 ? Style.Focused : Style.Normal );
}

void CUITextBox::OnAdvanceTime( float32 DeltaTime )
{
	BackgroundFade.Advance( GetBackgroundStateIndex(), DeltaTime, Style.TransitionDuration );
	// ダブルクリックの間隔を数える（数えすぎて、桁が落ちないように、上で止める）
	TimeSincePress = NMath::Min( TimeSincePress + DeltaTime, 3600.0f );
}

void CUITextBox::OnPaint( FDrawContext& CommandList, float32 EffectiveOpacity )
{
	const bool bEnabled = IsEffectivelyEnabled();
	if ( Style.TransitionDuration > 0.0f && BackgroundFade.IsStarted() && BackgroundFade.IsFading() )
	{
		// 背景の切り替えを、時間をかけて混ぜる
		NUIBrush::DrawFade( CommandList, GetBackgroundBrush( BackgroundFade.Previous ), GetBackgroundBrush( BackgroundFade.Current ), BackgroundFade.Progress, GetRect(), EffectiveOpacity );
	}
	else
	{
		const bool bUseFade = Style.TransitionDuration > 0.0f && BackgroundFade.IsStarted();
		NUIBrush::Draw( CommandList, GetBackgroundBrush( bUseFade ? BackgroundFade.Current : GetBackgroundStateIndex() ), GetRect(), EffectiveOpacity );
	}
	if ( !Font )
	{
		return;
	}

	const FUIRect Area = GetTextArea();
	if ( Area.Size.x <= 0.0f || Area.Size.y <= 0.0f )
	{
		return;
	}
	const bool bMultiline = Buffer.IsMultiline();
	const bool bFocused = IsFocused();
	const float32 GlyphHeight = static_cast<float32>( Font->GetMetrics().GlyphHeight );
	const float32 LineHeight = GetLineHeight();
	TArray<FLine> Lines;
	BuildLines( Lines );
	const int32 Cursor = Buffer.GetCursorByteOffset();
	int32 CaretLine = 0;
	for ( int32 Index = 0; Index < Lines.Num(); ++Index )
	{
		if ( Lines[Index].Start <= Cursor )
		{
			CaretLine = Index;
		}
	}
	const bool bComposing = !CompositionText.IsEmpty();
	const int32 CompositionCaret = NMath::Clamp( CompositionCursor, 0, CompositionText.Len() );

	// カーソルの位置（行の左端から。ずらす前）を測って、見える位置まで、横へ（複数行の欄では、縦にも）ずらす
	const float32 CaretPrefix = MeasureRange( Lines[CaretLine].Start, Cursor );
	const float32 CaretOffset = CaretPrefix + ( bComposing && CompositionCaret > 0 ? static_cast<float32>( Font->MeasureWidth( CompositionText.Left( CompositionCaret ) ) ) : 0.0f );
	if ( bFocused )
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
	if ( bMultiline )
	{
		if ( bScrollToCaret && bFocused )
		{
			const float32 CaretTop = static_cast<float32>( CaretLine ) * LineHeight;
			if ( CaretTop + GlyphHeight - ScrollY > Area.Size.y )
			{
				ScrollY = CaretTop + GlyphHeight - Area.Size.y;
			}
			if ( CaretTop < ScrollY )
			{
				ScrollY = CaretTop;
			}
		}
		ScrollY = NMath::Clamp( ScrollY, 0.0f, NMath::Max( 0.0f, static_cast<float32>( Lines.Num() - 1 ) * LineHeight + GlyphHeight - Area.Size.y ) );
	}
	else
	{
		ScrollY = 0.0f;
	}
	bScrollToCaret = false;

	// 文字・下線・カーソルは、欄の矩形でクリップ
	FClipScope Clip( CommandList, static_cast<int32>( NMath::Floor( Area.Left() ) ), static_cast<int32>( NMath::Floor( Area.Top() ) ), static_cast<int32>( NMath::Ceil( Area.Right() ) ),
					 static_cast<int32>( NMath::Ceil( Area.Bottom() ) ) );
	const float32 OriginX = Area.Left() - ScrollX;
	// 見えている行だけを描く
	auto IsLineVisible = [&]( int32 LineIndex )
	{
		const float32 Top = GetLineTop( LineIndex );
		return Top < Area.Bottom() && Top + GlyphHeight > Area.Top();
	};

	// 選んでいる範囲の背景（変換中は、出さない）
	if ( Buffer.HasSelection() && !bComposing && bFocused )
	{
		const int32 SelectionAlpha = NUIBrush::ToAlpha( Style.SelectionColor, EffectiveOpacity );
		const int32 SelectionStart = Buffer.GetSelectionStart();
		const int32 SelectionEnd = Buffer.GetSelectionEnd();
		for ( int32 Index = 0; Index < Lines.Num() && SelectionAlpha > 0; ++Index )
		{
			const FLine& Line = Lines[Index];
			if ( SelectionEnd < Line.Start || SelectionStart > Line.End || !IsLineVisible( Index ) )
			{
				continue;
			}
			const float32 StartX = OriginX + MeasureRange( Line.Start, NMath::Max( SelectionStart, Line.Start ) );
			// 改行も選ばれている行は、行の末尾の先まで、少し伸ばす
			const float32 EndX = OriginX + MeasureRange( Line.Start, NMath::Min( SelectionEnd, Line.End ) ) + ( SelectionEnd > Line.End ? GlyphHeight * 0.3f : 0.0f );
			if ( EndX > StartX )
			{
				const float32 Top = GetLineTop( Index );
				CommandList.Blend( EDrawBlend::Alpha, SelectionAlpha );
				CommandList.BoxAA( StartX, Top, EndX, Top + GlyphHeight, Style.SelectionColor, true );
			}
		}
	}

	// 文字（空で、フォーカスも無ければ、薄い案内の文字）
	const bool bShowPlaceholder = Buffer.GetText().IsEmpty() && !bComposing && !bFocused && !Placeholder.IsEmpty();
	const FColor32 TextColor = !bEnabled ? Style.DisabledTextColor : ( bShowPlaceholder ? Style.PlaceholderColor : Style.TextColor );
	const int32 TextAlpha = NUIBrush::ToAlpha( TextColor, EffectiveOpacity );
	for ( int32 Index = 0; Index < Lines.Num() && TextAlpha > 0; ++Index )
	{
		if ( !IsLineVisible( Index ) )
		{
			continue;
		}
		const FLine& Line = Lines[Index];
		FString Shown;
		if ( bShowPlaceholder )
		{
			Shown = Placeholder;
		}
		else if ( bComposing && Index == CaretLine )
		{
			// かな漢字変換の途中の文字は、カーソルの位置に差し込む
			Shown = GetShownRange( Line.Start, Cursor ) + CompositionText + GetShownRange( Cursor, Line.End );
		}
		else
		{
			Shown = GetShownRange( Line.Start, Line.End );
		}
		if ( Shown.IsEmpty() )
		{
			continue;
		}
		CommandList.Blend( EDrawBlend::Alpha, TextAlpha );
		FTextLayoutOptions Options;
		const FTextLayout Layout = Font->Layout( *Shown, Options );
		Font->RecordLayout( CommandList, Layout, static_cast<int32>( NMath::Floor( OriginX ) ), static_cast<int32>( NMath::Floor( GetLineTop( Index ) ) ), Font->MeasureWidth( Shown ) + 2, TextColor, EDrawTextAlign::Left, FColor32( 0, 0, 0, 0 ) );
	}

	const float32 CaretTop = GetLineTop( CaretLine );

	// かな漢字変換の途中の文字の下線
	if ( bComposing && bFocused )
	{
		const float32 StartX = OriginX + CaretPrefix;
		const float32 EndX = StartX + static_cast<float32>( Font->MeasureWidth( CompositionText ) );
		const int32 Alpha = NUIBrush::ToAlpha( Style.TextColor, EffectiveOpacity );
		CommandList.Blend( EDrawBlend::Alpha, Alpha );
		CommandList.BoxAA( StartX, CaretTop + GlyphHeight, EndX, CaretTop + GlyphHeight + 1.0f, Style.TextColor, true );
	}

	// カーソル
	if ( bFocused && bEnabled )
	{
		if ( IsCaretVisible() )
		{
			const int32 Alpha = NUIBrush::ToAlpha( Style.CaretColor, EffectiveOpacity );
			if ( Alpha > 0 )
			{
				const float32 CaretX = OriginX + CaretOffset;
				CommandList.Blend( EDrawBlend::Alpha, Alpha );
				CommandList.BoxAA( CaretX, CaretTop, CaretX + 2.0f, CaretTop + GlyphHeight, Style.CaretColor, true );
			}
		}
		++PaintCounter;
	}
}
