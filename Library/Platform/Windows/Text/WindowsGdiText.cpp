// SPDX-License-Identifier: MIT
#include "WindowsGdiText.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

#pragma comment( lib, "gdi32.lib" )

namespace
{
	constexpr int32 MaxTextBytes = 65536;       // 1 つの文字列の上限
	constexpr int32 MaxImageSide = 4096;        // 画像の各辺の上限
	constexpr int32 MaxImagePixels = 4 * 1024 * 1024;
	constexpr int32 MaxCachedFonts = 64;        // これを超えたら、作ったフォントを全部作り直す
	constexpr int32 MaxCachedGlyphs = 16384;    // キャッシュする文字の数の上限（超えたら、全部捨てて、数え直す）
	constexpr int64 MaxCachedGlyphBytes = 32ll * 1024 * 1024; // キャッシュする被覆率の大きさの上限
	constexpr int32 LinePadding = 1;            // 1 行の文字の上下に足す余白（DxLib のフォントは、1 行の高さが、大きさより 2 大きい）

	// 太さ（0..9）を、OS の太さにする。2 階調の文字は、DxLib と同じく 100 倍する（標準の 6 は 600 で、太字になる）。
	// なめらかな文字は、OS の太字にすると、DxLib より、およそ 1 画素太くなり、字の送りも合わなくなるので、4 までに収める（字の送りは、GetBoldAdvance で足す）。
	int32 ToGdiWeight( int32 Thickness, bool bSmooth )
	{
		return NMath::Clamp( bSmooth ? NMath::Min( Thickness, 4 ) : Thickness, 0, 9 ) * 100;
	}

	// 標準のなめらかさで、太さが標準（6）以上のとき、DxLib が 1 文字ごとに足す送り（画素）。DxLib の実測: 大きさ 16 で 0、22 で 1、32 で 3（およそ 0.19 x ( 大きさ - 16 )）。
	// 2 階調の文字は、OS の太字の送りが DxLib と合う（大きさ 22 で確かめた）ので、足さない
	int32 GetBoldAdvance( const FRenderTextFont& Font )
	{
		return Font.Antialias == 1 && Font.Thickness >= 6 ? NMath::Max( 0, NMath::RoundToInt( 0.19f * static_cast<float32>( Font.Size - 16 ) ) ) : 0;
	}

	// UTF-8 の 1 文字を読む。壊れていれば false。OutBytes は、その文字のバイト数
	bool DecodeUtf8( const char* Text, int32 Remaining, uint32& OutCodePoint, int32& OutBytes )
	{
		const uint8 Lead = static_cast<uint8>( Text[0] );
		int32 Bytes = 0;
		uint32 Value = 0;
		if ( Lead < 0x80 )
		{
			Bytes = 1;
			Value = Lead;
		}
		else if ( ( Lead & 0xE0 ) == 0xC0 )
		{
			Bytes = 2;
			Value = Lead & 0x1F;
		}
		else if ( ( Lead & 0xF0 ) == 0xE0 )
		{
			Bytes = 3;
			Value = Lead & 0x0F;
		}
		else if ( ( Lead & 0xF8 ) == 0xF0 )
		{
			Bytes = 4;
			Value = Lead & 0x07;
		}
		else
		{
			return false;
		}
		if ( Bytes > Remaining )
		{
			return false;
		}
		for ( int32 Index = 1; Index < Bytes; ++Index )
		{
			const uint8 Next = static_cast<uint8>( Text[Index] );
			if ( ( Next & 0xC0 ) != 0x80 )
			{
				return false;
			}
			Value = ( Value << 6 ) | ( Next & 0x3F );
		}
		// 過長な表現・サロゲート・範囲の外は、壊れた文字として扱う
		const uint32 Minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
		if ( Value < Minimum[Bytes] || Value > 0x10FFFF || ( Value >= 0xD800 && Value <= 0xDFFF ) )
		{
			return false;
		}
		OutCodePoint = Value;
		OutBytes = Bytes;
		return true;
	}
}

