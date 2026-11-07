#include "Platformer3DHud.h"

#include "Source/Common/PlatformerHud.h"
#include "Source/Platformer3D/Platformer3DState.h"

CPlatformer3DHud::CPlatformer3DHud( const TSharedPtr<FPlatformer3DState>& InState )
	: State( InState )
{}

void CPlatformer3DHud::Draw( FDrawContext& CommandList )
{
	NPlatformerHud::Draw( CommandList, State->Status, State->bGoalReached, State->bGameOver );
}
