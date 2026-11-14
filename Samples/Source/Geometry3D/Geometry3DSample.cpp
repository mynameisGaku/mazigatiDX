// SPDX-License-Identifier: MIT
#include "Geometry3DSample.h"
#include "Source/Geometry3D/Geometry3DGallery.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// 描画APIを指定せず、同じ場面を各バックエンドへ渡す。
	const TSampleRegistrar<CGeometry3DSample> Registrar( FSampleInfo { "Geometry3D", "Geometry3D: camera and depth", "Unlit boxes, spheres, capsules, billboards, textured meshes and vertex/index buffers with camera and depth testing.", 16 } );
}

void CGeometry3DSample::OnEnter()
{
	SpawnObject<CGeometry3DGallery>();
}
