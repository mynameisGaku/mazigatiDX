// SPDX-License-Identifier: MIT
#include "WindowsFontStore.h"
#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"

#pragma comment( lib, "gdi32.lib" )

namespace
{
	// '\0' までの長さ（上限つき）。nullptr は 0
	int32 TextLength( const char* Text )
	{
		int32 Length = 0;
		while ( Text && Text[Length] != '\0' && Length < 65536 )
		{
			++Length;
		}
		return Length;
	}

	// 環境にあるフォントの名前を集める
	int CALLBACK CollectFontName( const LOGFONTW* Font, const TEXTMETRICW*, DWORD, LPARAM Param )
	{
		TArray<FString>* Names = reinterpret_cast<TArray<FString>*>( Param );
		if ( Font && Font->lfFaceName[0] != L'@' )   // 縦書き用の名前（@ で始まる）は出さない
		{
			Names->AddUnique( FString( Font->lfFaceName ) );
		}
		return 1;
	}

	void EnumerateFonts( const wchar_t* Face, TArray<FString>& OutNames )
	{
		const HDC Context = CreateCompatibleDC( nullptr );
		if ( !Context )
		{
			return;
		}
		LOGFONTW Filter = {};
		Filter.lfCharSet = DEFAULT_CHARSET;
		if ( Face )
		{
			for ( int32 Index = 0; Index < LF_FACESIZE - 1 && Face[Index] != L'\0'; ++Index )
			{
				Filter.lfFaceName[Index] = Face[Index];
			}
		}
		EnumFontFamiliesExW( Context, &Filter, CollectFontName, reinterpret_cast<LPARAM>( &OutNames ), 0 );
		DeleteDC( Context );
	}
}

// UTF-8 の先頭の 1 文字を読む。壊れていれば false。
static bool DecodeFirstCodePoint( const char* Text, int32 Remaining, uint32& OutCodePoint, int32& OutBytes )
{
	if ( !Text || Remaining <= 0 )
	{
		return false;
	}
	const uint8 Lead = static_cast<uint8>( Text[0] );
	int32 Bytes = Lead < 0x80 ? 1 : ( Lead & 0xE0 ) == 0xC0 ? 2 : ( Lead & 0xF0 ) == 0xE0 ? 3 : ( Lead & 0xF8 ) == 0xF0 ? 4 : 0;
	if ( Bytes == 0 || Bytes > Remaining )
	{
		return false;
	}
	uint32 Value = Bytes == 1 ? Lead : Bytes == 2 ? ( Lead & 0x1F ) : Bytes == 3 ? ( Lead & 0x0F ) : ( Lead & 0x07 );
	for ( int32 Index = 1; Index < Bytes; ++Index )
	{
		const uint8 Next = static_cast<uint8>( Text[Index] );
		if ( ( Next & 0xC0 ) != 0x80 )
		{
			return false;
		}
		Value = ( Value << 6 ) | ( Next & 0x3F );
	}
	OutCodePoint = Value;
	OutBytes = Bytes;
	return true;
}

bool CWindowsFontStore::AddCharImage( int32 Font, const char* Character, int32 Image, int32 DrawX, int32 DrawY, int32 Advance )
{
	uint32 CodePoint = 0;
	int32 Bytes = 0;
	if ( !Entries.IsValidIndex( Font ) || !Entries[Font].bUsed || !Character || !DecodeFirstCodePoint( Character, TextLength( Character ), CodePoint, Bytes ) || Image < 0 || Advance < 0 || Advance > 4096 )
	{
		return false;
	}
	FRenderCharImage Entry;
	Entry.CodePoint = CodePoint;
	Entry.Image = Image;
	Entry.DrawX = DrawX;
	Entry.DrawY = DrawY;
	Entry.Advance = Advance;
	TArray<FRenderCharImage>& Images = Entries[Font].Font.CharImages;
	for ( FRenderCharImage& Existing : Images )
	{
		if ( Existing.CodePoint == CodePoint )
		{
			Existing = Entry;
			return true;
		}
	}
	Images.Add( Entry );
	return true;
}

