// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Input/Touch/TouchTypes.h"

//=============================================================================
// ITouchInput
//   画面に触れている指（ペン）の窓口。毎フレームに一度取り込む。
//   実装: CDxLibInputSubsystem（DxLib）、CNullInputSubsystem（検査用）。すべてメインスレッドから使う。
//   タッチ画面が無い機器・対応していない実装では、IsTouchAvailable が false で、触れている指は常に空。
//=============================================================================
class ITouchInput
{
public:
	virtual ~ITouchInput() = default;

	// タッチを読める実装か（機器にタッチ画面があるかは分からないことがある）
	virtual bool IsTouchAvailable() const = 0;
	// 触れている指（このフレームの状態）。参照は次の入力更新まで有効
	virtual const TArray<FTouchPoint>& GetTouches() const = 0;
	// このフレームで離された指の ID。参照は次の入力更新まで有効
	virtual const TArray<uint32>& GetReleasedTouchIds() const = 0;
};