CWindowsGdiText::~CWindowsGdiText()
{
	if ( Context && PreviousFont )
	{
		SelectObject( Context, PreviousFont );
	}
	for ( const auto& Pair : Fonts )
	{
		DeleteObject( Pair.Value );
	}
	Fonts.Reset();
	FontIds.Reset();
	Glyphs.Reset();
	if ( Context )
	{
		DeleteDC( Context );
	}
}

bool CWindowsGdiText::EnsureContext()
{
	if ( !Context )
	{
		Context = CreateCompatibleDC( nullptr );
	}
	return Context != nullptr;
}

bool CWindowsGdiText::SelectFont( const FRenderTextFont& Font )
{
	if ( !EnsureContext() )
	{
		return false;
	}
	const FString Key = Font.ToKey();
	HFONT Handle = nullptr;
	if ( const HFONT* Found = Fonts.Find( Key ) )
	{
		Handle = *Found;
		const int32* FoundId = FontIds.Find( Key );
		CurrentFontId = FoundId ? *FoundId : 0;
	}
	else
	{
		if ( Fonts.Num() >= MaxCachedFonts )
		{
			// 選んだままのフォントは消せないので、元の選択へ戻してから、全部を消す
			if ( PreviousFont )
			{
				SelectObject( Context, PreviousFont );
			}
			for ( const auto& Pair : Fonts )
			{
				DeleteObject( Pair.Value );
			}
			Fonts.Reset();
			FontIds.Reset(); // 番号とキャッシュも、フォントと一緒に捨てる。
			Glyphs.Reset();
			GlyphBytes = 0;
		}
		const TArray<wchar_t> Face = Font.Face.ToWide();
		const wchar_t* FaceName = Font.Face.IsEmpty() ? L"MS Gothic" : Face.GetData();   // 既定は、DxLib の既定と同じフォント
		Handle = CreateFontW( NMath::Clamp( Font.Size, 1, 1024 ), 0, 0, 0, ToGdiWeight( Font.Thickness, Font.Antialias >= 1 ), Font.bItalic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, Font.Antialias != 0 ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, FaceName );
		if ( !Handle )
		{
			return false;
		}
		Fonts.Add( Key, Handle );
		CurrentFontId = NextFontId++;
		FontIds.Add( Key, CurrentFontId );
	}
	const HGDIOBJ Previous = SelectObject( Context, Handle );
	if ( !Previous || Previous == HGDI_ERROR )
	{
		return false;
	}
	if ( !PreviousFont )
	{
		PreviousFont = Previous;   // 最初に選ばれていた、OS の既定のフォント
	}
	return true;
}

CWindowsGdiText::FCachedGlyph& CWindowsGdiText::GetGlyph( wchar_t Unit )
{
	if ( Glyphs.Num() >= MaxCachedGlyphs || GlyphBytes >= MaxCachedGlyphBytes )
	{
		Glyphs.Reset(); // 上限を超えたら、全部捨てて、使うものから数え直す。
		GlyphBytes = 0;
	}
	const uint64 Key = ( static_cast<uint64>( static_cast<uint32>( CurrentFontId ) ) << 32 ) | static_cast<uint64>( static_cast<uint16>( Unit ) );
	if ( FCachedGlyph* Found = Glyphs.Find( Key ) )
	{
		return *Found;
	}
	return Glyphs.Add( Key, FCachedGlyph() );
}

bool CWindowsGdiText::GetMetrics( const FRenderTextFont& Font, FFontMetrics& OutMetrics )
{
	TEXTMETRICW Metrics = {};
	if ( !SelectFont( Font ) || !GetTextMetricsW( Context, &Metrics ) )
	{
		return false;
	}
	const int32 Edge = NMath::Clamp( Font.EdgeSize, 0, 64 );
	FFontMetrics Result;
	Result.Size = Font.Size;
	Result.GlyphHeight = Metrics.tmHeight + ( LinePadding + Edge ) * 2;
	Result.LineHeight = Font.LineHeight >= 0 ? Font.LineHeight : Result.GlyphHeight;
	Result.Ascent = Metrics.tmAscent + LinePadding + Edge;
	Result.MaxWidth = Metrics.tmMaxCharWidth + Font.LetterSpacing + Edge * 2;
	Result.EdgeSize = Edge;
	Result.LetterSpacing = Font.LetterSpacing;
	OutMetrics = Result;
	return true;
}

