#include "String.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "Library/Core/Types/Utility.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
	//-------------------------------------------------------------------------
	// マルチバイト（Shift-JIS 等）の文字境界を考慮するためのヘルパー
	//-------------------------------------------------------------------------
	// Str[Index] から始まる文字のバイト数 (1 or 2)
	int32 CharSize( const char* Str, int32 Length, int32 Index )
	{
		if ( Index + 1 < Length && IsDBCSLeadByte( static_cast<BYTE>( Str[Index] ) ) ) return 2;
		return 1;
	}

	char ToLowerAscii( char Ch ) { return ( Ch >= 'A' && Ch <= 'Z' ) ? static_cast<char>( Ch - 'A' + 'a' ) : Ch; }
	char ToUpperAscii( char Ch ) { return ( Ch >= 'a' && Ch <= 'z' ) ? static_cast<char>( Ch - 'a' + 'A' ) : Ch; }
	bool IsSpace( char Ch ) { return Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n' || Ch == '\v' || Ch == '\f'; }

	// Str[Index..] が Sub と一致するか。Index は文字の先頭であること
	bool MatchAt( const char* Str, int32 Length, int32 Index, const char* Sub, int32 SubLength, ESearchCase SearchCase )
	{
		if ( Index + SubLength > Length ) return false;

		int32 i = 0;
		while ( i < SubLength )
		{
			if ( CharSize( Str, Length, Index + i ) == 2 )
			{
				// 全角文字はバイト完全一致。Sub 側も 2 バイト揃っている必要がある
				if ( i + 1 >= SubLength ) return false;
				if ( Str[Index + i] != Sub[i] || Str[Index + i + 1] != Sub[i + 1] ) return false;
				i += 2;
			}
			else
			{
				const char A = Str[Index + i];
				const char B = Sub[i];
				if ( SearchCase == ESearchCase::IgnoreCase ? ToLowerAscii( A ) != ToLowerAscii( B ) : A != B ) return false;
				i += 1;
			}
		}
		return true;
	}

	// 文字境界にある一致位置（bLast なら最後の一致）
	int32 FindImpl( const char* Str, int32 Length, const char* Sub, int32 SubLength, ESearchCase SearchCase, int32 StartIndex, bool bLast )
	{
		if ( SubLength == 0 ) return bLast ? Length : ( StartIndex < Length ? StartIndex : Length );
		if ( SubLength > Length ) return INDEX_NONE;

		int32 Found = INDEX_NONE;
		for ( int32 i = 0; i + SubLength <= Length; i += CharSize( Str, Length, i ) )
		{
			if ( i < StartIndex ) continue;
			if ( MatchAt( Str, Length, i, Sub, SubLength, SearchCase ) )
			{
				Found = i;
				if ( !bLast ) break;
			}
		}
		return Found;
	}

	// コードページ間の変換（ワイド文字を経由）
	FString Convert( const char* Src, int32 SrcLength, UINT FromCodePage, UINT ToCodePage )
	{
		if ( SrcLength <= 0 ) return FString();

		const int32 WideLength = MultiByteToWideChar( FromCodePage, 0, Src, SrcLength, nullptr, 0 );
		TArray<wchar_t> Wide;
		Wide.SetNum( WideLength );
		MultiByteToWideChar( FromCodePage, 0, Src, SrcLength, Wide.GetData(), WideLength );

		const int32 Length = WideCharToMultiByte( ToCodePage, 0, Wide.GetData(), WideLength, nullptr, 0, nullptr, nullptr );
		TArray<char> Bytes;
		Bytes.SetNum( Length );
		WideCharToMultiByte( ToCodePage, 0, Wide.GetData(), WideLength, Bytes.GetData(), Length, nullptr, nullptr );
		return FString( Bytes.GetData(), Length );
	}
}

#pragma region Construction

FString::FString( const char* Str )
{
	if ( Str ) Append( Str, static_cast<int32>( std::strlen( Str ) ) );
}

FString::FString( const char* Str, int32 Length )
{
	if ( Str ) Append( Str, Length );
}

FString::FString( const wchar_t* Str )
{
	if ( !Str || !*Str ) return;

	const int32 SrcLength = static_cast<int32>( std::wcslen( Str ) );
	const int32 Length    = WideCharToMultiByte( CP_ACP, 0, Str, SrcLength, nullptr, 0, nullptr, nullptr );
	Data.SetNum( Length + 1 );
	WideCharToMultiByte( CP_ACP, 0, Str, SrcLength, Data.GetData(), Length, nullptr, nullptr );
	Data[Length] = '\0';
}

