#include "PlatformerHud.h"

#include "Library/Framework/Render/Draw/DrawContext.h"

namespace
{
	const char* GetFormName( EPlatformerForm Form )
	{
		switch ( Form )
		{
		case EPlatformerForm::Big:
			return "Big";
		case EPlatformerForm::Fire:
			return "Fire";
		default:
			return "Small";
		}
	}
}

void NPlatformerHud::Draw( FDrawContext& CommandList, const FPlatformerStatus& Status, bool bGoalReached, bool bGameOver )
{
	// 上の帯（サンプルの見出しの背景）
	CommandList.Box( 0, 0, 1280, 92, FColor32( 20, 24, 36 ), true );
	CommandList.TextF( 772, 16, FColor32::White, "SCORE %06d    COINS x%02d    LIVES x%d", Status.Score, Status.Coins, Status.Lives );
	if ( Status.HasStar() )
	{
		CommandList.TextF( 772, 40, FColor32( 255, 230, 90 ), "FORM: %s    STAR %.1f s", GetFormName( Status.Form ), Status.StarTime );
	}
	else
	{
		CommandList.TextF( 772, 40, FColor32::White, "FORM: %s", GetFormName( Status.Form ) );
	}

	if ( bGoalReached )
	{
		CommandList.Text( 480, 280, "COURSE CLEAR!", FColor32( 255, 230, 90 ) );
		CommandList.Text( 440, 308, "Press Enter to play again", FColor32::White );
	}
	else if ( bGameOver )
	{
		CommandList.Text( 520, 280, "GAME OVER", FColor32( 240, 80, 70 ) );
		CommandList.Text( 440, 308, "Press Enter to play again", FColor32::White );
	}
}