bool CWindowsGdiText::Arrange( const FRenderTextFont& Font, TArrayView<const char> Text, TArray<FCharacter>& OutCharacters, FTextSize& OutSize )
{
	TEXTMETRICW Metrics = {};
	if ( Text.Num() < 0 || Text.Num() > MaxTextBytes || ( Text.Num() > 0 && !Text.GetData() ) || !GetTextMetricsW( Context, &Metrics ) )
	{
		return false;
	}
	const int32 Edge = NMath::Clamp( Font.EdgeSize, 0, 64 );
	const int32 GlyphHeight = Metrics.tmHeight + ( LinePadding + Edge ) * 2;
	const int32 LineAdvance = Font.LineHeight >= 0 ? Font.LineHeight : GlyphHeight;
	SIZE SpaceSize = {};
	GetTextExtentPoint32W( Context, L" ", 1, &SpaceSize );

	TArray<FCharacter> Characters;
	FTextSize Size;
	int32 X = 0, Y = 0;
	int32 Offset = 0;
	while ( Offset < Text.Num() )
	{
		uint32 CodePoint = 0;
		int32 Bytes = 0;
		if ( !DecodeUtf8( Text.GetData() + Offset, Text.Num() - Offset, CodePoint, Bytes ) )
		{
			return false;
		}
		FCharacter Character;
		Character.Offset = Offset;
		Character.Bytes = Bytes;
		Character.X = X;
		Character.Y = Y;
		Offset += Bytes;
		if ( Size.NumLines == 0 )
		{
			Size.NumLines = 1;
		}
		if ( CodePoint == '\n' )
		{
			Character.bNewLine = true;
			Characters.Add( Character );
			X = 0;
			Y += LineAdvance;
			++Size.NumLines;
			continue;
		}
		if ( CodePoint == '\r' )
		{
			Characters.Add( Character );   // 幅 0。描かない
			continue;
		}
		if ( CodePoint == '\t' )
		{
			Character.Width = SpaceSize.cx * 4 + Font.LetterSpacing + Edge * 2;   // タブは、空白 4 つ分
		}
		else
		{
			if ( CodePoint >= 0x10000 )
			{
				const uint32 Value = CodePoint - 0x10000;
				Character.Units[0] = static_cast<wchar_t>( 0xD800 + ( Value >> 10 ) );
				Character.Units[1] = static_cast<wchar_t>( 0xDC00 + ( Value & 0x3FF ) );
				Character.NumUnits = 2;
			}
			else
			{
				Character.Units[0] = static_cast<wchar_t>( CodePoint );
				Character.NumUnits = 1;
			}
			SIZE Extent = {};
			if ( Character.NumUnits == 1 )
			{
				FCachedGlyph& Cached = GetGlyph( Character.Units[0] ); // 送り幅は、フォントごと・文字ごとに 1 回だけ OS に聞く。
				if ( !Cached.bHasAdvance )
				{
					if ( !GetTextExtentPoint32W( Context, Character.Units, 1, &Extent ) )
					{
						return false;
					}
					Cached.Advance = Extent.cx;
					Cached.bHasAdvance = true;
				}
				Extent.cx = Cached.Advance;
			}
			else if ( !GetTextExtentPoint32W( Context, Character.Units, Character.NumUnits, &Extent ) )
			{
				return false;
			}
			Character.Width = Extent.cx + GetBoldAdvance( Font ) + Font.LetterSpacing + Edge * 2;
			for ( const FRenderCharImage& Image : Font.CharImages )
			{
				if ( Image.CodePoint == CodePoint )
				{
					Character.NumUnits = 0; // 文字は描かない（画像は、描画の命令を解釈する側が置く）。
					Character.Width = Image.Advance + Font.LetterSpacing + Edge * 2;
					break;
				}
			}
		}
		Character.Width = NMath::Max( 0, Character.Width );
		X += Character.Width;
		Size.Width = NMath::Max( Size.Width, X );
		Characters.Add( Character );
	}
	Size.Height = Size.NumLines > 0 ? ( Size.NumLines - 1 ) * LineAdvance + GlyphHeight : 0;
	OutCharacters = MoveTemp( Characters );
	OutSize = Size;
	return true;
}