void CWindowsFontStore::RemoveCharImage( int32 Font, const char* Character )
{
	uint32 CodePoint = 0;
	int32 Bytes = 0;
	if ( !Entries.IsValidIndex( Font ) || !Entries[Font].bUsed || !Character || !DecodeFirstCodePoint( Character, TextLength( Character ), CodePoint, Bytes ) )
	{
		return;
	}
	TArray<FRenderCharImage>& Images = Entries[Font].Font.CharImages;
	for ( int32 Index = 0; Index < Images.Num(); ++Index )
	{
		if ( Images[Index].CodePoint == CodePoint )
		{
			Images.RemoveAt( Index );
			return;
		}
	}
}

bool CWindowsFontStore::GetFontImagePlacements( int32 Font, const char* InText, int32 Length, TArray<FFontImagePlacement>& Out )
{
	FRenderTextFont View;
	FTextSize Size;
	TArray<FTextGlyph> Glyphs;
	if ( Length < 0 || ( Length > 0 && !InText ) || !GetFontDrawView( Font, View ) || View.CharImages.IsEmpty() || !Text.Layout( View, TArrayView<const char>( InText, Length ), Size, &Glyphs ) )
	{
		return false;
	}
	TArray<FFontImagePlacement> Result;
	for ( const FTextGlyph& Glyph : Glyphs )
	{
		uint32 CodePoint = 0;
		int32 Bytes = 0;
		if ( Glyph.Offset < 0 || Glyph.Offset >= Length || !DecodeFirstCodePoint( InText + Glyph.Offset, Length - Glyph.Offset, CodePoint, Bytes ) )
		{
			continue;
		}
		for ( const FRenderCharImage& Image : View.CharImages )
		{
			if ( Image.CodePoint == CodePoint )
			{
				FFontImagePlacement Placement;
				Placement.Image = Image.Image;
				Placement.X = Glyph.X + View.EdgeSize + Image.DrawX;
				Placement.Y = Glyph.Y + View.EdgeSize + Image.DrawY;
				Result.Add( Placement );
				break;
			}
		}
	}
	if ( Result.IsEmpty() )
	{
		return false;
	}
	Out = MoveTemp( Result );
	return true;
}

int32 CWindowsFontStore::MakeFont( const FFontDesc& Desc )
{
	if ( Desc.Size < 1 || Desc.Size > 1024 || Desc.EdgeSize < 0 || Desc.EdgeSize > 64 )
	{
		return NAsset::InvalidHandle;
	}
	FEntry Entry;
	Entry.Font.Face = Desc.Face;
	Entry.Font.Size = Desc.Size;
	Entry.Font.Thickness = NMath::Clamp( Desc.Thickness, 0, 9 );
	Entry.Font.EdgeSize = Desc.EdgeSize;
	Entry.Font.LetterSpacing = Desc.LetterSpacing;
	Entry.Font.LineHeight = Desc.LineHeight;
	Entry.Font.bItalic = Desc.bItalic;
	Entry.Font.Antialias = Desc.Antialias == EFontAntialias::None ? 0 : Desc.Antialias == EFontAntialias::High4x4 ? 4 : Desc.Antialias == EFontAntialias::High8x8 ? 8 : Desc.Antialias == EFontAntialias::High16x16 ? 16 : 1;
	Entry.bUsed = true;
	FFontMetrics Metrics;
	if ( !Text.GetMetrics( Entry.Font, Metrics ) )
	{
		return NAsset::InvalidHandle;   // OS がフォントを作れない
	}
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		if ( !Entries[Index].bUsed )
		{
			Entries[Index] = Entry;
			return Index;
		}
	}
	Entries.Add( Entry );
	return Entries.Num() - 1;
}

