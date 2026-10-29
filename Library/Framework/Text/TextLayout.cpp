#include "TextLayout.h"

#include "Library/Math/MathUtils.h"

namespace
{
	// 行頭に置かない方がよい文字
	constexpr uint32 CharsAvoidedAtLineStart[] = {
		0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x30FB, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF01,   // 、。，．・：；？！
		0x30FC, 0x3005, 0x309D, 0x309E, 0x30FD, 0x30FE, 0x309B, 0x309C,           // ー々ゝゞヽヾ゛゜
		0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x3095, 0x3096,   // 小さいひらがな
		0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6,   // 小さいカタカナ
		0xFF09, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, 0xFF5D, 0xFF3D,   // ）」』】〕〉》｝］
		0x2019, 0x201D, 0x2025, 0x2026,                                           // ’”‥…
		0x2C, 0x2E, 0x3B, 0x3A, 0x3F, 0x21, 0x29, 0x5D, 0x7D, 0x25,              // , . ; : ? ! ) ] } %
	};

	// 行末に置かない方がよい文字（開き括弧など）
	constexpr uint32 CharsAvoidedAtLineEnd[] = {
		0xFF08, 0x300C, 0x300E, 0x3010, 0x3014, 0x3008, 0x300A, 0xFF5B, 0xFF3B,   // （「『【〔〈《｛［
		0x2018, 0x201C,                                                           // ‘“
		0x28, 0x5B, 0x7B,                                                         // ( [ {
	};

	template<size_t N>
	bool Contains( const uint32 ( &Table )[N], uint32 CodePoint )
	{
		for ( const uint32 Entry : Table )
		{
			if ( Entry == CodePoint )
			{
				return true;
			}
		}
		return false;
	}

	// 配置の前の準備: 文字 1 つ分の、コードポイントと位置と幅
	struct FLayoutGlyph
	{
		uint32 CodePoint = 0;
		int32 Offset = 0;
		int32 Bytes = 0;
		int32 Width = 0;
	};

	// できた行を集める（行数の上限と、いちばん長い行を管理する）
	class FLineCollector
	{
	public:
		FLineCollector( FTextLayout& InLayout, int32 InMaxLines )
			: Layout( InLayout )
			, MaxLines( InMaxLines )
		{
		}

		// 行を足す。上限を超えるときは、足さずに bTruncated にして false を返す
		bool Add( int32 Begin, int32 End, int32 Width )
		{
			if ( MaxLines > 0 && Layout.Lines.Num() >= MaxLines )
			{
				Layout.bTruncated = true;
				return false;
			}
			FTextLine Line;
			Line.Begin = Begin;
			Line.End = End;
			Line.Width = Width;
			Layout.Lines.Add( Line );
			Layout.Width = NMath::Max( Layout.Width, Width );
			return true;
		}

	private:
		FTextLayout& Layout;
		int32 MaxLines;
	};

