// SPDX-License-Identifier: MIT
#include "DateTime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace
{
	constexpr int64 MillisecondsPerSecond = 1000;
	constexpr int64 MillisecondsPerDay = 86400000;

	// 1970-01-01 からの日数 -> 年月日（グレゴリオ暦。H. Hinnant 氏の civil_from_days）
	void CivilFromDays( int64 Days, int32& OutYear, int32& OutMonth, int32& OutDay )
	{
		Days += 719468;
		const int64 Era = ( Days >= 0 ? Days : Days - 146096 ) / 146097;
		const int64 DayOfEra = Days - Era * 146097;
		const int64 YearOfEra = ( DayOfEra - DayOfEra / 1460 + DayOfEra / 36524 - DayOfEra / 146096 ) / 365;
		const int64 DayOfYear = DayOfEra - ( 365 * YearOfEra + YearOfEra / 4 - YearOfEra / 100 );
		const int64 MonthPrime = ( 5 * DayOfYear + 2 ) / 153;
		OutDay = static_cast<int32>( DayOfYear - ( 153 * MonthPrime + 2 ) / 5 + 1 );
		OutMonth = static_cast<int32>( MonthPrime < 10 ? MonthPrime + 3 : MonthPrime - 9 );
		OutYear = static_cast<int32>( YearOfEra + Era * 400 + ( OutMonth <= 2 ? 1 : 0 ) );
	}

	// 年月日 -> 1970-01-01 からの日数（days_from_civil）
	int64 DaysFromCivil( int32 Year, int32 Month, int32 Day )
	{
		const int64 AdjustedYear = static_cast<int64>( Year ) - ( Month <= 2 ? 1 : 0 );
		const int64 Era = ( AdjustedYear >= 0 ? AdjustedYear : AdjustedYear - 399 ) / 400;
		const int64 YearOfEra = AdjustedYear - Era * 400;
		const int64 MonthPrime = Month > 2 ? Month - 3 : Month + 9;
		const int64 DayOfYear = ( 153 * MonthPrime + 2 ) / 5 + Day - 1;
		const int64 DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
		return Era * 146097 + DayOfEra - 719468;
	}

	// 割り算の切り捨てが負の側へ向かう版（日付の計算用）
	int64 FloorDivide( int64 Value, int64 Divisor )
	{
		int64 Quotient = Value / Divisor;
		if ( ( Value % Divisor != 0 ) && ( ( Value < 0 ) != ( Divisor < 0 ) ) )
		{
			--Quotient;
		}
		return Quotient;
	}

	bool IsValidParts( const FDateParts& Parts )
	{
		return Parts.Year >= 1 && Parts.Year <= 9999 && Parts.Month >= 1 && Parts.Month <= 12 && Parts.Day >= 1 && Parts.Day <= FDateTime::GetDaysInMonth( Parts.Year, Parts.Month )
			&& Parts.Hour >= 0 && Parts.Hour <= 23 && Parts.Minute >= 0 && Parts.Minute <= 59 && Parts.Second >= 0 && Parts.Second <= 59 && Parts.Millisecond >= 0 && Parts.Millisecond <= 999;
	}

	int64 PartsToMilliseconds( const FDateParts& Parts )
	{
		return DaysFromCivil( Parts.Year, Parts.Month, Parts.Day ) * MillisecondsPerDay + static_cast<int64>( Parts.Hour ) * 3600000 + static_cast<int64>( Parts.Minute ) * 60000
			+ static_cast<int64>( Parts.Second ) * 1000 + Parts.Millisecond;
	}

	FDateParts MillisecondsToParts( int64 UnixMilliseconds )
	{
		const int64 Days = FloorDivide( UnixMilliseconds, MillisecondsPerDay );
		const int64 InDay = UnixMilliseconds - Days * MillisecondsPerDay;
		FDateParts Parts;
		CivilFromDays( Days, Parts.Year, Parts.Month, Parts.Day );
		Parts.Hour = static_cast<int32>( InDay / 3600000 );
		Parts.Minute = static_cast<int32>( ( InDay / 60000 ) % 60 );
		Parts.Second = static_cast<int32>( ( InDay / 1000 ) % 60 );
		Parts.Millisecond = static_cast<int32>( InDay % 1000 );
		return Parts;
	}

	SYSTEMTIME PartsToSystemTime( const FDateParts& Parts )
	{
		SYSTEMTIME Time = {};
		Time.wYear = static_cast<WORD>( Parts.Year );
		Time.wMonth = static_cast<WORD>( Parts.Month );
		Time.wDay = static_cast<WORD>( Parts.Day );
		Time.wHour = static_cast<WORD>( Parts.Hour );
		Time.wMinute = static_cast<WORD>( Parts.Minute );
		Time.wSecond = static_cast<WORD>( Parts.Second );
		Time.wMilliseconds = static_cast<WORD>( Parts.Millisecond );
		return Time;
	}

	FDateParts SystemTimeToParts( const SYSTEMTIME& Time )
	{
		FDateParts Parts;
		Parts.Year = Time.wYear;
		Parts.Month = Time.wMonth;
		Parts.Day = Time.wDay;
		Parts.Hour = Time.wHour;
		Parts.Minute = Time.wMinute;
		Parts.Second = Time.wSecond;
		Parts.Millisecond = Time.wMilliseconds;
		return Parts;
	}

	// 先頭から Count 桁の数字を読む。読めれば Position を進めて true
	bool ReadDigits( const FString& Text, int32& Position, int32 Count, int32& OutValue )
	{
		if ( Position + Count > Text.Len() )
		{
			return false;
		}
		int32 Value = 0;
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			const char Character = Text[Position + Index];
			if ( Character < '0' || Character > '9' )
			{
				return false;
			}
			Value = Value * 10 + ( Character - '0' );
		}
		Position += Count;
		OutValue = Value;
		return true;
	}

	bool ReadCharacter( const FString& Text, int32& Position, char Expected )
	{
		if ( Position >= Text.Len() || Text[Position] != Expected )
		{
			return false;
		}
		++Position;
		return true;
	}
}

