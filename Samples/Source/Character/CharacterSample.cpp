// SPDX-License-Identifier: MIT
#include "CharacterSample.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Character/CharacterStage.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// ゲーム側は、読み込み・骨の上書き・描画の共通窓口だけを使う。
	const TSampleRegistrar<CCharacterSample> Registrar( FSampleInfo { "Character", "Character: a PMX model moved by code", "Load a PMX model and animate its bones from code (head, arms, ears and tail) with frame pose overrides, no motion file.", 18 } );
}

void CCharacterSample::OnEnter()
{
	Stage = SpawnObject<CCharacterStage>();
}

void CCharacterSample::OnSampleDraw( FImmediateDraw& Screen )
{
	if ( Stage && Stage->HasFrozenPose() )
	{
		Screen.Flush();
		if ( IDrawExecutor* Executor = GetDrawExecutor() ) { Stage->DrawFrozen( *Executor ); }
	}
}
