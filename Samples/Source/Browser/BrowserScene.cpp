#include "BrowserScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Math/Math.h"
#include "Library/Platform/System/CommandLine.h"
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

		// 選んでいるサンプルの説明の位置（画面の下に固定。サンプルが増えても動かない）。長い説明は、画面の幅で折り返して、数行に分けて描く
	constexpr int32 DescriptionY = 630;
	constexpr int32 DescriptionLineHeight = 17;
	// 1 行の文字数と、行数の上限。1 行が長すぎる文字列は、描画先によっては描けない（DX12 は、1 行が 4096 ピクセルを超える文字列を描けず、フレームごと失敗する）。画面の幅（1280。1 文字は約 9 ピクセル）にも収まる長さにする
	constexpr int32 DescriptionMaxChars = 130;
	constexpr int32 DescriptionMaxLines = 5;

	// Text を、1 行が MaxChars 文字までになるよう、空白で折り返す（空白がなければ、その長さで切る。UTF-8 の文字の途中では切らない）。MaxLines に入り切らない分は、最後の行の終わりを「...」にする
	TArray<FString> WrapText( const FString& Text, int32 MaxChars, int32 MaxLines )
	{
		TArray<FString> Lines;
		const char* Data = *Text;
		const int32 Length = Text.Len();
		int32 Start = 0;
		while ( Start < Length && Lines.Num() < MaxLines )
		{
			int32 End = NMath::Min( Start + MaxChars, Length );
			if ( End < Length )
			{
				int32 Break = End;
				while ( Break > Start && Data[Break] != ' ' )
				{
					--Break;
				}
				if ( Break > Start )
				{
					End = Break;
				}
				else
				{
					while ( End > Start + 1 && ( static_cast<uint8>( Data[End] ) & 0xC0 ) == 0x80 )
					{
						--End;
					}
				}
			}
			Lines.Add( FString( Data + Start, End - Start ) );
			Start = End;
			while ( Start < Length && Data[Start] == ' ' )
			{
				++Start;
			}
		}
		if ( Start < Length && Lines.Num() > 0 )
		{
			Lines[Lines.Num() - 1] = Lines[Lines.Num() - 1] + "...";
		}
		return Lines;
	}
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

	// 検査用: --browser-select=<サンプル名> で、最初に選んでいるサンプルを指定する（キー操作なしで、その項目を選んだときの画面を確かめる）。最初に入ったときだけ
	static bool bApplied = false;
	if ( !bApplied )
	{
		bApplied = true;
		const TOptional<FString> Requested = NCommandLine::GetValue( "browser-select" );
		if ( Requested.IsSet() )
		{
			for ( int32 i = 0; i < Entries.Num(); ++i )
			{
				if ( Entries[i].Name == Requested.GetValue() )
				{
					SelectedIndex = i;
				}
			}
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

	// 選んでいるサンプルの説明（折り返して、数行で）
	const TArray<FString> DescriptionLines = WrapText( Entries[SelectedIndex].Description, DescriptionMaxChars, DescriptionMaxLines );
	for ( int32 i = 0; i < DescriptionLines.Num(); ++i )
	{
		Screen.Text( FirstColumnX, DescriptionY + i * DescriptionLineHeight, *DescriptionLines[i], FColor32::Gray );
	}
}

int32 CBrowserScene::GetRowsPerColumn() const
{
	return NMath::Max( 1, ( Entries.Num() + NumColumns - 1 ) / NumColumns );
}

void CBrowserScene::MoveSelection( int32 Delta )
{
	SelectedIndex = NMath::Clamp( SelectedIndex + Delta, 0, Entries.Num() - 1 );
}
