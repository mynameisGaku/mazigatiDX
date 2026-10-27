// SPDX-License-Identifier: NOASSERTION
#pragma once

#include <sal.h>
#include <cerrno>    // errno / ERANGE（範囲を超えたかの確認）
#include <cstdarg>
#include <cstdio>    // 中身は std::snprintf / std::vsnprintf。標準ライブラリに触れるのは、このヘッダーだけに閉じ込める
#include <cstdlib>   // std::atoi / std::strtoll / std::strtod
#include <cstring>   // std::strlen / std::strcmp など
#include <cwchar>    // std::wcslen

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NCString
//   終端が '\0' の文字列（const char*）を調べる・比べる・数にする・書式で書く関数。
//   <cstring> / <cstdio> / <cstdlib> の代わり。結果は std と同じで、名前だけ違う。
//     std::strlen( T )                  -> NCString::Length( T )
//     std::strcmp( A, B )               -> NCString::Compare( A, B )
//     std::strncmp( A, B, N )           -> NCString::CompareN( A, B, N )
//     _stricmp / _strnicmp              -> NCString::CompareIgnoreCase / CompareNIgnoreCase（大文字小文字を区別しない）
//     strncpy_s( D, N, S, _TRUNCATE )   -> NCString::CopyTruncated( D, N, S )
//     std::strstr( T, P ) / strchr      -> NCString::Find( T, P ) / FindChar( T, C )
//     std::snprintf / std::vsnprintf    -> NCString::Format / FormatV
//     std::atoi / strtoll / strtod      -> NCString::ToInt32 / ToInt64 / ToFloat64
//     std::wcslen( W )                  -> NCString::WideLength( W )
//     errno = 0; ...; errno == ERANGE   -> NCString::ClearRangeError(); ...; NCString::HasRangeError()
//
//   どの関数も、空ポインター（nullptr）は渡せない（Format の Buffer が nullptr で Size が 0 のときだけ例外）。
//   文字コードは見ない（バイト単位）。FString を使えるところでは FString を使うこと。
//
//   char Label[64];
//   NCString::Format( Label, sizeof( Label ), "%d / %d", Hp, MaxHp );   // 入りきらないときは切り詰めて、終端を付ける
//=============================================================================
namespace NCString
{
	// 終端までの文字数（終端は含まない）。
	FORCEINLINE usize Length( const char* Text ) noexcept { return std::strlen( Text ); }

	// 終端までの文字数（ワイド文字）。
	FORCEINLINE usize WideLength( const wchar_t* Text ) noexcept { return std::wcslen( Text ); }

	// 辞書順に比べる。A が小さければ負、等しければ 0、大きければ正（大きさには意味がない）。
	FORCEINLINE int32 Compare( const char* A, const char* B ) noexcept { return std::strcmp( A, B ); }

	// 先頭の Count 文字だけを辞書順に比べる。
	FORCEINLINE int32 CompareN( const char* A, const char* B, usize Count ) noexcept { return std::strncmp( A, B, Count ); }

	// 大文字小文字を区別せずに（ASCII の A-Z と a-z を同じとして）辞書順に比べる。_stricmp。
	FORCEINLINE int32 CompareIgnoreCase( const char* A, const char* B ) noexcept { return ::_stricmp( A, B ); }

	// 先頭の Count 文字だけを、大文字小文字を区別せずに比べる。_strnicmp。
	FORCEINLINE int32 CompareNIgnoreCase( const char* A, const char* B, usize Count ) noexcept { return ::_strnicmp( A, B, Count ); }

	// Source を Dest（Size バイト）へコピーする。入りきらなければ切り詰めて、終端を付ける（Size が 0 のときは何も書かない）。
	inline void CopyTruncated( char* Dest, usize Size, const char* Source ) noexcept
	{
		if ( Size > 0 ) ::strncpy_s( Dest, Size, Source, _TRUNCATE );
	}

	// Text の中で最初に Pattern が現れる位置。無ければ nullptr。
	FORCEINLINE const char* Find( const char* Text, const char* Pattern ) noexcept { return std::strstr( Text, Pattern ); }

	// Text の中で最初に Character が現れる位置。無ければ nullptr。
	FORCEINLINE const char* FindChar( const char* Text, char Character ) noexcept { return std::strchr( Text, Character ); }

	// Format で書いた文字列を Buffer（Size バイト）へ書く。入りきらなければ切り詰めて終端を付ける（Size が 0 のときは何も書かない）。
	// 戻り値は、切り詰めなかったときの文字数（終端を除く）。書式が不正なら負。
	inline int32 Format( char* Buffer, usize Size, _Printf_format_string_ const char* InFormat, ... )
	{
		va_list Args;
		va_start( Args, InFormat );
		const int32 Result = std::vsnprintf( Buffer, Size, InFormat, Args );
		va_end( Args );
		return Result;
	}

	// Format の va_list 版。
	inline int32 FormatV( char* Buffer, usize Size, const char* InFormat, va_list Args )
	{
		return std::vsnprintf( Buffer, Size, InFormat, Args );
	}

	// 10 進数の文字列を数にする。先頭の空白は読み飛ばす。数でなければ 0。範囲を超えたときの結果は決まっていない。
	FORCEINLINE int32 ToInt32( const char* Text ) noexcept { return std::atoi( Text ); }

	// 文字列を整数にする。Base が 0 なら "0x" で 16 進、"0" で 8 進、それ以外は 10 進。読み終えた位置を *End に書く（End は nullptr でもよい）。
	// 範囲を超えたら、上限（下限）の値を返す。数でなければ 0 を返し、*End は Text のまま。
	FORCEINLINE int64 ToInt64( const char* Text, char** End, int32 Base ) noexcept { return std::strtoll( Text, End, Base ); }

	// 文字列を実数にする（"1e3"、"inf"、"nan" も読める）。読み終えた位置を *End に書く（End は nullptr でもよい）。数でなければ 0。
	FORCEINLINE float64 ToFloat64( const char* Text, char** End ) noexcept { return std::strtod( Text, End ); }

	// ToInt64 / ToFloat64 が、読んだ数が範囲を超えた（整数は上限・下限に丸め、実数は無限大か 0 になった）かを調べる。
	// 読む直前に ClearRangeError() を呼び、読んだ直後に HasRangeError() で調べる（スレッドごとの記録なので、別のスレッドの読みとは混ざらない）。
	FORCEINLINE void ClearRangeError() noexcept { errno = 0; }
	FORCEINLINE bool HasRangeError() noexcept { return errno == ERANGE; }
}
