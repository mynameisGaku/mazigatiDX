// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// NDxLibVertex
//   描画コマンドの頂点（FDrawVertex3D）を、DxLib の頂点（VERTEX3D）にする。頂点の並びの描画（DxLibShapes3D）と、頂点バッファへの書き込み（DxLibAssetBackend）が使う。
//=============================================================================
namespace NDxLibVertex
{
	// 頂点 1 つを DxLib の頂点にする。数値（位置・向き・画像の座標）が有限でなければ、false（OutVertex は変えない）
	bool ToDxVertex( const FDrawVertex3D& Source, VERTEX3D& OutVertex );
}
