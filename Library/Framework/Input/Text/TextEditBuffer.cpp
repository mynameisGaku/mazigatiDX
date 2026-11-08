// SPDX-License-Identifier: MIT
#include "TextEditBuffer.h"

namespace
{
	// 続きのバイト（文字の途中）か
	bool IsContinuation( char Byte )
	{
		return ( static_cast<uint8>( Byte ) & 0xC0 ) == 0x80;
	}

	// 文字列の先頭から、MaxChars 文字ぶんのバイト数。足りなければ全体
	int32 CountBytesOfChars( const FString& Text, int32 MaxChars )
	{
		int32 Chars = 0;
		int32 Offset = 0;
		while ( Offset < Text.Len() && Chars < MaxChars )
		{
			++Offset;
			while ( Offset < Text.Len() && IsContinuation( Text[Offset] ) )
			{
				++Offset;
			}
			++Chars;
		}
		return Offset;
	}
}

void FTextEditBuffer::SetMaxCharCount( int32 InMaxCharCount )
{
	MaxCharCount = InMaxCharCount > 0 ? InMaxCharCount : 0;
	if ( MaxCharCount > 0 && GetCharCount() > MaxCharCount )
	{
		Text = Text.Left( CountBytesOfChars( Text, MaxCharCount ) );
		CursorByteOffset = CursorByteOffset > Text.Len() ? Text.Len() : CursorByteOffset;
		SelectionAnchor = -1;
	}
}

int32 FTextEditBuffer::GetCharCount() const
{
	return Text.CharCount();
}

int32 FTextEditBuffer::GetCursorCharIndex() const
{
	return Text.Left( CursorByteOffset ).CharCount();
}

void FTextEditBuffer::SetText( const FString& InText )
{
	Text = InText.ToValidUtf8();
	if ( MaxCharCount > 0 && GetCharCount() > MaxCharCount )
	{
		Text = Text.Left( CountBytesOfChars( Text, MaxCharCount ) );
	}
	CursorByteOffset = Text.Len();
	SelectionAnchor = -1;
}

void FTextEditBuffer::Clear()
{
	Text = FString();
	CursorByteOffset = 0;
	SelectionAnchor = -1;
	bSubmitted = false;
	bCancelled = false;
}

void FTextEditBuffer::SetCursorByteOffset( int32 InByteOffset )
{
	int32 Offset = InByteOffset < 0 ? 0 : ( InByteOffset > Text.Len() ? Text.Len() : InByteOffset );
	// 文字の途中なら、その文字の手前へ戻す
	while ( Offset > 0 && Offset < Text.Len() && IsContinuation( Text[Offset] ) )
	{
		--Offset;
	}
	CursorByteOffset = Offset;
	SelectionAnchor = -1;
}

FString FTextEditBuffer::GetSelectedText() const
{
	return HasSelection() ? Text.Mid( GetSelectionStart(), GetSelectionEnd() - GetSelectionStart() ) : FString();
}

void FTextEditBuffer::SetSelection( int32 InAnchorByteOffset, int32 InCursorByteOffset )
{
	// どちらも文字の境目へ寄せる（SetCursorByteOffset と同じ。範囲は、いったん、やめる）
	SetCursorByteOffset( InAnchorByteOffset );
	const int32 Anchor = CursorByteOffset;
	SetCursorByteOffset( InCursorByteOffset );
	SelectionAnchor = Anchor;
}

void FTextEditBuffer::SelectAll()
{
	SelectionAnchor = 0;
	CursorByteOffset = Text.Len();
}

bool FTextEditBuffer::DeleteSelection()
{
	if ( !HasSelection() )
	{
		SelectionAnchor = -1;
		return false;
	}
	const int32 Start = GetSelectionStart();
	const int32 End = GetSelectionEnd();
	Text = Text.Left( Start ) + Text.RightChop( End );
	CursorByteOffset = Start;
	SelectionAnchor = -1;
	return true;
}

bool FTextEditBuffer::InsertText( const FString& Inserted )
{
	const bool bDeleted = DeleteSelection();
	return InsertAtCursor( Inserted ) || bDeleted;
}

bool FTextEditBuffer::InsertAtCursor( const FString& Inserted )
{
	FString Accepted = Inserted.ToValidUtf8();
	if ( MaxCharCount > 0 )
	{
		const int32 Room = MaxCharCount - GetCharCount();
		if ( Room <= 0 )
		{
			return false;
		}
		Accepted = Accepted.Left( CountBytesOfChars( Accepted, Room ) );
	}
	if ( Accepted.IsEmpty() )
	{
		return false;
	}
	Text = Text.Left( CursorByteOffset ) + Accepted + Text.RightChop( CursorByteOffset );
	CursorByteOffset += Accepted.Len();
	return true;
}

int32 FTextEditBuffer::PreviousBoundary( int32 ByteOffset ) const
{
	int32 Offset = ByteOffset > 0 ? ByteOffset - 1 : 0;
	while ( Offset > 0 && IsContinuation( Text[Offset] ) )
	{
		--Offset;
	}
	return Offset;
}

int32 FTextEditBuffer::NextBoundary( int32 ByteOffset ) const
{
	int32 Offset = ByteOffset < Text.Len() ? ByteOffset + 1 : Text.Len();
	while ( Offset < Text.Len() && IsContinuation( Text[Offset] ) )
	{
		++Offset;
	}
	return Offset;
}

