// SPDX-License-Identifier: MIT
#include "CharacterSample.h"
#include "Source/Character/CharacterStage.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// ゲーム側は、読み込み・骨の上書き・描画の共通窓口だけを使う。
	const TSampleRegistrar<CCharacterSample> Registrar( FSampleInfo { "Character", "Character: a PMX model moved by code", "Load a PMX model and animate its bones from code (head, arms, ears and tail) with frame pose overrides, no motion file.", 18 } );
}

void CCharacterSample::OnEnter()
{
	SpawnObject<CCharacterStage>();
}