bool CWindowsGdiText::Layout( const FRenderTextFont& Font, TArrayView<const char> Text, FTextSize& OutSize, TArray<FTextGlyph>* OutGlyphs )
{
	TArray<FCharacter> Characters;
	FTextSize Size;
	if ( !SelectFont( Font ) || !Arrange( Font, Text, Characters, Size ) )
	{
		return false;
	}
	if ( OutGlyphs )
	{
		TArray<FTextGlyph> Glyphs;
		Glyphs.Reserve( Characters.Num() );
		for ( const FCharacter& Character : Characters )
		{
			FTextGlyph Glyph;
			Glyph.Offset = Character.Offset;
			Glyph.Bytes = Character.Bytes;
			Glyph.X = Character.X;
			Glyph.Y = Character.Y;
			Glyph.Width = Character.Width;
			Glyphs.Add( Glyph );
		}
		*OutGlyphs = MoveTemp( Glyphs );
	}
	OutSize = Size;
	return true;
}

void CWindowsGdiText::DrawGrayGlyphs( const TArray<FCharacter>& Characters, int32 Edge, int32 Width, int32 Height, TArray<uint8>& Coverage )
{
	TEXTMETRICW Metrics = {};
	if ( !GetTextMetricsW( Context, &Metrics ) )
	{
		return;
	}
	const MAT2 Identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } }; // 変形なし。
	for ( const FCharacter& Character : Characters )
	{
		if ( Character.NumUnits != 1 )
		{
			continue;
		}
		FCachedGlyph& Cached = GetGlyph( Character.Units[0] ); // 被覆率は、フォントごと・文字ごとに 1 回だけ OS に数えさせる。
		if ( !Cached.bHasBitmap )
		{
			Cached.bHasBitmap = true;
			GLYPHMETRICS Glyph = {};
			const DWORD Size = GetGlyphOutlineW( Context, Character.Units[0], GGO_GRAY8_BITMAP, &Glyph, 0, nullptr, &Identity );
			if ( Size != GDI_ERROR && Size != 0 )
			{
				Cached.Bitmap.SetNum( static_cast<int32>( Size ) );
				if ( GetGlyphOutlineW( Context, Character.Units[0], GGO_GRAY8_BITMAP, &Glyph, Size, Cached.Bitmap.GetData(), &Identity ) == GDI_ERROR )
				{
					Cached.Bitmap.Reset();
				}
				else
				{
					Cached.Metrics = Glyph;
					GlyphBytes += static_cast<int64>( Size );
				}
			}
		}
		if ( Cached.Bitmap.IsEmpty() )
		{
			continue; // 空白などは、描くものが無い。
		}
		const GLYPHMETRICS& Glyph = Cached.Metrics;
		const uint8* Buffer = Cached.Bitmap.GetData();
		const int32 Pitch = static_cast<int32>( ( Glyph.gmBlackBoxX + 3 ) & ~3u ); // 1 行は 4 バイトの境界。
		const int32 OriginX = Character.X + Edge + Glyph.gmptGlyphOrigin.x;
		const int32 OriginY = Character.Y + Edge + LinePadding + Metrics.tmAscent - Glyph.gmptGlyphOrigin.y;
		for ( int32 Row = 0; Row < static_cast<int32>( Glyph.gmBlackBoxY ); ++Row )
		{
			const int32 Y = OriginY + Row;
			if ( Y < 0 || Y >= Height )
			{
				continue;
			}
			for ( int32 Column = 0; Column < static_cast<int32>( Glyph.gmBlackBoxX ); ++Column )
			{
				const int32 X = OriginX + Column;
				const int32 Level = Buffer[Row * Pitch + Column]; // 0..64
				if ( Level == 0 || X < 0 || X >= Width )
				{
					continue;
				}
				const uint8 Value = static_cast<uint8>( NMath::Min( 255, ( Level * 255 + 32 ) / 64 ) );
				uint8& Target = Coverage[Y * Width + X];
				Target = NMath::Max( Target, Value );
			}
		}
	}
}

