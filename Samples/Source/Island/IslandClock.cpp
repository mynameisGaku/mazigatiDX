#include "IslandClock.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/CommandLine.h"

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
	// 絵を比べる検査のために、--island-fixed で、時計と昼夜を止める（GPU が影を受ける経路と、CPU の経路を、同じ絵で比べる）。
	static const bool bFixed = NCommandLine::HasFlag( "island-fixed" );
	if ( bFixed )
	{
		DeltaTime = 0.0f;
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
