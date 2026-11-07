#include "AdventureCampfire.h"

#include "Source/Adventure/AdventureState.h"

CAdventureCampfire::CAdventureCampfire( const TSharedPtr<FAdventureState>& InState )
	: State( InState )
{}

FVector3 CAdventureCampfire::GetInteractPosition() const
{
	return GetPlaceholderPosition();
}

void CAdventureCampfire::Interact()
{
	State->Hp = FAdventureState::MaxHp;
	State->bSaveRequested = true;
	State->PlaySound( State->PickupSound, 0.6f, 0.3f );
}