void CWindowsGdiText::DrawSampledGlyphs( const FRenderTextFont& Font, const TArray<FCharacter>& Characters, int32 Scale, int32 Edge, int32 Width, int32 Height, TArray<uint8>& Coverage )
{
	Scale = NMath::Clamp( NMath::Min( Scale, 1024 / NMath::Max( 1, Font.Size ) ), 1, 16 ); // 作れるフォントの大きさの範囲で。
	FRenderTextFont Big = Font; // 大きな 2 階調のフォント。
	Big.Size = Font.Size * Scale;
	Big.Antialias = 0;
	Big.EdgeSize = 0;
	Big.LetterSpacing = 0;
	Big.LineHeight = -1;
	Big.Thickness = NMath::Min( Font.Thickness, 4 ); // 縮めるので、2 階調でも、なめらかな文字の太さにする。
	TEXTMETRICW Metrics = {};
	if ( !SelectFont( Big ) || !GetTextMetricsW( Context, &Metrics ) )
	{
		return;
	}
	int32 CellWidth = Scale * 4; // 1 文字を大きく描く範囲（はみ出しの余白つき）。
	for ( const FCharacter& Character : Characters )
	{
		SIZE Extent = {};
		if ( Character.NumUnits == 1 && GetTextExtentPoint32W( Context, Character.Units, 1, &Extent ) )
		{
			CellWidth = NMath::Max( CellWidth, static_cast<int32>( Extent.cx ) + Scale * 4 );
		}
	}
	const int32 CellHeight = Metrics.tmHeight + Scale * 2;
	if ( static_cast<int64>( CellWidth ) * CellHeight > 64 * 1024 * 1024 )
	{
		return;
	}
	BITMAPINFO Info = {};
	Info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	Info.bmiHeader.biWidth = CellWidth;
	Info.bmiHeader.biHeight = -CellHeight;
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;
	void* Bytes = nullptr;
	const HBITMAP Bitmap = CreateDIBSection( Context, &Info, DIB_RGB_COLORS, &Bytes, nullptr, 0 );
	if ( !Bitmap || !Bytes )
	{
		if ( Bitmap )
		{
			DeleteObject( Bitmap );
		}
		return;
	}
	const HGDIOBJ PreviousBitmap = SelectObject( Context, Bitmap );
	if ( !PreviousBitmap || PreviousBitmap == HGDI_ERROR )
	{
		DeleteObject( Bitmap );
		return;
	}
	SetTextColor( Context, RGB( 255, 255, 255 ) );
	SetBkMode( Context, TRANSPARENT );
	SetTextAlign( Context, TA_LEFT | TA_TOP );
	const int32 OutWidth = ( CellWidth + Scale - 1 ) / Scale, OutHeight = ( CellHeight + Scale - 1 ) / Scale; // 縮めたあとの範囲。
	const uint8* Source = static_cast<const uint8*>( Bytes );
	for ( const FCharacter& Character : Characters )
	{
		if ( Character.NumUnits != 1 )
		{
			continue;
		}
		FMemory::Memzero( Bytes, static_cast<usize>( CellWidth ) * CellHeight * 4 );
		if ( !TextOutW( Context, 0, 0, Character.Units, 1 ) || !GdiFlush() )
		{
			continue;
		}
		for ( int32 OutY = 0; OutY < OutHeight; ++OutY )
		{
			const int32 Y = Character.Y + Edge + LinePadding + OutY;
			if ( Y < 0 || Y >= Height )
			{
				continue;
			}
			for ( int32 OutX = 0; OutX < OutWidth; ++OutX )
			{
				const int32 X = Character.X + Edge + OutX;
				if ( X < 0 || X >= Width )
				{
					continue;
				}
				int32 Count = 0; // 大きく描いた画素のうち、文字が載っている数。
				for ( int32 SubY = 0; SubY < Scale && OutY * Scale + SubY < CellHeight; ++SubY )
				{
					for ( int32 SubX = 0; SubX < Scale && OutX * Scale + SubX < CellWidth; ++SubX )
					{
						if ( Source[( ( OutY * Scale + SubY ) * CellWidth + OutX * Scale + SubX ) * 4 + 1] >= 128 )
						{
							++Count;
						}
					}
				}
				if ( Count == 0 )
				{
					continue;
				}
				const uint8 Value = static_cast<uint8>( ( Count * 255 + Scale * Scale / 2 ) / ( Scale * Scale ) );
				uint8& Target = Coverage[Y * Width + X];
				Target = NMath::Max( Target, Value );
			}
		}
	}
	SelectObject( Context, PreviousBitmap );
	DeleteObject( Bitmap );
}

