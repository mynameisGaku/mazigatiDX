#include "String.h"


#include "Library/Core/Types/CString.h"
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
	char ToLowerAscii( char Ch ) { return ( Ch >= 'A' && Ch <= 'Z' ) ? static_cast<char>( Ch - 'A' + 'a' ) : Ch; }
	char ToUpperAscii( char Ch ) { return ( Ch >= 'a' && Ch <= 'z' ) ? static_cast<char>( Ch - 'a' + 'A' ) : Ch; }
	bool IsSpace( char Ch ) { return Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n' || Ch == '\v' || Ch == '\f'; }

	// UTF-8 の文字の 2 バイト目以降（10xxxxxx）か
	bool IsContinuationByte( char Ch ) { return ( static_cast<unsigned char>( Ch ) & 0xC0 ) == 0x80; }

	// Str[Index..] が Sub と一致するか。
	// UTF-8 の 2 バイト目以降は ASCII（'\\' や '/' など）と重ならないので、バイト単位で比べても文字の途中には一致しない
	bool MatchAt( const char* Str, int32 Length, int32 Index, const char* Sub, int32 SubLength, ESearchCase SearchCase )
	{
		if ( Index + SubLength > Length ) return false;

		for ( int32 i = 0; i < SubLength; ++i )
		{
			const char A = Str[Index + i];
			const char B = Sub[i];
			if ( SearchCase == ESearchCase::IgnoreCase ? ToLowerAscii( A ) != ToLowerAscii( B ) : A != B ) return false;
		}
		return true;
	}

	// 一致位置（bLast なら最後の一致）
	int32 FindImpl( const char* Str, int32 Length, const char* Sub, int32 SubLength, ESearchCase SearchCase, int32 StartIndex, bool bLast )
	{
		if ( SubLength == 0 ) return bLast ? Length : ( StartIndex < Length ? StartIndex : Length );
		if ( SubLength > Length ) return INDEX_NONE;

		int32 Found = INDEX_NONE;
		for ( int32 i = StartIndex; i + SubLength <= Length; ++i )
		{
			if ( MatchAt( Str, Length, i, Sub, SubLength, SearchCase ) )
			{
				Found = i;
				if ( !bLast ) break;
			}
		}
		return Found;
	}

	// UTF-16 -> コードページ（UTF-8 など）
	FString FromWideChars( const wchar_t* Wide, int32 WideLength, UINT CodePage )
	{
		if ( WideLength <= 0 ) return FString();

		const int32 Length = WideCharToMultiByte( CodePage, 0, Wide, WideLength, nullptr, 0, nullptr, nullptr );
		TArray<char> Bytes;
		Bytes.SetNum( Length );
		WideCharToMultiByte( CodePage, 0, Wide, WideLength, Bytes.GetData(), Length, nullptr, nullptr );
		return FString( Bytes.GetData(), Length );
	}

	// コードページ -> UTF-16（終端なし）。不正なバイトは U+FFFD
	TArray<wchar_t> ToWideChars( const char* Bytes, int32 Length, UINT CodePage )
	{
		TArray<wchar_t> Wide;
		if ( Length <= 0 ) return Wide;

		const int32 WideLength = MultiByteToWideChar( CodePage, 0, Bytes, Length, nullptr, 0 );
		Wide.SetNum( WideLength );
		MultiByteToWideChar( CodePage, 0, Bytes, Length, Wide.GetData(), WideLength );
		return Wide;
	}
}

#pragma region Construction

FString::FString( const char* Str )
{
	if ( Str ) Append( Str, static_cast<int32>( NCString::Length( Str ) ) );
}

FString::FString( const char* Str, int32 Length )
{
	if ( Str ) Append( Str, Length );
}

FString::FString( const wchar_t* Str )
{
	if ( !Str || !*Str ) return;

	*this = FromWideChars( Str, static_cast<int32>( NCString::WideLength( Str ) ), CP_UTF8 );
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
	const int Length = NCString::FormatV( nullptr, 0, Format, ArgsCopy );
	va_end( ArgsCopy );
	if ( Length <= 0 ) return Result;

	Result.Data.SetNum( Length + 1 );
	NCString::FormatV( Result.Data.GetData(), static_cast<usize>( Length ) + 1, Format, Args );
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
	if ( Str ) Append( Str, static_cast<int32>( NCString::Length( Str ) ) );
	return *this;
}

