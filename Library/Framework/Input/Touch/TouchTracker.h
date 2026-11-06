// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Input/Touch/TouchTypes.h"

//=============================================================================
// FTouchTracker
//   毎フレームの「いま触れている指」の一覧から、「このフレームで触れ始めた指」と「離れた指」を求める。
//   ITouchInput を実装するバックエンドが共通で使う（バックエンドは、いまの指の一覧だけ渡せばよい）。
//
//   Tracker.Update( RawTouches );                 // 毎フレーム 1 回
//   for ( const FTouchPoint& Touch : Tracker.GetTouches() ) { ... Touch.bPressedThisFrame ... }
//   for ( uint32 Id : Tracker.GetReleasedIds() ) { ... }
//
//   - 前のフレームに無かった ID が bPressedThisFrame になる。前のフレームにあって今回無い ID が、離れた指になる
//   - 同じ ID が 1 フレームの中に複数あるときは、最初のものだけ使う
//=============================================================================
class FTouchTracker
{
public:
	// 今回の指の一覧（bPressedThisFrame は無視する）を反映する
	void Update( const TArray<FTouchPoint>& RawTouches );

	// 状態を空に戻す
	void Reset();

	const TArray<FTouchPoint>& GetTouches() const
	{
		return Touches;
	}

	const TArray<uint32>& GetReleasedIds() const
	{
		return ReleasedIds;
	}

private:
	TArray<FTouchPoint> Touches;
	TArray<uint32> ReleasedIds;
};
