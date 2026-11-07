#include "WorldSampleHud.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/World/WorldSampleState.h"

CWorldSampleHud::CWorldSampleHud( const TSharedPtr<FWorldSampleState>& InState )
	: State( InState )
{}

void CWorldSampleHud::Draw( FDrawContext& CommandList )
{
	CommandList.Box( 0, 0, FWorldSampleState::ScreenWidth, 92, FColor32( 20, 24, 36 ), true );
	CommandList.TextF( 772, 16, FColor32::White, "COINS %d / %d    SCORE %d", State->CoinsCollected, State->CoinsTotal, State->ScoreCollected );
	CommandList.TextF( 772, 40, FColor32( 190, 196, 216 ), "courtyard.gxworld: %d scenery   loaded x%d", State->Scenery.Num(), State->NumLoads );
	if ( State->CoinsTotal > 0 && State->CoinsCollected >= State->CoinsTotal )
	{
		CommandList.Text( 440, 280, "ALL COINS COLLECTED!  Press R to load the world again", FColor32( 255, 230, 90 ) );
	}
}
