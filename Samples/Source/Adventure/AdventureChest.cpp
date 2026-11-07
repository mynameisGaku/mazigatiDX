#include "AdventureChest.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Adventure/AdventureState.h"

CAdventureChest::CAdventureChest( const TSharedPtr<FAdventureState>& InState, const FString& InItem, int32 InCount, int32 InPlaceholderId, bool bInOpened )
	: State( InState )
	, Item( InItem )
	, Count( InCount )
	, PlaceholderId( InPlaceholderId )
	, bOpened( bInOpened )
{}

FVector3 CAdventureChest::GetInteractPosition() const
{
	return GetPlaceholderPosition();
}

void CAdventureChest::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
}

void CAdventureChest::Interact()
{
	if ( bOpened )
	{
		return;
	}
	bOpened = true;
	State->OpenedChests.Add( PlaceholderId );
	if ( Item == "Potion" )
	{
		State->Potions = NMath::Min( FAdventureState::MaxPotions, State->Potions + Count );
		State->Say( FString::Printf( "Found %d potion(s).", Count ) );
	}
	else if ( Item == "Gem" )
	{
		State->Gems += Count;
		State->Say( FString::Printf( "Found %d gem(s).", Count ) );
	}
	else
	{
		++State->Keys;
		State->Say( "Found a rusty key!" );
	}
	State->PlaySound( State->OpenSound, 1.0f, 0.4f );
	State->PlayEffect( "Effects/sparks.gxeffect", GetPlaceholderPosition() + FVector3( 0.0f, 0.9f, 0.0f ), 1.0f );
}

void CAdventureChest::Record3D( FDrawContext& CommandList )
{
	const FVector3 At = GetPlaceholderPosition();
	if ( bOpened )
	{
		// 開いた蓋（後ろへ倒れている）
		CommandList.Cube3D( At + FVector3( -0.55f, 0.8f, 0.4f ), At + FVector3( 0.55f, 1.25f, 0.5f ), FColor32( 110, 70, 30 ) );
		return;
	}
	// 閉じている: 留め金と、「調べられる」目印（ゆっくり上下する光る玉）
	CommandList.Cube3D( At + FVector3( -0.12f, 0.45f, -0.45f ), At + FVector3( 0.12f, 0.65f, -0.38f ), FColor32( 230, 200, 80 ) );
	CommandList.Sphere3D( At + FVector3( 0.0f, 1.4f + 0.1f * NMath::Sin( Age * 3.0f ), 0.0f ), 0.1f, 8, FColor32( 255, 235, 140 ) );
}
