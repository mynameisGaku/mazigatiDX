#include "BrowserScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Library/Math/Math.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// 一覧の 1 行の高さと、最初の行の位置（ピクセル）
	constexpr int32 FirstRowY = 120;
	constexpr int32 RowHeight = 28;
}

void CBrowserScene::OnEnter()
{
	Entries = CSampleCatalog::Get().GetSorted();

	// 最後に選んでいたサンプルを選ぶ（無ければ先頭）
	SelectedIndex = 0;
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		if ( Entries[i].Name == CSampleCatalog::Get().GetLastSelected() ) SelectedIndex = i;
	}
}

void CBrowserScene::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr || Entries.IsEmpty() ) return;

	if ( Input->IsKeyPressed( EKey::Up ) || Input->IsKeyPressed( EKey::W ) ) MoveSelection( -1 );
	if ( Input->IsKeyPressed( EKey::Down ) || Input->IsKeyPressed( EKey::S ) ) MoveSelection( 1 );

	if ( Input->IsKeyPressed( EKey::Enter ) || Input->IsKeyPressed( EKey::Space ) )
	{
		const FSampleInfo& Selected = Entries[SelectedIndex];
		CSampleCatalog::Get().SetLastSelected( Selected.Name );
		GetSceneManager()->ChangeScene( Selected.Name );
	}
}

void CBrowserScene::OnDraw()
{
	FImmediateDraw Screen( GetDrawExecutor() );
	Screen.Text( 16, 16, "mazigatiDX Samples", FColor32::White );
	Screen.Text( 16, 40, "[Up / Down] Choose    [Enter] Run    [Esc] (in a sample) Back to this list", FColor32::Gray );

	if ( Entries.IsEmpty() )
	{
		Screen.Text( 32, FirstRowY, "No samples are registered.", FColor32::Orange );
		return;
	}

	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		const bool bSelected = i == SelectedIndex;
		Screen.TextF( 32, FirstRowY + i * RowHeight, bSelected ? FColor32::Yellow : FColor32::White, "%s %s", bSelected ? ">" : " ", *Entries[i].Title );
	}

	// 選んでいるサンプルの説明
	const int32 DescriptionY = FirstRowY + Entries.Num() * RowHeight + 24;
	Screen.Text( 32, DescriptionY, *Entries[SelectedIndex].Description, FColor32::Gray );
}

void CBrowserScene::MoveSelection( int32 Delta )
{
	SelectedIndex = NMath::Clamp( SelectedIndex + Delta, 0, Entries.Num() - 1 );
}