	// グリフ [ParagraphBegin, ParagraphEnd)（改行を含まない 1 段落）を、行に分ける。行数の上限で止まったら false
	bool LayoutParagraph( const TArray<FLayoutGlyph>& Glyphs, int32 ParagraphBegin, int32 ParagraphEnd, int32 TextLength, const FTextLayoutOptions& Options, FLineCollector& Lines )
	{
		// 空の段落は、空の行 1 つ
		if ( ParagraphBegin >= ParagraphEnd )
		{
			const int32 Offset = ParagraphBegin < Glyphs.Num() ? Glyphs[ParagraphBegin].Offset : TextLength;
			return Lines.Add( Offset, Offset, 0 );
		}

		int32 LineStart = ParagraphBegin;
		while ( LineStart < ParagraphEnd )
		{
			int32 WidthWithTrailingSpaces = 0;      // 行の先頭から、いまの文字の手前まで（空白を含む）
			int32 WidthWithoutTrailingSpaces = 0;   // そのうち、末尾の空白を除いた幅
			int32 VisibleEnd = LineStart;           // 末尾の空白を除いた、最後の文字の次（グリフの番号）
			int32 LastBreakPosition = -1;           // 折ってよい位置（次の行の先頭になるグリフの番号）
			int32 WidthAtLastBreak = 0;
			int32 VisibleEndAtLastBreak = LineStart;

			int32 LineEnd = -1;                     // 行の終わり（グリフの番号。含まない）。-1 は、段落の終わりまで
			int32 LineWidth = 0;
			int32 NextLineStart = ParagraphEnd;

			for ( int32 Index = LineStart; Index < ParagraphEnd; ++Index )
			{
				const bool bWhitespace = NTextLayout::IsWhitespace( Glyphs[Index].CodePoint );
				// この文字の手前が、折ってよい位置か（行の中身が、すでにあるときだけ）
				if ( Index > LineStart && VisibleEnd > LineStart && NTextLayout::CanBreakBetween( Glyphs[Index - 1].CodePoint, Glyphs[Index].CodePoint, Options.bAvoidPunctuationAtLineEdges ) )
				{
					LastBreakPosition = Index;
					WidthAtLastBreak = WidthWithoutTrailingSpaces;
					VisibleEndAtLastBreak = VisibleEnd;
				}
				// この文字を足すと、幅（空白を除く）が上限を超えるなら、ここで折る（行の中身が、すでにあるときだけ。1 文字は、必ず行に入る）
				if ( Options.MaxWidth > 0 && !bWhitespace && VisibleEnd > LineStart && WidthWithTrailingSpaces + Glyphs[Index].Width > Options.MaxWidth )
				{
					if ( LastBreakPosition > LineStart )
					{
						LineEnd = VisibleEndAtLastBreak;
						LineWidth = WidthAtLastBreak;
						NextLineStart = LastBreakPosition;
						break;
					}
					// 折れる位置が無い（長い単語）: 文字の途中で折る。ただし、行頭に置けない文字（句読点など）は、行の端にはみ出して、ぶら下げる
					const bool bHang = Options.bAvoidPunctuationAtLineEdges && !NTextLayout::CanStartLine( Glyphs[Index].CodePoint );
					if ( !bHang )
					{
						// 開き括弧などで行が終わらないよう、折る位置を手前へ戻す（1 文字は、必ず行に残す）
						int32 BreakIndex = Index;
						while ( Options.bAvoidPunctuationAtLineEdges && BreakIndex > LineStart + 1 && !NTextLayout::CanEndLine( Glyphs[BreakIndex - 1].CodePoint ) )
						{
							--BreakIndex;
						}
						int32 WidthBeforeBreak = 0;
						for ( int32 Before = LineStart; Before < BreakIndex; ++Before )
						{
							WidthBeforeBreak += Glyphs[Before].Width;
						}
						LineEnd = BreakIndex;
						LineWidth = BreakIndex == Index ? WidthWithoutTrailingSpaces : WidthBeforeBreak;
						NextLineStart = BreakIndex;
						break;
					}
				}
				WidthWithTrailingSpaces += Glyphs[Index].Width;
				if ( !bWhitespace )
				{
					WidthWithoutTrailingSpaces = WidthWithTrailingSpaces;
					VisibleEnd = Index + 1;
				}
			}
			if ( LineEnd < 0 )
			{
				LineEnd = VisibleEnd;
				LineWidth = WidthWithoutTrailingSpaces;
				NextLineStart = ParagraphEnd;
			}

			const int32 BeginOffset = Glyphs[LineStart].Offset;
			const int32 EndOffset = LineEnd > LineStart ? Glyphs[LineEnd - 1].Offset + Glyphs[LineEnd - 1].Bytes : BeginOffset;
			if ( !Lines.Add( BeginOffset, EndOffset, LineWidth ) )
			{
				return false;
			}

			// 折り返した次の行の、行頭の空白は読み飛ばす
			LineStart = NextLineStart;
			while ( LineStart < ParagraphEnd && NTextLayout::IsWhitespace( Glyphs[LineStart].CodePoint ) )
			{
				++LineStart;
			}
		}
		return true;
	}
}

FString FTextLayout::GetLineText( int32 Index ) const
{
	if ( !Lines.IsValidIndex( Index ) )
	{
		return FString();
	}
	const FTextLine& Line = Lines[Index];
	return Text.Mid( Line.Begin, Line.End - Line.Begin );
}

uint32 NTextLayout::DecodeUtf8( const char* Text, int32 Length, int32& InOutOffset )
{
	if ( Text == nullptr || InOutOffset < 0 || InOutOffset >= Length )
	{
		return 0;
	}
	const uint8* Bytes = reinterpret_cast<const uint8*>( Text ) + InOutOffset;
	const int32 Remaining = Length - InOutOffset;
	const uint32 First = Bytes[0];
	int32 NumBytes = 0;
	uint32 CodePoint = 0;
	uint32 Minimum = 0;
	if ( First < 0x80 )
	{
		++InOutOffset;
		return First;
	}
	else if ( First >= 0xC2 && First <= 0xDF )
	{
		NumBytes = 2;
		CodePoint = First & 0x1F;
		Minimum = 0x80;
	}
	else if ( First >= 0xE0 && First <= 0xEF )
	{
		NumBytes = 3;
		CodePoint = First & 0x0F;
		Minimum = 0x800;
	}
	else if ( First >= 0xF0 && First <= 0xF4 )
	{
		NumBytes = 4;
		CodePoint = First & 0x07;
		Minimum = 0x10000;
	}
	else
	{
		++InOutOffset;
		return 0xFFFD;
	}
	if ( Remaining < NumBytes )
	{
		++InOutOffset;
		return 0xFFFD;
	}
	for ( int32 Index = 1; Index < NumBytes; ++Index )
	{
		if ( ( Bytes[Index] & 0xC0 ) != 0x80 )
		{
			++InOutOffset;
			return 0xFFFD;
		}
		CodePoint = ( CodePoint << 6 ) | ( Bytes[Index] & 0x3F );
	}
	// 必要より長い書き方・サロゲート・範囲の外は、壊れた並び
	if ( CodePoint < Minimum || CodePoint > 0x10FFFF || ( CodePoint >= 0xD800 && CodePoint <= 0xDFFF ) )
	{
		++InOutOffset;
		return 0xFFFD;
	}
	InOutOffset += NumBytes;
	return CodePoint;
}

