// SPDX-License-Identifier: MIT
#include "StaticModelSample.h"
#include "Source/StaticModel/StaticModelGallery.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// ゲーム側は読み込み・描画・見た目の共通窓口だけを使う。
	const TSampleRegistrar<CStaticModelSample> Registrar( FSampleInfo { "StaticModel", "StaticModel: shared geometry", "Load an OBJ, create two instances and change opacity independently.", 17 } );
}

void CStaticModelSample::OnEnter()
{
	SpawnObject<CStaticModelGallery>();
}
