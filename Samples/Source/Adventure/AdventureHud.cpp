#include "AdventureHud.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Adventure/AdventureState.h"

CAdventureHud::CAdventureHud( const TSharedPtr<FAdventureState>& InState )
	: State( InState )
{}

void CAdventureHud::Draw( FDrawContext& CommandList )
{
	// 上の帯: 体力・持ち物・目的
	CommandList.Box( 0, 0, FAdventureState::ScreenWidth, 92, FColor32( 20, 22, 32 ), true );
	CommandList.TextF( 772, 14, FColor32::White, "HP %d / %d", State->Hp, FAdventureState::MaxHp );
	const int32 BarX = 890;
	CommandList.Box( BarX, 16, BarX + 300, 32, FColor32( 60, 30, 30 ), true );
	CommandList.Box( BarX, 16, BarX + 300 * State->Hp / FAdventureState::MaxHp, 32, State->Hp > 30 ? FColor32( 90, 200, 110 ) : FColor32( 230, 80, 70 ), true );
	CommandList.TextF( 772, 40, FColor32( 255, 224, 140 ), "Potions %d [F]    Key %d    Gems %d", State->Potions, State->Keys, State->Gems );
	const char* Objective = State->bWon ? "You escaped!" : ( State->bGateOpen ? "Objective: reach the altar" : ( State->Keys > 0 ? "Objective: open the gate" : "Objective: find the key" ) );
	CommandList.Text( 772, 62, Objective, FColor32( 170, 176, 200 ) );

	// 操作の案内（下の端）
	CommandList.Text( 20, 694, "WASD move   Arrows camera   Space attack   Shift roll   E interact   F potion   F5 save   F9 load", FColor32( 190, 196, 216 ) );

	// メッセージの履歴（下）
	for ( int32 Index = 0; Index < State->Messages.Num(); ++Index )
	{
		const bool bNewest = Index == State->Messages.Num() - 1;
		CommandList.Text( 20, 640 - ( State->Messages.Num() - 1 - Index ) * 22, *State->Messages[Index], bNewest ? FColor32::White : FColor32( 150, 154, 176 ) );
	}
	// 調べられるものの案内
	if ( !State->Prompt.IsEmpty() && !State->bPlayerDead )
	{
		CommandList.Box( 440, 560, 840, 596, FColor32( 20, 22, 32 ), true );
		CommandList.Box( 440, 560, 840, 596, FColor32( 230, 210, 140 ), false, 1 );
		CommandList.Text( 456, 570, *State->Prompt, FColor32( 255, 235, 160 ) );
	}
	if ( State->bPlayerDead )
	{
		CommandList.Text( 470, 280, "YOU DIED", FColor32( 240, 80, 70 ) );
		CommandList.Text( 400, 308, "R: start again    F9: load the last save", FColor32::White );
	}
	else if ( State->bWon )
	{
		CommandList.Text( 440, 280, "YOU ESCAPED THE RUINS!", FColor32( 255, 230, 90 ) );
		CommandList.Text( 440, 308, "R: play again", FColor32::White );
	}
}
