#include "AdventureGate.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Adventure/AdventureState.h"

CAdventureGate::CAdventureGate( const TSharedPtr<FAdventureState>& InState )
	: State( InState )
{}

FVector3 CAdventureGate::GetInteractPosition() const
{
	return GetPlaceholderPosition();
}

const char* CAdventureGate::GetPrompt() const
{
	return State->Keys > 0 ? "Open the gate with the key" : "The gate is locked";
}

void CAdventureGate::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
}

void CAdventureGate::Interact()
{
	if ( State->Keys <= 0 )
	{
		State->Say( "The gate is locked. Find a key." );
		return;
	}
	--State->Keys;
	State->bGateOpen = true;
	State->Say( "The gate opens." );
	State->PlaySound( State->OpenSound, 0.7f, 0.4f );
	State->PlayEffect( "Effects/sparks.gxeffect", GetPlaceholderPosition() + FVector3( 0.0f, 1.2f, 0.0f ), 2.0f );
	// 置き物（門）を消す: 通れない四角形も、一緒に消える
	if ( CGameObject* Placeholder = GetParent() )
	{
		Placeholder->Destroy();
	}
}

void CAdventureGate::Record3D( FDrawContext& CommandList )
{
	// 鍵穴の目印（錠）
	const FVector3 At = GetPlaceholderPosition();
	CommandList.Sphere3D( At + FVector3( 0.0f, 1.4f, -0.55f ), 0.22f, 10, FColor32( 230, 200, 80 ) );
	CommandList.Cube3D( At + FVector3( -0.06f, 1.0f, -0.6f ), At + FVector3( 0.06f, 1.25f, -0.52f ), FColor32( 40, 30, 20 ) );
}
