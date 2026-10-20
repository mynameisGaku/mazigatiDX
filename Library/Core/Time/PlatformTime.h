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
	// 起動後の経過カウント（QueryPerformanceCounter）
	int64 GetTicks();
	// 1 秒あたりのカウント数
	int64 GetTicksPerSecond();

	float64 TicksToMs( int64 Ticks );
	float64 TicksToUs( int64 Ticks );

	// 呼び出したスレッドの ID
	uint32 GetCurrentThreadId();
}
