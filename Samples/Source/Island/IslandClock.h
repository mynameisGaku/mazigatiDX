// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandClock
//   時間を進め（一日の時刻は FIslandState::DayLength 秒で一巡）、数字キーで設定を切り替える。
//   1: 影  2: 霧  3: 時刻を止める
//=============================================================================
class CIslandClock : public CGameObject
{
public:
	explicit CIslandClock( const TSharedPtr<FIslandState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	TSharedPtr<FIslandState> State;
};
