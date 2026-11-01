// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandCrystals
//   結晶を拾う。探検家が近づくと拾い（数が増える）、全部拾うと、少しして、すべての結晶が元の場所に戻る。
//   描くのは NIslandScenery（影の絵と同じ流れで描くため）。ここは、拾ったかどうかだけを決める。
//=============================================================================
class CIslandCrystals : public CGameObject
{
public:
	explicit CIslandCrystals( const TSharedPtr<FIslandState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	TSharedPtr<FIslandState> State;
};
