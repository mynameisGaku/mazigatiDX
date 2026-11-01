#include "IslandClock.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

CIslandClock::CIslandClock( const TSharedPtr<FIslandState>& InState )
	: State( InState )
{
}

void CIslandClock::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	State->Time += DeltaTime;
	if ( State->bTimeRunning )
	{
		State->DayTime = NMath::Fmod( State->DayTime + DeltaTime / FIslandState::DayLength, 1.0f );
	}

	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::Num1 ) )
	{
		State->bShadows = !State->bShadows;
	}
	if ( Input->IsKeyPressed( EKey::Num2 ) )
	{
		State->bFog = !State->bFog;
	}
	if ( Input->IsKeyPressed( EKey::Num3 ) )
	{
		State->bTimeRunning = !State->bTimeRunning;
	}
}
