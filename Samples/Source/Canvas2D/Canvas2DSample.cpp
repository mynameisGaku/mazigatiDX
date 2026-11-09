// SPDX-License-Identifier: MIT
#include "Canvas2DSample.h"
#include "Source/Canvas2D/Canvas2DGallery.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// 同じ見本をDxLib・Null・DX12で選べる。
	const TSampleRegistrar<CCanvas2DSample> Registrar( FSampleInfo { "Canvas2D", "Canvas2D: backend basics", "Shapes, clipping, alpha and a generated image using common draw commands.", 15 } );
}

void CCanvas2DSample::OnEnter()
{
	SpawnObject<CCanvas2DGallery>();
}