FString& FString::operator/=( const FString& Path )
{
	if ( Path.IsEmpty() ) return *this;
	if ( IsEmpty() ) return *this = Path;

	// UTF-8 の文字の途中のバイトは ASCII と重ならないので、末尾の 1 バイトを見れば区切りかどうか分かる
	const char Last = GetData()[Len() - 1];
	const bool bEndsWithSep   = Last == '/' || Last == '\\';
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
	// 2 バイト目以降を数えずに、文字の先頭だけを数える（不正な UTF-8 でも、バイト数を超えない）
	const char* Str = GetData();
	const int32 Length = Len();
	int32 Count = 0;
	for ( int32 i = 0; i < Length; ++i )
	{
		if ( !IsContinuationByte( Str[i] ) ) ++Count;
	}
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
	return NCString::Compare( GetData(), Other ? Other : "" );
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

	return MatchAt( GetData(), Len(), Len() - Suffix.Len(), Suffix.GetData(), Suffix.Len(), SearchCase );
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
	// 1 バイトずつ ASCII だけ変える（UTF-8 の 2 バイト目以降は 0x80 以上なので、そのまま残る）
	FString Result( *this );
	char* Str = Result.Data.GetData();
	const int32 Length = Result.Len();
	for ( int32 i = 0; i < Length; ++i ) Str[i] = ToUpperAscii( Str[i] );
	return Result;
}

FString FString::ToLower() const
{
	FString Result( *this );
	char* Str = Result.Data.GetData();
	const int32 Length = Result.Len();
	for ( int32 i = 0; i < Length; ++i ) Str[i] = ToLowerAscii( Str[i] );
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
	// UTF-8 の文字の途中のバイトは 0x80 以上なので、空白と誤判定しない
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
	int32 Copied = 0;   // Str のここまでは Result に入れた
	for ( int32 i = 0; i < Length; )
	{
		if ( MatchAt( Str, Length, i, From.GetData(), From.Len(), SearchCase ) )
		{
			Result.Append( Str + Copied, i - Copied );
			Result += To;
			i += From.Len();
			Copied = i;
			++Count;
		}
		else
		{
			++i;
		}
	}
	if ( Count > 0 )
	{
		Result.Append( Str + Copied, Length - Copied );
		*this = MoveTemp( Result );
	}
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
	NCString::ClearRangeError();
	const long long Value = NCString::ToInt64( Trimmed.GetData(), &End, 0 );
	if ( NCString::HasRangeError() || End != Trimmed.GetData() + Trimmed.Len() ) return {};
	return static_cast<int64>( Value );
}

TOptional<float64> FString::TryParseFloat() const
{
	const FString Trimmed = TrimStartAndEnd();
	if ( Trimmed.IsEmpty() ) return {};

	char* End = nullptr;
	NCString::ClearRangeError();
	const double Value = NCString::ToFloat64( Trimmed.GetData(), &End );
	if ( NCString::HasRangeError() || End != Trimmed.GetData() + Trimmed.Len() ) return {};
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
	TArray<wchar_t> Result = ToWideChars( GetData(), Len(), CP_UTF8 );
	Result.Add( L'\0' );
	return Result;
}

bool FString::IsValidUtf8() const
{
	if ( IsEmpty() ) return true;
	// MB_ERR_INVALID_CHARS: 不正なバイトがあると 0 を返す（過長な表現・サロゲート・途中で切れた文字も不正）
	return MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, GetData(), Len(), nullptr, 0 ) > 0;
}

FString FString::ToValidUtf8() const
{
	if ( IsValidUtf8() ) return *this;

	// 不正なバイトは変換で U+FFFD になるので、UTF-16 を経由して UTF-8 に戻す
	const TArray<wchar_t> Wide = ToWideChars( GetData(), Len(), CP_UTF8 );
	return FromWideChars( Wide.GetData(), Wide.Num(), CP_UTF8 );
}

FString FString::FromCodePage( const char* Bytes, int32 Length, uint32 CodePage )
{
	if ( !Bytes || Length <= 0 ) return FString();

	const TArray<wchar_t> Wide = ToWideChars( Bytes, Length, CodePage );
	return FromWideChars( Wide.GetData(), Wide.Num(), CP_UTF8 );
}

FString FString::ToCodePage( uint32 CodePage ) const
{
	const TArray<wchar_t> Wide = ToWideChars( GetData(), Len(), CP_UTF8 );
	return FromWideChars( Wide.GetData(), Wide.Num(), CodePage );
}

#pragma endregion
