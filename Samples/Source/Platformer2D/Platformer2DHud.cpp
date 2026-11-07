#include "Platformer2DHud.h"

#include "Source/Common/PlatformerHud.h"
#include "Source/Platformer2D/Platformer2DState.h"

CPlatformer2DHud::CPlatformer2DHud( const TSharedPtr<FPlatformer2DState>& InState )
	: State( InState )
{}

void CPlatformer2DHud::Draw( FDrawContext& CommandList )
{
	NPlatformerHud::Draw( CommandList, State->Status, State->bGoalReached, State->bGameOver );
}