void CWindowsFontStore::DeleteFont( int32 Font )
{
	if ( Entries.IsValidIndex( Font ) )
	{
		Entries[Font] = FEntry();
	}
}

int32 CWindowsFontStore::GetNumFonts() const
{
	int32 Count = 0;
	for ( const FEntry& Entry : Entries )
	{
		Count += Entry.bUsed ? 1 : 0;
	}
	return Count;
}

bool CWindowsFontStore::GetFontDrawView( int32 Font, FRenderTextFont& Out )
{
	if ( Font < 0 )
	{
		Out = FRenderTextFont();   // 既定のフォント
		Out.Size = 18;             // DxLib の既定の文字と同じ大きさ（半角の幅が 9 画素。実測）
		return true;
	}
	if ( !Entries.IsValidIndex( Font ) || !Entries[Font].bUsed )
	{
		return false;
	}
	Out = Entries[Font].Font;
	return true;
}

bool CWindowsFontStore::MeasureFontText( int32 Font, const char* InText, int32 Length, FTextSize& Out )
{
	FRenderTextFont View;
	if ( Length < 0 || ( Length > 0 && !InText ) || !GetFontDrawView( Font, View ) )
	{
		return false;
	}
	return Text.Layout( View, TArrayView<const char>( InText, Length ), Out );
}

int32 CWindowsFontStore::MeasureText( int32 Font, const char* InText )
{
	FTextSize Size;
	return Font >= 0 && MeasureFontText( Font, InText, TextLength( InText ), Size ) ? Size.Width : 0;
}

bool CWindowsFontStore::GetFontMetrics( int32 Font, FFontMetrics& OutMetrics )
{
	FRenderTextFont View;
	return Font >= 0 && GetFontDrawView( Font, View ) && Text.GetMetrics( View, OutMetrics );
}

bool CWindowsFontStore::MeasureTextSize( int32 Font, const char* InText, FTextSize& OutSize )
{
	return Font >= 0 && MeasureFontText( Font, InText, TextLength( InText ), OutSize );
}

bool CWindowsFontStore::GetTextGlyphs( int32 Font, const char* InText, TArray<FTextGlyph>& OutGlyphs )
{
	FRenderTextFont View;
	FTextSize Size;
	return Font >= 0 && GetFontDrawView( Font, View ) && Text.Layout( View, TArrayView<const char>( InText, TextLength( InText ) ), Size, &OutGlyphs );
}

bool CWindowsFontStore::IsFontAvailable( const FString& Face )
{
	if ( Face.IsEmpty() )
	{
		return false;
	}
	TArray<FString> Names;
	const TArray<wchar_t> Wide = Face.ToWide();
	EnumerateFonts( Wide.GetData(), Names );
	for ( const FString& Name : Names )
	{
		if ( Name.Equals( Face, ESearchCase::IgnoreCase ) )
		{
			return true;
		}
	}
	return false;
}

void CWindowsFontStore::GetFontNames( TArray<FString>& OutNames )
{
	TArray<FString> Names;
	EnumerateFonts( nullptr, Names );
	Names.Sort();
	OutNames = MoveTemp( Names );
}

bool CWindowsFontStore::AddFontFile( const FString& Path )
{
	const TArray<wchar_t> Wide = Path.ToWide();
	if ( Path.IsEmpty() || AddFontResourceExW( Wide.GetData(), FR_PRIVATE, nullptr ) <= 0 )
	{
		return false;
	}
	FontFiles.Add( Path );
	return true;
}

void CWindowsFontStore::RemoveFontFile( const FString& Path )
{
	const int32 Index = FontFiles.Find( Path );
	if ( Index == INDEX_NONE )
	{
		return;
	}
	const TArray<wchar_t> Wide = Path.ToWide();
	RemoveFontResourceExW( Wide.GetData(), FR_PRIVATE, nullptr );
	FontFiles.RemoveAt( Index );
}