int32 FTextEditBuffer::LineStart( int32 ByteOffset ) const
{
	if ( !bMultiline )
	{
		return 0;
	}
	int32 Offset = ByteOffset;
	while ( Offset > 0 && Text[Offset - 1] != '\n' )
	{
		--Offset;
	}
	return Offset;
}

int32 FTextEditBuffer::LineEnd( int32 ByteOffset ) const
{
	if ( !bMultiline )
	{
		return Text.Len();
	}
	int32 Offset = ByteOffset;
	while ( Offset < Text.Len() && Text[Offset] != '\n' )
	{
		++Offset;
	}
	return Offset;
}

bool FTextEditBuffer::Apply( const FTextInputEvent& Event )
{
	if ( !Event.bCommand )
	{
		return InsertText( Event.Text );
	}

	switch ( Event.Command )
	{
	case ETextEditCommand::Backspace:
	{
		if ( DeleteSelection() )
		{
			return true;
		}
		SelectionAnchor = -1;
		if ( CursorByteOffset <= 0 )
		{
			return false;
		}
		const int32 Start = PreviousBoundary( CursorByteOffset );
		Text = Text.Left( Start ) + Text.RightChop( CursorByteOffset );
		CursorByteOffset = Start;
		return true;
	}
	case ETextEditCommand::Delete:
	{
		if ( DeleteSelection() )
		{
			return true;
		}
		SelectionAnchor = -1;
		if ( CursorByteOffset >= Text.Len() )
		{
			return false;
		}
		const int32 End = NextBoundary( CursorByteOffset );
		Text = Text.Left( CursorByteOffset ) + Text.RightChop( End );
		return true;
	}
	case ETextEditCommand::Left:
	{
		if ( HasSelection() )
		{
			CursorByteOffset = GetSelectionStart();
			SelectionAnchor = -1;
			return true;
		}
		SelectionAnchor = -1;
		const int32 Moved = PreviousBoundary( CursorByteOffset );
		const bool bChanged = Moved != CursorByteOffset;
		CursorByteOffset = Moved;
		return bChanged;
	}
	case ETextEditCommand::Right:
	{
		if ( HasSelection() )
		{
			CursorByteOffset = GetSelectionEnd();
			SelectionAnchor = -1;
			return true;
		}
		SelectionAnchor = -1;
		const int32 Moved = NextBoundary( CursorByteOffset );
		const bool bChanged = Moved != CursorByteOffset;
		CursorByteOffset = Moved;
		return bChanged;
	}
	case ETextEditCommand::Home:
	{
		const bool bHadSelection = HasSelection();
		SelectionAnchor = -1;
		const int32 Moved = LineStart( CursorByteOffset );
		const bool bChanged = Moved != CursorByteOffset || bHadSelection;
		CursorByteOffset = Moved;
		return bChanged;
	}
	case ETextEditCommand::End:
	{
		const bool bHadSelection = HasSelection();
		SelectionAnchor = -1;
		const int32 Moved = LineEnd( CursorByteOffset );
		const bool bChanged = Moved != CursorByteOffset || bHadSelection;
		CursorByteOffset = Moved;
		return bChanged;
	}
	case ETextEditCommand::SelectLeft:
	case ETextEditCommand::SelectRight:
	case ETextEditCommand::SelectHome:
	case ETextEditCommand::SelectEnd:
	{
		// 動かない端を決めてから、カーソルだけ動かす
		if ( SelectionAnchor < 0 )
		{
			SelectionAnchor = CursorByteOffset;
		}
		int32 Moved = CursorByteOffset;
		switch ( Event.Command )
		{
		case ETextEditCommand::SelectLeft:
			Moved = PreviousBoundary( CursorByteOffset );
			break;
		case ETextEditCommand::SelectRight:
			Moved = NextBoundary( CursorByteOffset );
			break;
		case ETextEditCommand::SelectHome:
			Moved = LineStart( CursorByteOffset );
			break;
		default:
			Moved = LineEnd( CursorByteOffset );
			break;
		}
		const bool bChanged = Moved != CursorByteOffset;
		CursorByteOffset = Moved;
		return bChanged;
	}
	case ETextEditCommand::SelectAll:
	{
		const bool bChanged = !( SelectionAnchor == 0 && CursorByteOffset == Text.Len() );
		SelectAll();
		return bChanged && Text.Len() > 0;
	}
	case ETextEditCommand::Enter:
	{
		if ( bMultiline )
		{
			return InsertAtCursor( FString( "\n" ) );
		}
		bSubmitted = true;
		return false;
	}
	case ETextEditCommand::Escape:
	{
		bCancelled = true;
		return false;
	}
	default:
		// 上下・Tab・ページ・コピー・切り取り・貼り付けは、何もしない（コピーと貼り付けは、クリップボードを持つ側が扱う）
		return false;
	}
}

bool FTextEditBuffer::ApplyAll( const TArray<FTextInputEvent>& Events )
{
	bool bChanged = false;
	for ( const FTextInputEvent& Event : Events )
	{
		bChanged = Apply( Event ) || bChanged;
	}
	return bChanged;
}

bool FTextEditBuffer::ConsumeSubmitted()
{
	const bool bResult = bSubmitted;
	bSubmitted = false;
	return bResult;
}

bool FTextEditBuffer::ConsumeCancelled()
{
	const bool bResult = bCancelled;
	bCancelled = false;
	return bResult;
}