FString FString::Repeat( char Ch, int32 Count )
{
	FString Result;
	if ( Count <= 0 ) return Result;
	Result.Data.SetNum( Count + 1, Ch );
	Result.Data[Count] = '\0';
	return Result;
}

FString FString::Printf( const char* Format, ... )
{
	va_list Args;
	va_start( Args, Format );
	FString Result = PrintfV( Format, Args );
	va_end( Args );
	return Result;
}

FString FString::PrintfV( const char* Format, va_list Args )
{
	FString Result;
	if ( !Format ) return Result;

	va_list ArgsCopy;
	va_copy( ArgsCopy, Args );
	const int Length = std::vsnprintf( nullptr, 0, Format, ArgsCopy );
	va_end( ArgsCopy );
	if ( Length <= 0 ) return Result;

	Result.Data.SetNum( Length + 1 );
	std::vsnprintf( Result.Data.GetData(), static_cast<usize>( Length ) + 1, Format, Args );
	return Result;
}

FString FString::FromInt( int64 Value )
{
	return Printf( "%lld", static_cast<long long>( Value ) );
}

FString FString::FromFloat( float64 Value, int32 Decimals )
{
	return Printf( "%.*f", Decimals < 0 ? 0 : Decimals, Value );
}

FString FString::FromUtf8( const char* Utf8 )
{
	if ( !Utf8 ) return FString();
	return Convert( Utf8, static_cast<int32>( std::strlen( Utf8 ) ), CP_UTF8, CP_ACP );
}

FString FString::Join( const TArray<FString>& Items, const FString& Separator )
{
	FString Result;
	for ( int32 i = 0; i < Items.Num(); ++i )
	{
		if ( i > 0 ) Result += Separator;
		Result += Items[i];
	}
	return Result;
}

#pragma endregion

#pragma region Append

FString& FString::Append( const char* Str, int32 Length )
{
	if ( !Str || Length <= 0 ) return *this;

	// 自分自身の一部を追加する場合は先にコピーする（再確保で無効になるため）
	const char* Begin = Data.GetData();
	if ( Begin && Str >= Begin && Str < Begin + Data.Num() )
	{
		const FString Copy( Str, Length );
		return Append( Copy.GetData(), Copy.Len() );
	}

	if ( Data.Num() > 0 ) Data.Pop();  // 終端を外す
	else Data.Reserve( Length + 1 );
	Data.Append( Str, Length );
	Data.Add( '\0' );
	return *this;
}

FString& FString::operator+=( const char* Str )
{
	if ( Str ) Append( Str, static_cast<int32>( std::strlen( Str ) ) );
	return *this;
}

FString& FString::operator/=( const FString& Path )
{
	if ( Path.IsEmpty() ) return *this;
	if ( IsEmpty() ) return *this = Path;

	// 末尾の文字が区切りか（全角文字の 2 バイト目の '\\' を誤判定しないよう文字単位で見る）
	const char* Str = GetData();
	const int32 Length = Len();
	int32 LastStart = 0;
	for ( int32 i = 0; i < Length; i += CharSize( Str, Length, i ) ) LastStart = i;

	const bool bEndsWithSep   = CharSize( Str, Length, LastStart ) == 1 && ( Str[LastStart] == '/' || Str[LastStart] == '\\' );
	const bool bStartsWithSep = Path[0] == '/' || Path[0] == '\\';

	if ( bEndsWithSep && bStartsWithSep ) Append( Path.GetData() + 1, Path.Len() - 1 );
	else if ( bEndsWithSep || bStartsWithSep ) *this += Path;
	else { *this += '/'; *this += Path; }
	return *this;
}

#pragma endregion

#pragma region Query

int32 FString::CharCount() const
{
	const char* Str = GetData();
	const int32 Length = Len();
	int32 Count = 0;
	for ( int32 i = 0; i < Length; i += CharSize( Str, Length, i ) ) ++Count;
	return Count;
}

bool FString::Equals( const FString& Other, ESearchCase SearchCase ) const
{
	return Len() == Other.Len() && MatchAt( GetData(), Len(), 0, Other.GetData(), Other.Len(), SearchCase );
}

int32 FString::Compare( const FString& Other, ESearchCase SearchCase ) const noexcept
{
	const char* A = GetData();
	const char* B = Other.GetData();
	const int32 LenA = Len();
	const int32 LenB = Other.Len();
	const int32 MinLen = LenA < LenB ? LenA : LenB;

	for ( int32 i = 0; i < MinLen; ++i )
	{
		char CA = A[i], CB = B[i];
		if ( SearchCase == ESearchCase::IgnoreCase ) { CA = ToLowerAscii( CA ); CB = ToLowerAscii( CB ); }
		if ( CA != CB ) return static_cast<unsigned char>( CA ) < static_cast<unsigned char>( CB ) ? -1 : 1;
	}
	return LenA == LenB ? 0 : ( LenA < LenB ? -1 : 1 );
}

