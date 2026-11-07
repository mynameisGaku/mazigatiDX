// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;

//=============================================================================
// CAdventureCampfire
//   焚き火。タグ "Campfire" の置き物に結び付ける。E キーで休むと、体力が全回復して、ここまでの進み具合を、セーブする。
//   見た目（炎）は、ワールドの Effect 部品。
//=============================================================================
class CAdventureCampfire : public CAdventureActor, public IAdventureInteractable
{
public:
	explicit CAdventureCampfire( const TSharedPtr<FAdventureState>& InState );

	FVector3 GetInteractPosition() const override;
	const char* GetPrompt() const override { return "Rest at the fire (heal and save)"; }
	void Interact() override;

private:
	TSharedPtr<FAdventureState> State;
};
