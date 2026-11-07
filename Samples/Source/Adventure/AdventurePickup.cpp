#include "AdventurePickup.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Adventure/AdventureState.h"

namespace
{
	constexpr float32 PickupRadius = 1.0f;
}

CAdventurePickup::CAdventurePickup( const TSharedPtr<FAdventureState>& InState, const FString& InItem, int32 InPlaceholderId, const FVector3& InPosition )
	: State( InState )
	, Item( InItem )
	, PlaceholderId( InPlaceholderId )
	, Position( InPosition )
{}

FVector3 CAdventurePickup::GetPosition() const
{
	return PlaceholderId != 0 ? GetPlaceholderPosition() : Position;
}

void CAdventurePickup::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
	if ( State->bPlayerDead )
	{
		return;
	}
	const FVector3 At = GetPosition();
	const float32 DX = State->PlayerPosition.x - At.x;
	const float32 DZ = State->PlayerPosition.z - At.z;
	if ( DX * DX + DZ * DZ > PickupRadius * PickupRadius )
	{
		return;
	}
	if ( Item == "Potion" )
	{
		if ( State->Potions >= FAdventureState::MaxPotions )
		{
			return;
		}
		++State->Potions;
		State->Say( "Picked up a potion." );
	}
	else
	{
		++State->Gems;
		State->Say( "Picked up a gem." );
	}
	State->PlaySound( State->PickupSound, 1.0f, 0.35f );
	if ( PlaceholderId != 0 )
	{
		State->PickedUp.Add( PlaceholderId );
	}
	Destroy();
}

void CAdventurePickup::Record3D( FDrawContext& CommandList )
{
	const FVector3 Center = GetPosition() + FVector3( 0.0f, 0.55f + 0.12f * NMath::Sin( Age * 3.0f ), 0.0f );
	if ( Item == "Potion" )
	{
		// 瓶: 赤い液体の丸と、細い口
		CommandList.Sphere3D( Center, 0.24f, 10, FColor32( 230, 70, 90 ) );
		CommandList.Capsule3D( Center + FVector3( 0.0f, 0.2f, 0.0f ), Center + FVector3( 0.0f, 0.4f, 0.0f ), 0.08f, 6, FColor32( 220, 230, 240 ) );
	}
	else
	{
		// 宝石: 2 つの円すいを合わせた形。回る
		const float32 Spin = Age * 2.0f;
		CommandList.Cone3D( Center + FVector3( 0.0f, 0.28f, 0.0f ), Center, 0.22f, 6, FColor32( 120, 230, 250 ) );
		CommandList.Cone3D( Center - FVector3( 0.0f, 0.28f, 0.0f ), Center, 0.22f, 6, FColor32( 80, 190, 230 ) );
		CommandList.Sphere3D( Center + FVector3( NMath::Cos( Spin ) * 0.22f, 0.0f, NMath::Sin( Spin ) * 0.22f ), 0.04f, 6, FColor32::White );
	}
	// 足元の影
	CommandList.Cone3D( GetPosition() + FVector3( 0.0f, 0.03f, 0.0f ), GetPosition() + FVector3( 0.0f, 0.005f, 0.0f ), 0.3f, 12, FColor32( 24, 28, 24 ) );
}
