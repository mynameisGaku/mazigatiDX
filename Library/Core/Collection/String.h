#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <sal.h>
#include <cstdarg>
#include <utility>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Hash.h"
#include "Library/Core/Collection/Optional.h"

//=============================================================================
// FString
//   文字列。内部は終端 '\0' 付きの TArray<char>。
//
//   文字コードは DxLib / ソースの文字列リテラルと同じ「システムのマルチバイト」(日本語環境では Shift-JIS)。
//   DxLib の関数にはそのまま渡せる:   DrawString( 0, 0, *Name, Color );
//   ImGui (UTF-8) に渡すときは変換:     ImGui::TextUnformatted( *Name.ToUtf8() );
//
//   検索・置換・大文字小文字変換は全角文字の 2 バイト目を誤検出しないよう処理している。
//   Left / Right / Mid などの位置・長さはバイト単位。
//
//   FString Path = FString( "Data" ) / "Model" / "Player.mv1";   // "Data/Model/Player.mv1"
//   FString Msg  = FString::Printf( "HP: %d / %d", Hp, MaxHp );
//   TArray<FString> Parts = FString( "a,b,,c" ).ParseIntoArray( "," );   // { "a", "b", "c" }
//=============================================================================
class FString
{
public:
	using iterator       = char*;
	using const_iterator = const char*;

	//-------------------------------------------------------------------------
	// 生成
	//-------------------------------------------------------------------------
	FString() = default;
	FString( const char* Str );
	FString( const char* Str, int32 Length );
	// ワイド文字列から変換
	explicit FString( const wchar_t* Str );

	// Count 個の Ch
	static FString Repeat( char Ch, int32 Count );

	// printf 形式
	static FString Printf( _Printf_format_string_ const char* Format, ... );
	static FString PrintfV( const char* Format, va_list Args );

	static FString FromInt( int64 Value );
	// Decimals 桁で丸める。末尾の 0 は削らない
	static FString FromFloat( float64 Value, int32 Decimals = 3 );
	FORCEINLINE static FString FromBool( bool bValue ) { return bValue ? "true" : "false"; }

	// UTF-8 文字列から変換
	static FString FromUtf8( const char* Utf8 );

	static FString Join( const TArray<FString>& Items, const FString& Separator );

	//-------------------------------------------------------------------------
	// アクセス
	//-------------------------------------------------------------------------
	// C 文字列（DxLib の const TCHAR* 引数にそのまま渡せる）
	FORCEINLINE const char* operator*() const noexcept { return GetData(); }
	FORCEINLINE const char* c_str() const noexcept { return GetData(); }
	FORCEINLINE const char* GetData() const noexcept { return Data.Num() > 0 ? Data.GetData() : ""; }

	// 終端を含む内部配列
	FORCEINLINE const TArray<char>& GetCharArray() const noexcept { return Data; }