int32 FString::CompareRaw( const char* Other ) const noexcept
{
	return std::strcmp( GetData(), Other ? Other : "" );
}

int32 FString::Find( const FString& Sub, ESearchCase SearchCase, int32 StartIndex ) const
{
	return FindImpl( GetData(), Len(), Sub.GetData(), Sub.Len(), SearchCase, StartIndex < 0 ? 0 : StartIndex, false );
}

int32 FString::FindLast( const FString& Sub, ESearchCase SearchCase ) const
{
	return FindImpl( GetData(), Len(), Sub.GetData(), Sub.Len(), SearchCase, 0, true );
}

bool FString::StartsWith( const FString& Prefix, ESearchCase SearchCase ) const
{
	return MatchAt( GetData(), Len(), 0, Prefix.GetData(), Prefix.Len(), SearchCase );
}

bool FString::EndsWith( const FString& Suffix, ESearchCase SearchCase ) const
{
	if ( Suffix.Len() > Len() ) return false;
	if ( Suffix.IsEmpty() ) return true;

	// 末尾の一致位置が文字境界であることを確認する
	const char* Str = GetData();
	const int32 Length = Len();
	const int32 Start  = Length - Suffix.Len();
	int32 i = 0;
	while ( i < Start ) i += CharSize( Str, Length, i );
	return i == Start && MatchAt( Str, Length, Start, Suffix.GetData(), Suffix.Len(), SearchCase );
}

#pragma endregion

#pragma region Substring

FString FString::Left( int32 Count ) const
{
	if ( Count <= 0 ) return FString();
	return FString( GetData(), Count < Len() ? Count : Len() );
}

FString FString::Right( int32 Count ) const
{
	const int32 N = Count < 0 ? 0 : ( Count > Len() ? Len() : Count );
	return FString( GetData() + Len() - N, N );
}

FString FString::LeftChop( int32 Count ) const
{
	const int32 N = Count < 0 ? 0 : ( Count > Len() ? Len() : Count );
	return Left( Len() - N );
}

FString FString::RightChop( int32 Count ) const
{
	const int32 N = Count < 0 ? 0 : ( Count > Len() ? Len() : Count );
	return FString( GetData() + N, Len() - N );
}

FString FString::Mid( int32 Start, int32 Count ) const
{
	if ( Start < 0 ) Start = 0;
	if ( Start >= Len() || Count <= 0 ) return FString();
	const int32 Available = Len() - Start;
	return FString( GetData() + Start, Count < Available ? Count : Available );
}

#pragma endregion

#pragma region Transform

FString FString::ToUpper() const
{
	FString Result( *this );
	char* Str = Result.Data.GetData();
	const int32 Length = Result.Len();
	for ( int32 i = 0; i < Length; )
	{
		const int32 Size = CharSize( Str, Length, i );
		if ( Size == 1 ) Str[i] = ToUpperAscii( Str[i] );
		i += Size;
	}
	return Result;
}

FString FString::ToLower() const
{
	FString Result( *this );
	char* Str = Result.Data.GetData();
	const int32 Length = Result.Len();
	for ( int32 i = 0; i < Length; )
	{
		const int32 Size = CharSize( Str, Length, i );
		if ( Size == 1 ) Str[i] = ToLowerAscii( Str[i] );
		i += Size;
	}
	return Result;
}

FString FString::TrimStart() const
{
	const char* Str = GetData();
	int32 i = 0;
	while ( i < Len() && IsSpace( Str[i] ) ) ++i;
	return FString( Str + i, Len() - i );
}

FString FString::TrimEnd() const
{
	// 全角文字の 2 バイト目は 0x40 以上なので空白と誤判定しない
	const char* Str = GetData();
	int32 End = Len();
	while ( End > 0 && IsSpace( Str[End - 1] ) ) --End;
	return FString( Str, End );
}

FString FString::TrimStartAndEnd() const
{
	return TrimStart().TrimEnd();
}

FString FString::Replace( const FString& From, const FString& To, ESearchCase SearchCase ) const
{
	FString Result( *this );
	Result.ReplaceInline( From, To, SearchCase );
	return Result;
}