bool CWindowsGdiText::NeedsSystemDraw( const FRenderTextFont& Font, const TArray<FCharacter>& Characters )
{
	for ( const FCharacter& Character : Characters )
	{
		if ( Character.NumUnits > 0 && ( Font.Antialias == 0 || Character.NumUnits != 1 ) )
		{
			return true;
		}
	}
	return false;
}

bool CWindowsGdiText::DrawSystemGlyphs( int32 Antialias, const TArray<FCharacter>& Characters, int32 Edge, int32 Width, int32 Height, TArray<uint8>& Coverage )
{
	BITMAPINFO Info = {};   // 左上から行の順の 32bit 画像
	Info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	Info.bmiHeader.biWidth = Width;
	Info.bmiHeader.biHeight = -Height;
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;
	void* Bytes = nullptr;   // OS が持ち、この呼び出しの間だけ読む画像
	const HBITMAP Bitmap = CreateDIBSection( Context, &Info, DIB_RGB_COLORS, &Bytes, nullptr, 0 );
	if ( !Bitmap || !Bytes )
	{
		if ( Bitmap )
		{
			DeleteObject( Bitmap );
		}
		return false;
	}
	const HGDIOBJ PreviousBitmap = SelectObject( Context, Bitmap );   // 読み出したあと、必ず元へ戻す
	if ( !PreviousBitmap || PreviousBitmap == HGDI_ERROR )
	{
		DeleteObject( Bitmap );
		return false;
	}
	FMemory::Memzero( Bytes, static_cast<usize>( Width ) * Height * 4 );
	SetTextColor( Context, RGB( 255, 255, 255 ) );
	SetBkMode( Context, TRANSPARENT );
	SetTextAlign( Context, TA_LEFT | TA_TOP );
	bool bDrawn = true;
	for ( const FCharacter& Character : Characters )
	{
		if ( Character.NumUnits > 0 && ( Antialias == 0 || Character.NumUnits != 1 ) ) // 2 つの UTF-16 の文字は、OS の描き方で描く
		{
			bDrawn = TextOutW( Context, Character.X + Edge, Character.Y + Edge + LinePadding, Character.Units, Character.NumUnits ) != FALSE && bDrawn;
		}
	}
	bDrawn = GdiFlush() != FALSE && bDrawn;   // CPU で画素を読む前に、OS 側の描画を終わらせる
	if ( bDrawn )
	{
		Coverage.SetNum( Width * Height );
		const uint8* Source = static_cast<const uint8*>( Bytes );   // OS の BGR を、被覆率にする
		for ( int32 Index = 0; Index < Coverage.Num(); ++Index )
		{
			Coverage[Index] = NMath::Max( Source[Index * 4], Source[Index * 4 + 1], Source[Index * 4 + 2] );   // 色のにじみを、文字の色へ混ぜない
		}
	}
	SelectObject( Context, PreviousBitmap );
	DeleteObject( Bitmap );
	return bDrawn;
}

