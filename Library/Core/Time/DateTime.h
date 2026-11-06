// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 日時（カレンダーの日付と時刻）
//   FDateTime は、世界協定時刻（UTC）の 1970-01-01 00:00:00 からの経過ミリ秒（Unix 時間）で日時を持つ。
//   タイムゾーンに依らない値なので、保存・通信・比較に向く。表示するときだけ、年月日時分秒（FDateParts）に分ける。
//
//   FDateTime Now = FDateTime::Now();
//   FDateParts Local = Now.ToLocalParts();                       // 今の壁の時計の時刻（タイムゾーン・夏時間を反映）
//   FString Text = Now.ToIso8601();                              // "2026-11-06T09:30:00.123Z"
//   FDateTime Later = Now.AddSeconds( 90.0 );
//   const float64 Seconds = Later.SecondsSince( Now );          // 90.0
//
//   - 日付の計算は、グレゴリオ暦（1970 年より前も、うるう年の規則どおり）。うるう秒は扱わない
//   - 年月日の範囲は 1 〜 9999 年
//=============================================================================

// 曜日（日曜が 0）
enum class EDayOfWeek : uint8
{
	Sunday,
	Monday,
	Tuesday,
	Wednesday,
	Thursday,
	Friday,
	Saturday,
};

// 年月日時分秒に分けた日時
struct FDateParts
{
	int32 Year = 1970;
	int32 Month = 1;        // 1 から 12
	int32 Day = 1;          // 1 から 31
	int32 Hour = 0;         // 0 から 23
	int32 Minute = 0;       // 0 から 59
	int32 Second = 0;       // 0 から 59
	int32 Millisecond = 0;  // 0 から 999
};

class FDateTime
{
public:
	// 1970-01-01 00:00:00 UTC
	FDateTime() = default;

	explicit FDateTime( int64 InUnixMilliseconds )
		: UnixMilliseconds( InUnixMilliseconds )
	{
	}

	// 今の日時
	static FDateTime Now();

	// UTC の年月日時分秒から作る。範囲の外（13 月、2 月 30 日、25 時など）なら false で、Out は変えない
	static bool FromParts( const FDateParts& Parts, FDateTime& Out );

	// 壁の時計（ローカル時刻。タイムゾーンと夏時間を反映）の年月日時分秒から作る。範囲の外・変換できなければ false
	static bool FromLocalParts( const FDateParts& Parts, FDateTime& Out );

	// ISO 8601 の文字列（"2024-02-29T12:34:56Z"、小数秒 ".789"、"+09:00" のようなずれも読める）から作る。読めなければ false
	static bool TryParseIso8601( const FString& Text, FDateTime& Out );

	static bool IsLeapYear( int32 Year );
	// Year 年 Month 月（1〜12）の日数。月が範囲の外なら 0
	static int32 GetDaysInMonth( int32 Year, int32 Month );

	int64 GetUnixMilliseconds() const
	{
		return UnixMilliseconds;
	}

	int64 GetUnixSeconds() const;

	// UTC の年月日時分秒
	FDateParts ToParts() const;
	// ローカル時刻の年月日時分秒
	FDateParts ToLocalParts() const;
	// UTC の曜日
	EDayOfWeek GetDayOfWeek() const;

	// "2026-11-06T09:30:00.123Z"（UTC。小数秒は常に 3 桁）
	FString ToIso8601() const;

	FDateTime AddMilliseconds( int64 Milliseconds ) const;
	FDateTime AddSeconds( float64 Seconds ) const;
	FDateTime AddDays( int32 Days ) const;

	// Earlier から、この日時までの秒数（Earlier のほうが後なら負）
	float64 SecondsSince( const FDateTime& Earlier ) const;

	bool operator==( const FDateTime& Other ) const
	{
		return UnixMilliseconds == Other.UnixMilliseconds;
	}

	bool operator!=( const FDateTime& Other ) const
	{
		return UnixMilliseconds != Other.UnixMilliseconds;
	}

	bool operator<( const FDateTime& Other ) const
	{
		return UnixMilliseconds < Other.UnixMilliseconds;
	}

	bool operator<=( const FDateTime& Other ) const
	{
		return UnixMilliseconds <= Other.UnixMilliseconds;
	}

	bool operator>( const FDateTime& Other ) const
	{
		return UnixMilliseconds > Other.UnixMilliseconds;
	}

	bool operator>=( const FDateTime& Other ) const
	{
		return UnixMilliseconds >= Other.UnixMilliseconds;
	}

private:
	int64 UnixMilliseconds = 0;
};
