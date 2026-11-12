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

	// Offset から始まる 1 文字のコードポイント（壊れていれば、そのバイトの値）
	uint32 DecodeAt( const FString& Text, int32 Offset )
	{
		const uint32 Lead = static_cast<uint8>( Text[Offset] );
		int32 Extra = 0;
		uint32 Code = Lead;
		if ( Lead >= 0xF0 )
		{
			Extra = 3;
			Code = Lead & 0x07;
		}
		else if ( Lead >= 0xE0 )
		{
			Extra = 2;
			Code = Lead & 0x0F;
		}
		else if ( Lead >= 0xC0 )
		{
			Extra = 1;
			Code = Lead & 0x1F;
		}
		for ( int32 Index = 1; Index <= Extra; ++Index )
		{
			if ( Offset + Index >= Text.Len() || !IsContinuation( Text[Offset + Index] ) )
			{
				return Lead;
			}
			Code = ( Code << 6 ) | ( static_cast<uint8>( Text[Offset + Index] ) & 0x3F );
		}
		return Code;
	}

	// 単語を分ける、文字の種類
	enum class ECharClass : uint8
	{
		Space,
		Word,          // 英数字と _（全角の英数字も）
		Hiragana,
		Katakana,
		Kanji,
		OtherLetter,   // そのほかの、ASCII でない文字
		Punctuation,
		NewLine
	};

	bool InRange( uint32 Code, uint32 First, uint32 Last )
	{
		return Code >= First && Code <= Last;
	}

	ECharClass ClassOf( uint32 Code )
	{
		if ( Code == '\n' || Code == '\r' )
		{
			return ECharClass::NewLine;
		}
		if ( Code == ' ' || Code == '\t' || Code == 0x3000 )
		{
			return ECharClass::Space;
		}
		if ( InRange( Code, '0', '9' ) || InRange( Code, 'A', 'Z' ) || InRange( Code, 'a', 'z' ) || Code == '_' || InRange( Code, 0xFF10, 0xFF19 ) || InRange( Code, 0xFF21, 0xFF3A ) || InRange( Code, 0xFF41, 0xFF5A ) )
		{
			return ECharClass::Word;
		}
		if ( Code < 0x80 )
		{
			return ECharClass::Punctuation;
		}
		if ( InRange( Code, 0x3041, 0x309F ) )
		{
			return ECharClass::Hiragana;
		}
		// 長音の記号（ー）は、カタカナの続きとして扱う
		if ( InRange( Code, 0x30A1, 0x30FA ) || InRange( Code, 0x30FC, 0x30FF ) || InRange( Code, 0x31F0, 0x31FF ) || InRange( Code, 0xFF66, 0xFF9F ) )
		{
			return ECharClass::Katakana;
		}
		if ( Code == 0x3005 || InRange( Code, 0x3400, 0x4DBF ) || InRange( Code, 0x4E00, 0x9FFF ) || InRange( Code, 0xF900, 0xFAFF ) || InRange( Code, 0x20000, 0x2FFFF ) )
		{
			return ECharClass::Kanji;
		}
		// 全角の記号・句読点
		if ( InRange( Code, 0x2000, 0x206F ) || InRange( Code, 0x3001, 0x303F ) || Code == 0x30A0 || Code == 0x30FB || InRange( Code, 0xFF01, 0xFF0F ) || InRange( Code, 0xFF1A, 0xFF20 ) || InRange( Code, 0xFF3B, 0xFF40 )
			 || InRange( Code, 0xFF5B, 0xFF65 ) )
		{
			return ECharClass::Punctuation;
		}
		return ECharClass::OtherLetter;
	}
}

void FTextEditBuffer::GetWordRange( int32 ByteOffset, int32& OutStart, int32& OutEnd ) const
{
	// 文字の境目へ寄せる
	int32 Offset = ByteOffset < 0 ? 0 : ( ByteOffset > Text.Len() ? Text.Len() : ByteOffset );
	while ( Offset > 0 && Offset < Text.Len() && IsContinuation( Text[Offset] ) )
	{
		--Offset;
	}
	OutStart = Offset;
	OutEnd = Offset;
	// 行の末尾（文字列の末尾・改行の前）なら、その前の文字の単語
	if ( Offset >= Text.Len() || ClassOf( DecodeAt( Text, Offset ) ) == ECharClass::NewLine )
	{
		if ( Offset == 0 )
		{
			return;
		}
		Offset = PreviousBoundary( Offset );
		if ( ClassOf( DecodeAt( Text, Offset ) ) == ECharClass::NewLine )
		{
			return; // 空の行
		}
	}
	const ECharClass Class = ClassOf( DecodeAt( Text, Offset ) );
	int32 Start = Offset;
	while ( Start > 0 )
	{
		const int32 Previous = PreviousBoundary( Start );
		if ( ClassOf( DecodeAt( Text, Previous ) ) != Class )
		{
			break;
		}
		Start = Previous;
	}
	int32 End = NextBoundary( Offset );
	while ( End < Text.Len() && ClassOf( DecodeAt( Text, End ) ) == Class )
	{
		End = NextBoundary( End );
	}
	OutStart = Start;
	OutEnd = End;
}

void FTextEditBuffer::GetLineRange( int32 ByteOffset, int32& OutStart, int32& OutEnd ) const
{
	const int32 Offset = ByteOffset < 0 ? 0 : ( ByteOffset > Text.Len() ? Text.Len() : ByteOffset );
	OutStart = LineStart( Offset );
	OutEnd = LineEnd( Offset );
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
