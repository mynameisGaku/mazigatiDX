// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;

//=============================================================================
// CAdventureGate
//   鍵のかかった門。タグ "Gate" の置き物に結び付ける（置き物自身が、通れない四角形でもある）。
//   鍵を持って E キーを押すと、置き物（門）が消えて、通れるようになる。開けたことは、セーブに残る。
//=============================================================================
class CAdventureGate : public CAdventureActor, public IAdventureInteractable
{
public:
	explicit CAdventureGate( const TSharedPtr<FAdventureState>& InState );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	FVector3 GetInteractPosition() const override;
	const char* GetPrompt() const override;
	void Interact() override;

private:
	TSharedPtr<FAdventureState> State;
	float32 Age = 0.0f;
};