FDateTime FDateTime::Now()
{
	FILETIME FileTime;
	::GetSystemTimeAsFileTime( &FileTime );
	const uint64 Ticks = ( static_cast<uint64>( FileTime.dwHighDateTime ) << 32 ) | FileTime.dwLowDateTime;
	// 1601-01-01 からの 100 ナノ秒 -> 1970-01-01 からのミリ秒
	constexpr uint64 UnixEpochTicks = 116444736000000000ULL;
	return FDateTime( static_cast<int64>( ( Ticks - UnixEpochTicks ) / 10000ULL ) );
}

bool FDateTime::FromParts( const FDateParts& Parts, FDateTime& Out )
{
	if ( !IsValidParts( Parts ) )
	{
		return false;
	}
	Out = FDateTime( PartsToMilliseconds( Parts ) );
	return true;
}

bool FDateTime::FromLocalParts( const FDateParts& Parts, FDateTime& Out )
{
	if ( !IsValidParts( Parts ) )
	{
		return false;
	}
	const SYSTEMTIME Local = PartsToSystemTime( Parts );
	SYSTEMTIME Utc = {};
	if ( !::TzSpecificLocalTimeToSystemTime( nullptr, &Local, &Utc ) )
	{
		return false;
	}
	Out = FDateTime( PartsToMilliseconds( SystemTimeToParts( Utc ) ) );
	return true;
}

bool FDateTime::TryParseIso8601( const FString& Text, FDateTime& Out )
{
	int32 Position = 0;
	FDateParts Parts;
	if ( !ReadDigits( Text, Position, 4, Parts.Year ) || !ReadCharacter( Text, Position, '-' ) || !ReadDigits( Text, Position, 2, Parts.Month ) || !ReadCharacter( Text, Position, '-' )
		|| !ReadDigits( Text, Position, 2, Parts.Day ) )
	{
		return false;
	}
	// 時刻が無い日付だけの形は、0 時として読む
	if ( Position < Text.Len() )
	{
		if ( !ReadCharacter( Text, Position, 'T' ) || !ReadDigits( Text, Position, 2, Parts.Hour ) || !ReadCharacter( Text, Position, ':' ) || !ReadDigits( Text, Position, 2, Parts.Minute ) )
		{
			return false;
		}
		if ( Position < Text.Len() && Text[Position] == ':' )
		{
			++Position;
			if ( !ReadDigits( Text, Position, 2, Parts.Second ) )
			{
				return false;
			}
			if ( Position < Text.Len() && ( Text[Position] == '.' || Text[Position] == ',' ) )
			{
				++Position;
				// 小数秒は、ミリ秒より細かい桁を切り捨てる
				int32 Digits = 0;
				int32 Fraction = 0;
				while ( Position < Text.Len() && Text[Position] >= '0' && Text[Position] <= '9' )
				{
					if ( Digits < 3 )
					{
						Fraction = Fraction * 10 + ( Text[Position] - '0' );
						++Digits;
					}
					++Position;
				}
				if ( Digits == 0 )
				{
					return false;
				}
				while ( Digits < 3 )
				{
					Fraction *= 10;
					++Digits;
				}
				Parts.Millisecond = Fraction;
			}
		}
	}

	// タイムゾーン: Z、または +hh:mm / -hh:mm（無ければ UTC として読む）
	int64 OffsetMinutes = 0;
	if ( Position < Text.Len() )
	{
		if ( Text[Position] == 'Z' )
		{
			++Position;
		}
		else if ( Text[Position] == '+' || Text[Position] == '-' )
		{
			const bool bNegative = Text[Position] == '-';
			++Position;
			int32 OffsetHours = 0;
			int32 OffsetMinutePart = 0;
			if ( !ReadDigits( Text, Position, 2, OffsetHours ) )
			{
				return false;
			}
			if ( Position < Text.Len() && Text[Position] == ':' )
			{
				++Position;
			}
			if ( Position < Text.Len() && !ReadDigits( Text, Position, 2, OffsetMinutePart ) )
			{
				return false;
			}
			if ( OffsetHours > 23 || OffsetMinutePart > 59 )
			{
				return false;
			}
			OffsetMinutes = ( static_cast<int64>( OffsetHours ) * 60 + OffsetMinutePart ) * ( bNegative ? -1 : 1 );
		}
	}
	if ( Position != Text.Len() )
	{
		return false;
	}

	FDateTime Parsed;
	if ( !FromParts( Parts, Parsed ) )
	{
		return false;
	}
	// 「+09:00」は UTC より 9 時間進んでいるので、UTC にするには引く
	Out = FDateTime( Parsed.UnixMilliseconds - OffsetMinutes * 60000 );
	return true;
}

