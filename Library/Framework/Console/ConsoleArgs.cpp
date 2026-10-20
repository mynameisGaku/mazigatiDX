#include "ConsoleArgs.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

bool NConsole::ParseInt64( const FString& Text, int64& Out )
{
	if ( Text.IsEmpty() ) return false;
	// 前後の空白・先頭の + は受け付けるが、途中の空白は受け付けない
	const char* Begin = *Text;
	char* End = nullptr;
	errno = 0;

	int Base = 10;
	const char* Digits = Begin;
	if ( *Digits == '+' || *Digits == '-' ) ++Digits;
	if ( Digits[0] == '0' && ( Digits[1] == 'x' || Digits[1] == 'X' ) ) Base = 16;

	const long long Value = std::strtoll( Begin, &End, Base );
	if ( End == Begin || *End != '\0' || errno == ERANGE ) return false;
	// strtoll は先頭の空白を読み飛ばすが、引数に空白は入らないので、ここでは拒否しておく
	if ( Begin[0] == ' ' || Begin[0] == '\t' ) return false;
	Out = Value;
	return true;
}

bool NConsole::ParseFloat64( const FString& Text, float64& Out )
{
	if ( Text.IsEmpty() ) return false;
	const char* Begin = *Text;
	if ( Begin[0] == ' ' || Begin[0] == '\t' ) return false;
	char* End = nullptr;
	errno = 0;
	const double Value = std::strtod( Begin, &End );
	if ( End == Begin || *End != '\0' ) return false;
	if ( !std::isfinite( Value ) ) return false;   // nan / inf / 溢れ
	Out = Value;
	return true;
}

bool NConsole::ParseBool( const FString& Text, bool& Out )
{
	const FString Lower = Text.ToLower();
	if ( Lower == "true" || Lower == "on" || Lower == "yes" || Lower == "1" )
	{
		Out = true;
		return true;
	}
	if ( Lower == "false" || Lower == "off" || Lower == "no" || Lower == "0" )
	{
		Out = false;
		return true;
	}
	return false;
}

FString NConsole::FormatFloat( float64 Value )
{
	char Buffer[48];
	std::snprintf( Buffer, sizeof( Buffer ), "%.6g", Value );
	return FString( Buffer );
}
