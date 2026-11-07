// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;

//=============================================================================
// CAdventureChest
//   宝箱。タグ "Chest"（プロパティ "Item"・"Count"）の置き物に結び付ける。E キーで開けて、中身（回復薬・宝石・鍵）をもらう。
//   開けた宝箱は、セーブに残る（読み込むと、開いたまま）。
//=============================================================================
class CAdventureChest : public CAdventureActor, public IAdventureInteractable
{
public:
	CAdventureChest( const TSharedPtr<FAdventureState>& InState, const FString& InItem, int32 InCount, int32 InPlaceholderId, bool bInOpened );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	FVector3 GetInteractPosition() const override;
	const char* GetPrompt() const override { return "Open the chest"; }
	bool CanInteract() const override { return !bOpened; }
	void Interact() override;

private:
	TSharedPtr<FAdventureState> State;
	FString Item;
	int32 Count;
	int32 PlaceholderId;
	bool bOpened;
	float32 Age = 0.0f;
};
