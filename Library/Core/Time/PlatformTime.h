#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// PlatformTime
//   高精度タイマーとスレッド ID（Windows API）。windows.h をヘッダーに持ち込まないよう .cpp に実装する。
//
//   const int64 Start = NPlatformTime::GetTicks();
//   ...
//   const float64 Ms = NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - Start );
//=============================================================================
namespace NPlatformTime
{
	// 今のローカル時刻（タイムゾーンと夏時間を反映した、壁の時計の時刻）。ログの行頭の時刻などに使う
	struct FLocalClock
	{
		int32 Year = 0;
		int32 Month = 0;         // 1 から 12
		int32 Day = 0;           // 1 から 31
		int32 Hour = 0;          // 0 から 23
		int32 Minute = 0;        // 0 から 59
		int32 Second = 0;        // 0 から 59（うるう秒は 59）
		int32 Millisecond = 0;   // 0 から 999
	};
	FLocalClock GetLocalClock();

	// 起動後の経過カウント（QueryPerformanceCounter）
	int64 GetTicks();
	// 1 秒あたりのカウント数
	int64 GetTicksPerSecond();

	float64 TicksToMs( int64 Ticks );
	float64 TicksToUs( int64 Ticks );

	// 呼び出したスレッドの ID
	uint32 GetCurrentThreadId();
}