bool NTextLayout::IsWhitespace( uint32 CodePoint )
{
	return CodePoint == 0x20 || CodePoint == 0x09 || CodePoint == 0x3000 || CodePoint == 0x0D;
}

bool NTextLayout::IsBreakableAnywhere( uint32 CodePoint )
{
	return ( CodePoint >= 0x1100 && CodePoint <= 0x11FF )        // ハングルの字母
		   || ( CodePoint >= 0x2E80 && CodePoint <= 0x303F )     // 部首・漢字圏の記号と句読点
		   || ( CodePoint >= 0x3040 && CodePoint <= 0x33FF )     // ひらがな・カタカナ・注音・ハングル互換・囲み文字
		   || ( CodePoint >= 0x3400 && CodePoint <= 0x4DBF )     // 漢字（拡張 A）
		   || ( CodePoint >= 0x4E00 && CodePoint <= 0x9FFF )     // 漢字（統合漢字）
		   || ( CodePoint >= 0xA000 && CodePoint <= 0xA4CF )     // 彝文字
		   || ( CodePoint >= 0xAC00 && CodePoint <= 0xD7AF )     // ハングルの音節
		   || ( CodePoint >= 0xF900 && CodePoint <= 0xFAFF )     // 互換漢字
		   || ( CodePoint >= 0xFE30 && CodePoint <= 0xFE4F )     // 互換形
		   || ( CodePoint >= 0xFF00 && CodePoint <= 0xFFEF )     // 全角形・半角形
		   || ( CodePoint >= 0x20000 && CodePoint <= 0x3FFFF );  // 漢字（拡張 B 以降）
}

bool NTextLayout::CanStartLine( uint32 CodePoint )
{
	return !Contains( CharsAvoidedAtLineStart, CodePoint );
}

bool NTextLayout::CanEndLine( uint32 CodePoint )
{
	return !Contains( CharsAvoidedAtLineEnd, CodePoint );
}

bool NTextLayout::CanBreakBetween( uint32 Before, uint32 After, bool bAvoidPunctuationAtLineEdges )
{
	// 空白は、行末にぶら下がる（空白の手前では折らない）。空白のあとでは折ってよい
	if ( IsWhitespace( After ) )
	{
		return false;
	}
	if ( IsWhitespace( Before ) )
	{
		return true;
	}
	if ( bAvoidPunctuationAtLineEdges && ( !CanStartLine( After ) || !CanEndLine( Before ) ) )
	{
		return false;
	}
	// 漢字・かななどは、1 文字ごとに折れる。英単語・数字の途中では折らない
	return IsBreakableAnywhere( Before ) || IsBreakableAnywhere( After );
}

FTextLayout NTextLayout::Build( const char* Text, const FTextLayoutOptions& Options, const ITextMeasurer& Measurer )
{
	FTextLayout Layout;
	if ( Text == nullptr )
	{
		return Layout;
	}
	Layout.Text = FString( Text );
	if ( !Layout.Text.IsValidUtf8() )
	{
		Layout.Text = Layout.Text.ToValidUtf8();
	}
	const int32 Length = Layout.Text.Len();
	if ( Length == 0 )
	{
		return Layout;
	}

	TArray<FTextGlyph> Measured;
	if ( !Measurer.GetGlyphs( *Layout.Text, Length, Measured ) )
	{
		return Layout;
	}
	TArray<FLayoutGlyph> Glyphs;
	for ( const FTextGlyph& Source : Measured )
	{
		if ( Source.Offset < 0 || Source.Bytes <= 0 || Source.Offset + Source.Bytes > Length )
		{
			continue;
		}
		FLayoutGlyph Glyph;
		int32 Offset = Source.Offset;
		Glyph.CodePoint = DecodeUtf8( *Layout.Text, Length, Offset );
		Glyph.Offset = Source.Offset;
		Glyph.Bytes = Source.Bytes;
		Glyph.Width = NMath::Max( Source.Width, 0 );
		Glyphs.Add( Glyph );
	}

	FLineCollector Lines( Layout, Options.MaxLines );
	int32 ParagraphStart = 0;
	while ( true )
	{
		// 段落は、改行（LF）まで
		int32 ParagraphEnd = ParagraphStart;
		while ( ParagraphEnd < Glyphs.Num() && Glyphs[ParagraphEnd].CodePoint != 0x0A )
		{
			++ParagraphEnd;
		}
		if ( !LayoutParagraph( Glyphs, ParagraphStart, ParagraphEnd, Length, Options, Lines ) )
		{
			break;
		}
		if ( ParagraphEnd >= Glyphs.Num() )
		{
			break;
		}
		// 改行の次から。改行が最後なら、そのあとに空の行が 1 つ続く（DxLib の行の数え方と同じ）
		ParagraphStart = ParagraphEnd + 1;
	}
	return Layout;
}
