// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;

//=============================================================================
// CAdventurePickup
//   拾うもの（回復薬・宝石）。タグ "Pickup"（プロパティ "Item"）の置き物に結び付けるか、敵が落とす（その場に、直接置く）。
//   ふわふわ浮いて、回って、主人公が近づいたら、拾われる。
//=============================================================================
class CAdventurePickup : public CAdventureActor
{
public:
	// InPlaceholderId が 0 でなければ、置き物に結び付いている（拾ったことを、セーブに残す）。そうでなければ、InPosition の場所にある
	CAdventurePickup( const TSharedPtr<FAdventureState>& InState, const FString& InItem, int32 InPlaceholderId, const FVector3& InPosition );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

private:
	FVector3 GetPosition() const;

	TSharedPtr<FAdventureState> State;
	FString Item;
	int32 PlaceholderId;
	FVector3 Position;
	float32 Age = 0.0f;
};
