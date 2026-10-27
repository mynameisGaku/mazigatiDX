#include "PlatformTime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace
{
	int64 QueryFrequency()
	{
		LARGE_INTEGER Frequency;
		QueryPerformanceFrequency( &Frequency );
		return Frequency.QuadPart;
	}

	// 周波数は起動中変わらないので 1 回だけ取得する
	const int64 GTicksPerSecond = QueryFrequency();
}

int64 NPlatformTime::GetTicks()
{
	LARGE_INTEGER Counter;
	QueryPerformanceCounter( &Counter );
	return Counter.QuadPart;
}

int64 NPlatformTime::GetTicksPerSecond()
{
	return GTicksPerSecond;
}

float64 NPlatformTime::TicksToMs( int64 Ticks )
{
	return static_cast<float64>( Ticks ) * 1000.0 / static_cast<float64>( GTicksPerSecond );
}

float64 NPlatformTime::TicksToUs( int64 Ticks )
{
	return static_cast<float64>( Ticks ) * 1000000.0 / static_cast<float64>( GTicksPerSecond );
}

NPlatformTime::FLocalClock NPlatformTime::GetLocalClock()
{
	SYSTEMTIME Now;
	::GetLocalTime( &Now );
	FLocalClock Clock;
	Clock.Year = Now.wYear;
	Clock.Month = Now.wMonth;
	Clock.Day = Now.wDay;
	Clock.Hour = Now.wHour;
	Clock.Minute = Now.wMinute;
	Clock.Second = Now.wSecond;
	Clock.Millisecond = Now.wMilliseconds;
	return Clock;
}

uint32 NPlatformTime::GetCurrentThreadId()
{
	return static_cast<uint32>( ::GetCurrentThreadId() );
}
