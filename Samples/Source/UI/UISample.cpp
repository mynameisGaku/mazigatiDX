#include "UISample.h"

#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/UI/UIScreen.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CUISample> Registrar( FSampleInfo { "UI", "UI: widgets, brushes, render target", "A window built from UI widgets (button, checkbox, slider, progress bar, label, icons) with the engine's built-in images, and a render target shown inside a widget.", 130 } );
}

void CUISample::OnEnter()
{
	SpawnObject<CUIScreen>();
}

void CUISample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Mouse, arrow keys / Tab, Enter and Escape (or a gamepad) operate the UI.", FColor32::Gray );
}
