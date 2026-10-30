// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// NDxLibShapes3D
//   3D 図形の追加の命令を、DxLib へ流す。どれも、描くだけで、状態は変えない（描画の終わりに戻すものは無い）。
//     Pixel3D -> DrawPixel3D / Cone3D -> DrawCone3D
//     BillboardPivot3D -> DrawBillboard3D / BillboardQuad3D -> DrawModiBillboard3D
//     Sprite3D -> DrawRotaGraph3D / SpriteStretch3D -> DrawExtendGraph3D / SpritePivot3D -> DrawRota2Graph3D
//     Primitive3D -> DrawPrimitive3D / DrawPrimitiveIndexed3D
//   画像は常に透過して描く（TransFlag = TRUE）。
//   座標の向きの違い: DxLib の DrawBillboard3D / DrawRota2Graph3D は、基準点の縦を画像の下から数える（上向き）が、
//   フレームワークは Quad3D や 2D の SpritePivot と同じに、画像の左上から数える。実行のときに、縦を入れ替えて渡す。
//=============================================================================
namespace NDxLibShapes3D
{
	// 命令を実行する。この種類の命令でなければ、何もせず false。大きさや数が合わない壊れた命令は、何も描かず true（処理した）
	bool Execute( const FDrawCommandView& Command );
}