bool FDateTime::IsLeapYear( int32 Year )
{
	return ( Year % 4 == 0 && Year % 100 != 0 ) || Year % 400 == 0;
}

int32 FDateTime::GetDaysInMonth( int32 Year, int32 Month )
{
	static const int32 Days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if ( Month < 1 || Month > 12 )
	{
		return 0;
	}
	return Month == 2 && IsLeapYear( Year ) ? 29 : Days[Month - 1];
}

int64 FDateTime::GetUnixSeconds() const
{
	return FloorDivide( UnixMilliseconds, MillisecondsPerSecond );
}

FDateParts FDateTime::ToParts() const
{
	return MillisecondsToParts( UnixMilliseconds );
}

FDateParts FDateTime::ToLocalParts() const
{
	const FDateParts Utc = ToParts();
	// Windows の変換は 1601 年〜30827 年の範囲だけ。範囲の外（1 年〜1600 年）は UTC のまま返す
	if ( Utc.Year < 1601 )
	{
		return Utc;
	}
	const SYSTEMTIME UtcTime = PartsToSystemTime( Utc );
	SYSTEMTIME LocalTime = {};
	if ( !::SystemTimeToTzSpecificLocalTime( nullptr, &UtcTime, &LocalTime ) )
	{
		return Utc;
	}
	return SystemTimeToParts( LocalTime );
}

EDayOfWeek FDateTime::GetDayOfWeek() const
{
	// 1970-01-01 は木曜
	const int64 Days = FloorDivide( UnixMilliseconds, MillisecondsPerDay );
	int64 Weekday = ( Days + 4 ) % 7;
	Weekday = Weekday < 0 ? Weekday + 7 : Weekday;
	return static_cast<EDayOfWeek>( Weekday );
}

FString FDateTime::ToIso8601() const
{
	const FDateParts Parts = ToParts();
	return FString::Printf( "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", Parts.Year, Parts.Month, Parts.Day, Parts.Hour, Parts.Minute, Parts.Second, Parts.Millisecond );
}

FDateTime FDateTime::AddMilliseconds( int64 Milliseconds ) const
{
	return FDateTime( UnixMilliseconds + Milliseconds );
}

FDateTime FDateTime::AddSeconds( float64 Seconds ) const
{
	// 数でない値は足さない。ミリ秒に丸める
	if ( Seconds != Seconds )
	{
		return *this;
	}
	const float64 Milliseconds = Seconds * 1000.0;
	return FDateTime( UnixMilliseconds + static_cast<int64>( Milliseconds < 0.0 ? Milliseconds - 0.5 : Milliseconds + 0.5 ) );
}

FDateTime FDateTime::AddDays( int32 Days ) const
{
	return FDateTime( UnixMilliseconds + static_cast<int64>( Days ) * MillisecondsPerDay );
}

float64 FDateTime::SecondsSince( const FDateTime& Earlier ) const
{
	return static_cast<float64>( UnixMilliseconds - Earlier.UnixMilliseconds ) / 1000.0;
}