bool CWindowsGdiText::Rasterize( const FRenderTextFont& Font, TArrayView<const char> Text, TArray<uint8>& OutCoverage, TArray<uint8>& OutEdge, int32& OutWidth, int32& OutHeight )
{
	TArray<FCharacter> Characters;
	FTextSize Size;
	if ( Text.Num() <= 0 || !SelectFont( Font ) || !Arrange( Font, Text, Characters, Size ) )
	{
		return false;
	}
	if ( Size.Height <= 0 )
	{
		return false;
	}
	// 画像の大きさの上限（各辺 4096・4M 画素）を超える文字列は、はみ出す右・下を切った画像にする（DxLib が、はみ出す文字を切って描くのと同じ）。
	// 文字を描く処理は、画像の外の画素を書かない（Layout の大きさより小さくなる）
	const int32 Width = NMath::Min( NMath::Max( 1, Size.Width ), MaxImageSide );   // 改行だけの文字列も、透明な 1 列として持つ
	int32 Height = NMath::Min( Size.Height, MaxImageSide );
	if ( static_cast<int64>( Width ) * Height > MaxImagePixels )
	{
		Height = MaxImagePixels / Width;
	}
	const int32 Edge = NMath::Clamp( Font.EdgeSize, 0, 64 );
	TArray<uint8> Coverage;
	if ( NeedsSystemDraw( Font, Characters ) )
	{
		if ( !DrawSystemGlyphs( Font.Antialias, Characters, Edge, Width, Height, Coverage ) )
		{
			return false;
		}
	}
	else
	{
		Coverage.SetNum( Width * Height, 0 ); // OS に描かせる文字が無いときは、OS の画像を作らずに、空の被覆率から始める。
	}
	if ( Font.Antialias == 1 )
	{
		DrawGrayGlyphs( Characters, Edge, Width, Height, Coverage );
	}
	else if ( Font.Antialias > 1 )
	{
		DrawSampledGlyphs( Font, Characters, Font.Antialias, Edge, Width, Height, Coverage );
		SelectFont( Font ); // 大きなフォントを選んだので、戻す。
	}
	TArray<uint8> EdgeCoverage;
	if ( Edge > 0 )
	{
		// 文字の被覆率を、縁取りの太さだけ太らせる（円の形の範囲の最大値）。先に横、次に縦へ広げると四角くなるので、円の範囲をそのまま見る
		EdgeCoverage.SetNum( Width * Height );
		TArray<int32> Reach;   // 縦のずれごとの、横に見る範囲
		Reach.SetNum( Edge * 2 + 1 );
		for ( int32 Dy = -Edge; Dy <= Edge; ++Dy )
		{
			Reach[Dy + Edge] = static_cast<int32>( NMath::Sqrt( static_cast<float32>( Edge * Edge - Dy * Dy ) + 0.5f ) );
		}
		for ( int32 Y = 0; Y < Height; ++Y )
		{
			for ( int32 X = 0; X < Width; ++X )
			{
				uint8 Best = 0;
				for ( int32 Dy = -Edge; Dy <= Edge && Best < 255; ++Dy )
				{
					const int32 SourceY = Y + Dy;
					if ( SourceY < 0 || SourceY >= Height )
					{
						continue;
					}
					const int32 Span = Reach[Dy + Edge];
					const int32 First = NMath::Max( 0, X - Span ), Last = NMath::Min( Width - 1, X + Span );
					const uint8* Row = Coverage.GetData() + SourceY * Width;
					for ( int32 SourceX = First; SourceX <= Last; ++SourceX )
					{
						Best = NMath::Max( Best, Row[SourceX] );
					}
				}
				EdgeCoverage[Y * Width + X] = Best;
			}
		}
	}
	OutCoverage = MoveTemp( Coverage );
	OutEdge = MoveTemp( EdgeCoverage );
	OutWidth = Width;
	OutHeight = Height;
	return true;
}
