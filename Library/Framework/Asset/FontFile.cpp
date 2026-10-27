#include "FontFile.h"

#include <cstdio>

#include "Library/Core/Collection/Array.h"

namespace
{
	// 範囲外を読まないための窓口（ファイルはビッグエンディアン）
	struct FReader
	{
		const uint8* Data;
		usize Size;

		bool CanRead( usize Offset, usize Length ) const { return Offset <= Size && Length <= Size - Offset; }

		uint32 U16( usize Offset ) const { return CanRead( Offset, 2 ) ? ( static_cast<uint32>( Data[Offset] ) << 8 ) | Data[Offset + 1] : 0; }
		uint32 U32( usize Offset ) const
		{
			return CanRead( Offset, 4 ) ? ( static_cast<uint32>( Data[Offset] ) << 24 ) | ( static_cast<uint32>( Data[Offset + 1] ) << 16 )
											  | ( static_cast<uint32>( Data[Offset + 2] ) << 8 ) | Data[Offset + 3]
										: 0;
		}
	};

	constexpr uint32 Tag( char A, char B, char C, char D )
	{
		return ( static_cast<uint32>( static_cast<uint8>( A ) ) << 24 ) | ( static_cast<uint32>( static_cast<uint8>( B ) ) << 16 )
			   | ( static_cast<uint32>( static_cast<uint8>( C ) ) << 8 ) | static_cast<uint32>( static_cast<uint8>( D ) );
	}

	// UTF-16BE の文字列から FString を作る
	FString FromUtf16BE( const FReader& Reader, usize Offset, usize Length )
	{
		TArray<wchar_t> Wide;
		for ( usize i = 0; i + 1 < Length; i += 2 ) Wide.Add( static_cast<wchar_t>( Reader.U16( Offset + i ) ) );
		Wide.Add( L'\0' );
		return FString( Wide.GetData() );
	}

	FString FromMacRoman( const FReader& Reader, usize Offset, usize Length )
	{
		FString Result;
		for ( usize i = 0; i < Length; ++i )
		{
			const char Ch = static_cast<char>( Reader.Data[Offset + i] );
			if ( static_cast<uint8>( Ch ) < 0x80 ) Result += Ch;   // ASCII の範囲だけ（フォント名はほぼ英数字）
		}
		return Result;
	}
}

bool NFontFile::ReadFaceNameFromMemory( const uint8* Data, usize Size, FString& OutFaceName )
{
	if ( Data == nullptr || Size < 12 ) return false;
	const FReader Reader { Data, Size };

	// .ttc（フォントの集まり）は、最初のフォントを使う
	usize FontOffset = 0;
	if ( Reader.U32( 0 ) == Tag( 't', 't', 'c', 'f' ) ) FontOffset = Reader.U32( 12 );

	const uint32 Version = Reader.U32( FontOffset );
	if ( Version != 0x00010000u && Version != Tag( 'O', 'T', 'T', 'O' ) && Version != Tag( 't', 'r', 'u', 'e' ) ) return false;

	const uint32 NumTables = Reader.U16( FontOffset + 4 );
	usize NameTable = 0;
	usize NameTableLength = 0;
	for ( uint32 i = 0; i < NumTables; ++i )
	{
		const usize Record = FontOffset + 12 + static_cast<usize>( i ) * 16;
		if ( Reader.U32( Record ) == Tag( 'n', 'a', 'm', 'e' ) )
		{
			NameTable = Reader.U32( Record + 8 );
			NameTableLength = Reader.U32( Record + 12 );
			break;
		}
	}
	if ( NameTable == 0 || !Reader.CanRead( NameTable, NameTableLength ) ) return false;

	const uint32 Count = Reader.U16( NameTable + 2 );
	const usize StringStorage = NameTable + Reader.U16( NameTable + 4 );

	// nameID 1（ファミリー名）の候補から、英語（Windows, 0x409）を最優先に選ぶ
	int32 BestScore = -1;
	FString Best;
	for ( uint32 i = 0; i < Count; ++i )
	{
		const usize Record = NameTable + 6 + static_cast<usize>( i ) * 12;
		if ( !Reader.CanRead( Record, 12 ) ) break;

		const uint32 PlatformId = Reader.U16( Record );
		const uint32 EncodingId = Reader.U16( Record + 2 );
		const uint32 LanguageId = Reader.U16( Record + 4 );
		const uint32 NameId = Reader.U16( Record + 6 );
		const usize Length = Reader.U16( Record + 8 );
		const usize Offset = StringStorage + Reader.U16( Record + 10 );
		if ( NameId != 1 || Length == 0 || !Reader.CanRead( Offset, Length ) ) continue;

		int32 Score = -1;
		FString Candidate;
		if ( PlatformId == 3 && ( EncodingId == 1 || EncodingId == 10 ) )
		{
			Score = LanguageId == 0x0409 ? 40 : 30;
			Candidate = FromUtf16BE( Reader, Offset, Length );
		}
		else if ( PlatformId == 0 )
		{
			Score = 20;
			Candidate = FromUtf16BE( Reader, Offset, Length );
		}
		else if ( PlatformId == 1 && EncodingId == 0 )
		{
			Score = 10;
			Candidate = FromMacRoman( Reader, Offset, Length );
		}
		if ( Score > BestScore && !Candidate.IsEmpty() )
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	if ( BestScore < 0 ) return false;
	OutFaceName = Best;
	return true;
}

bool NFontFile::ReadFaceName( const FString& Path, FString& OutFaceName )
{
	FILE* File = nullptr;
	if ( fopen_s( &File, *Path, "rb" ) != 0 || File == nullptr ) return false;

	TArray<uint8> Bytes;
	uint8 Chunk[8192];
	usize Read = 0;
	while ( ( Read = std::fread( Chunk, 1, sizeof( Chunk ), File ) ) > 0 ) Bytes.Append( Chunk, static_cast<int32>( Read ) );
	std::fclose( File );

	return ReadFaceNameFromMemory( Bytes.GetData(), static_cast<usize>( Bytes.Num() ), OutFaceName );
}