int32 FString::ReplaceInline( const FString& From, const FString& To, ESearchCase SearchCase )
{
	if ( From.IsEmpty() || IsEmpty() ) return 0;

	const char* Str = GetData();
	const int32 Length = Len();

	FString Result;
	Result.Reserve( Length );
	int32 Count = 0;
	for ( int32 i = 0; i < Length; )
	{
		if ( MatchAt( Str, Length, i, From.GetData(), From.Len(), SearchCase ) )
		{
			Result += To;
			i += From.Len();
			++Count;
		}
		else
		{
			const int32 Size = CharSize( Str, Length, i );
			Result.Append( Str + i, Size );
			i += Size;
		}
	}
	if ( Count > 0 ) *this = MoveTemp( Result );
	return Count;
}

bool FString::Split( const FString& Delimiter, FString* OutLeft, FString* OutRight, ESearchCase SearchCase, bool bFromEnd ) const
{
	if ( Delimiter.IsEmpty() ) return false;

	const int32 Index = bFromEnd ? FindLast( Delimiter, SearchCase ) : Find( Delimiter, SearchCase );
	if ( Index == INDEX_NONE ) return false;

	// OutLeft / OutRight が this を指していても壊れないよう、先に両方作る
	FString LeftPart  = Left( Index );
	FString RightPart = RightChop( Index + Delimiter.Len() );
	if ( OutLeft ) *OutLeft = MoveTemp( LeftPart );
	if ( OutRight ) *OutRight = MoveTemp( RightPart );
	return true;
}

TArray<FString> FString::ParseIntoArray( const FString& Delimiter, bool bCullEmpty ) const
{
	TArray<FString> Result;
	if ( Delimiter.IsEmpty() )
	{
		if ( !( bCullEmpty && IsEmpty() ) ) Result.Add( *this );
		return Result;
	}

	int32 Start = 0;
	while ( true )
	{
		const int32 Index = Find( Delimiter, ESearchCase::CaseSensitive, Start );
		const int32 End   = Index == INDEX_NONE ? Len() : Index;
		if ( !( bCullEmpty && End == Start ) ) Result.Add( Mid( Start, End - Start ) );
		if ( Index == INDEX_NONE ) break;
		Start = Index + Delimiter.Len();
	}
	return Result;
}

TArray<FString> FString::ParseIntoLines( bool bCullEmpty ) const
{
	return Replace( "\r\n", "\n" ).ParseIntoArray( "\n", bCullEmpty );
}

#pragma endregion

#pragma region Number

TOptional<int64> FString::TryParseInt() const
{
	const FString Trimmed = TrimStartAndEnd();
	if ( Trimmed.IsEmpty() ) return {};

	char* End = nullptr;
	errno = 0;
	const long long Value = std::strtoll( Trimmed.GetData(), &End, 0 );
	if ( errno == ERANGE || End != Trimmed.GetData() + Trimmed.Len() ) return {};
	return static_cast<int64>( Value );
}

TOptional<float64> FString::TryParseFloat() const
{
	const FString Trimmed = TrimStartAndEnd();
	if ( Trimmed.IsEmpty() ) return {};

	char* End = nullptr;
	errno = 0;
	const double Value = std::strtod( Trimmed.GetData(), &End );
	if ( errno == ERANGE || End != Trimmed.GetData() + Trimmed.Len() ) return {};
	return Value;
}

bool FString::IsNumeric() const
{
	return TryParseFloat().IsSet();
}

bool FString::ToBool() const
{
	const FString Trimmed = TrimStartAndEnd();
	if ( Trimmed.Equals( "true", ESearchCase::IgnoreCase ) || Trimmed.Equals( "yes", ESearchCase::IgnoreCase ) || Trimmed.Equals( "on", ESearchCase::IgnoreCase ) )
	{
		return true;
	}
	const TOptional<float64> Number = Trimmed.TryParseFloat();
	return Number.IsSet() && Number.GetValue() != 0.0;
}

#pragma endregion

#pragma region Encoding

TArray<wchar_t> FString::ToWide() const
{
	TArray<wchar_t> Result;
	const int32 Length = IsEmpty() ? 0 : MultiByteToWideChar( CP_ACP, 0, GetData(), Len(), nullptr, 0 );
	Result.SetNum( Length + 1 );
	if ( Length > 0 ) MultiByteToWideChar( CP_ACP, 0, GetData(), Len(), Result.GetData(), Length );
	Result[Length] = L'\0';
	return Result;
}

FString FString::ToUtf8() const
{
	return Convert( GetData(), Len(), CP_ACP, CP_UTF8 );
}

#pragma endregion