	FORCEINLINE char& operator[]( int32 Index )
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "FString index out of range" );
		return Data[Index];
	}
	FORCEINLINE const char& operator[]( int32 Index ) const
	{
		COLLECTION_CHECK( IsValidIndex( Index ), "FString index out of range" );
		return Data[Index];
	}

	// バイト数
	FORCEINLINE int32 Len() const noexcept { return Data.Num() > 0 ? Data.Num() - 1 : 0; }
	// 文字数（全角 1 文字 = 1）
	int32 CharCount() const;
	FORCEINLINE bool IsEmpty() const noexcept { return Len() == 0; }
	FORCEINLINE bool IsValidIndex( int32 Index ) const noexcept { return Index >= 0 && Index < Len(); }
	FORCEINLINE void Empty() { Data.Empty(); }
	FORCEINLINE void Reset() { Data.Reset(); }
	FORCEINLINE void Reserve( int32 Capacity ) { Data.Reserve( Capacity + 1 ); }

	//-------------------------------------------------------------------------
	// 連結
	//-------------------------------------------------------------------------
	FString& Append( const char* Str, int32 Length );
	FString& operator+=( const FString& Str ) { return Append( Str.GetData(), Str.Len() ); }
	FString& operator+=( const char* Str );
	FString& operator+=( char Ch ) { return Append( &Ch, 1 ); }

	friend FORCEINLINE FString operator+( const FString& A, const FString& B ) { FString R( A ); R += B; return R; }
	friend FORCEINLINE FString operator+( FString&& A, const FString& B ) { A += B; return std::move( A ); }
	friend FORCEINLINE FString operator+( const FString& A, const char* B ) { FString R( A ); R += B; return R; }
	friend FORCEINLINE FString operator+( FString&& A, const char* B ) { A += B; return std::move( A ); }
	friend FORCEINLINE FString operator+( const char* A, const FString& B ) { FString R( A ); R += B; return R; }
	friend FORCEINLINE FString operator+( const FString& A, char B ) { FString R( A ); R += B; return R; }
	friend FORCEINLINE FString operator+( FString&& A, char B ) { A += B; return std::move( A ); }
	friend FORCEINLINE FString operator+( char A, const FString& B ) { FString R; R += A; R += B; return R; }

	// パス連結（区切りは '/'。重複する区切りは 1 つにまとめる）
	FString& operator/=( const FString& Path );
	friend FORCEINLINE FString operator/( const FString& A, const FString& B ) { FString R( A ); R /= B; return R; }
	friend FORCEINLINE FString operator/( const FString& A, const char* B ) { FString R( A ); R /= FString( B ); return R; }

	//-------------------------------------------------------------------------
	// 比較（== / < は大文字小文字を区別する）
	//-------------------------------------------------------------------------
	friend bool operator==( const FString& A, const FString& B ) noexcept { return A.Compare( B ) == 0; }
	friend bool operator==( const FString& A, const char* B ) noexcept { return A.CompareRaw( B ) == 0; }
	friend bool operator==( const char* A, const FString& B ) noexcept { return B.CompareRaw( A ) == 0; }
	friend bool operator!=( const FString& A, const FString& B ) noexcept { return !( A == B ); }
	friend bool operator!=( const FString& A, const char* B ) noexcept { return !( A == B ); }
	friend bool operator!=( const char* A, const FString& B ) noexcept { return !( B == A ); }
	friend FORCEINLINE bool operator<( const FString& A, const FString& B ) noexcept { return A.Compare( B ) < 0; }
	friend FORCEINLINE bool operator>( const FString& A, const FString& B ) noexcept { return A.Compare( B ) > 0; }
	friend bool operator<=( const FString& A, const FString& B ) noexcept { return A.Compare( B ) <= 0; }
	friend bool operator>=( const FString& A, const FString& B ) noexcept { return A.Compare( B ) >= 0; }

	bool Equals( const FString& Other, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const;
	// 負: this < Other, 0: 等しい, 正: this > Other
	int32 Compare( const FString& Other, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const noexcept;

	friend FORCEINLINE uint32 GetTypeHash( const FString& Str ) { return HashBytes( Str.GetData(), static_cast<usize>( Str.Len() ) ); }

	//-------------------------------------------------------------------------
	// 検索（見つからなければ INDEX_NONE）
	//-------------------------------------------------------------------------
	int32 Find( const FString& Sub, ESearchCase SearchCase = ESearchCase::CaseSensitive, int32 StartIndex = 0 ) const;
	int32 FindLast( const FString& Sub, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const;
	FORCEINLINE bool Contains( const FString& Sub, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const { return Find( Sub, SearchCase ) != INDEX_NONE; }
	bool StartsWith( const FString& Prefix, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const;
	bool EndsWith( const FString& Suffix, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const;

	//-------------------------------------------------------------------------
	// 部分文字列（バイト単位）
	//-------------------------------------------------------------------------
	FString Left( int32 Count ) const;        // 先頭 Count バイト
	FString Right( int32 Count ) const;       // 末尾 Count バイト
	FString LeftChop( int32 Count ) const;    // 末尾 Count バイトを除いたもの
	FString RightChop( int32 Count ) const;   // 先頭 Count バイトを除いたもの
	FString Mid( int32 Start, int32 Count = 0x7FFFFFFF ) const;

	//-------------------------------------------------------------------------
	// 変換（新しい文字列を返す）
	//-------------------------------------------------------------------------
	FString ToUpper() const;   // ASCII のみ
	FString ToLower() const;   // ASCII のみ
	FString TrimStart() const;
	FString TrimEnd() const;
	FString TrimStartAndEnd() const;
	FString Replace( const FString& From, const FString& To, ESearchCase SearchCase = ESearchCase::CaseSensitive ) const;

	// その場で置換し、置換数を返す
	int32 ReplaceInline( const FString& From, const FString& To, ESearchCase SearchCase = ESearchCase::CaseSensitive );

	// 最初（bFromEnd なら最後）の Delimiter で 2 分割。見つからなければ false
	bool Split( const FString& Delimiter, FString* OutLeft, FString* OutRight,
				ESearchCase SearchCase = ESearchCase::CaseSensitive, bool bFromEnd = false ) const;

	// Delimiter で分割
	TArray<FString> ParseIntoArray( const FString& Delimiter, bool bCullEmpty = true ) const;
	// 改行 (\n, \r\n) で分割
	TArray<FString> ParseIntoLines( bool bCullEmpty = true ) const;

	//-------------------------------------------------------------------------
	// 数値変換
	//-------------------------------------------------------------------------
	bool IsNumeric() const;                       // 前後の空白を除いて数値として解釈できるか
	TOptional<int64>   TryParseInt() const;
	TOptional<float64> TryParseFloat() const;
	FORCEINLINE int32   ToInt( int32 Default = 0 ) const { return static_cast<int32>( TryParseInt().Get( Default ) ); }
	FORCEINLINE float32 ToFloat( float32 Default = 0.0f ) const { return static_cast<float32>( TryParseFloat().Get( Default ) ); }
	bool    ToBool() const;                       // "true" / "yes" / "on" / 0 以外の数値 で true

	//-------------------------------------------------------------------------
	// 文字コード変換
	//-------------------------------------------------------------------------
	// ワイド文字列（終端 L'\0' 付き。GetData() を Win32 の W 系 API に渡せる）
	TArray<wchar_t> ToWide() const;
	// UTF-8 のバイト列を持つ FString（ImGui などへ渡す用）
	FString ToUtf8() const;

	//-------------------------------------------------------------------------
	// イテレータ（終端 '\0' は含まない）
	//-------------------------------------------------------------------------
	FORCEINLINE char* begin() noexcept { return Data.GetData(); }
	FORCEINLINE char* end() noexcept { return Data.GetData() + Len(); }
	FORCEINLINE const char* begin() const noexcept { return Data.GetData(); }
	FORCEINLINE const char* end() const noexcept { return Data.GetData() + Len(); }

private:
	int32 CompareRaw( const char* Other ) const noexcept;

	TArray<char> Data;  // 空文字列なら要素 0 個、それ以外は末尾に '\0'
};
