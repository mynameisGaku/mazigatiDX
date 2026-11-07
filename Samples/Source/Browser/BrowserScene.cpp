#include "BrowserScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Math/Math.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// 一覧の 1 行の高さと、最初の行の位置（ピクセル）
	constexpr int32 FirstRowY = 120;
	constexpr int32 RowHeight = 28;

	// 一覧の列の数と、列の幅、最初の列の左端（ピクセル）
	constexpr int32 NumColumns = 2;
	constexpr int32 ColumnWidth = 600;
	constexpr int32 FirstColumnX = 32;

	// 選んでいるサンプルの説明の位置（画面の下に固定。サンプルが増えても動かない）
	constexpr int32 DescriptionY = 660;
}

void CBrowserScene::OnEnter()
{
	Entries = CSampleCatalog::Get().GetSorted();

	// 最後に選んでいたサンプルを選ぶ（無ければ先頭）
	SelectedIndex = 0;
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		if ( Entries[i].Name == CSampleCatalog::Get().GetLastSelected() )
		{
			SelectedIndex = i;
		}
	}
}

void CBrowserScene::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr || Entries.IsEmpty() )
	{
		return;
	}

	if ( Input->IsKeyPressed( EKey::Up ) || Input->IsKeyPressed( EKey::W ) )
	{
		MoveSelection( -1 );
	}
	if ( Input->IsKeyPressed( EKey::Down ) || Input->IsKeyPressed( EKey::S ) )
	{
		MoveSelection( 1 );
	}

	if ( Input->IsKeyPressed( EKey::Left ) || Input->IsKeyPressed( EKey::A ) )
	{
		MoveSelection( -GetRowsPerColumn() );
	}
	if ( Input->IsKeyPressed( EKey::Right ) || Input->IsKeyPressed( EKey::D ) )
	{
		MoveSelection( GetRowsPerColumn() );
	}

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
	Screen.Text( 16, 40, "[Up / Down / Left / Right] Choose    [Enter] Run    [Esc] (in a sample) Back to this list", FColor32::Gray );

	if ( Entries.IsEmpty() )
	{
		Screen.Text( 32, FirstRowY, "No samples are registered.", FColor32::Orange );
		return;
	}

	const int32 RowsPerColumn = GetRowsPerColumn();
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		const bool bSelected = i == SelectedIndex;
		const int32 X = FirstColumnX + ( i / RowsPerColumn ) * ColumnWidth;
		const int32 Y = FirstRowY + ( i % RowsPerColumn ) * RowHeight;
		Screen.TextF( X, Y, bSelected ? FColor32::Yellow : FColor32::White, "%s %s", bSelected ? ">" : " ", *Entries[i].Title );
	}

	// 選んでいるサンプルの説明
	Screen.Text( FirstColumnX, DescriptionY, *Entries[SelectedIndex].Description, FColor32::Gray );
}

int32 CBrowserScene::GetRowsPerColumn() const
{
	return NMath::Max( 1, ( Entries.Num() + NumColumns - 1 ) / NumColumns );
}

void CBrowserScene::MoveSelection( int32 Delta )
{
	SelectedIndex = NMath::Clamp( SelectedIndex + Delta, 0, Entries.Num() - 1 );
}
