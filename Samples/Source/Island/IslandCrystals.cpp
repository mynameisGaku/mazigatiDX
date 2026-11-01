#include "IslandCrystals.h"

namespace
{
	// 拾える距離（水平）と、高さの差の上限
	constexpr float32 PickupDistance = 1.6f;
	constexpr float32 PickupHeight = 3.0f;

	// 全部拾ってから、結晶が戻るまでの時間（秒）
	constexpr float32 RespawnDelay = 6.0f;
}

CIslandCrystals::CIslandCrystals( const TSharedPtr<FIslandState>& InState )
	: State( InState )
{
}

void CIslandCrystals::Tick( float32 DeltaTime )
{
	( void )DeltaTime;
	if ( !State.IsValid() )
	{
		return;
	}
	for ( FIslandState::FCrystal& Crystal : State->Crystals )
	{
		if ( Crystal.bTaken )
		{
			continue;
		}
		const FVector3 Delta = Crystal.Position - State->ExplorerPosition;
		if ( Delta.x * Delta.x + Delta.z * Delta.z < PickupDistance * PickupDistance && NMath::Abs( Delta.y ) < PickupHeight )
		{
			Crystal.bTaken = true;
			Crystal.TakenTime = State->Time;
			++State->NumTaken;
		}
	}

	// 全部拾ったら、少し待って、元に戻す
	if ( State->Crystals.Num() > 0 && State->NumTaken >= State->Crystals.Num() )
	{
		if ( State->AllTakenTime < 0.0f )
		{
			State->AllTakenTime = State->Time;
		}
		else if ( State->Time - State->AllTakenTime > RespawnDelay )
		{
			for ( FIslandState::FCrystal& Crystal : State->Crystals )
			{
				Crystal.bTaken = false;
			}
			State->NumTaken = 0;
			State->AllTakenTime = -1.0f;
		}
	}
}
