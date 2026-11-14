// SPDX-License-Identifier: MIT
#pragma once

class IRenderCanvas2D;
class IRenderGeometry3D;

// 描画命令の実行中だけ借りる2Dと3Dの窓口。値やポインターを実行後へ保持しない。
struct FRenderCommandContext
{
	// 現在の描画先の2D実行先。フレーム外ではnullptr。
	IRenderCanvas2D *Canvas = nullptr;
	// 現在の描画先の3D実行先。フレーム外・非対応ではnullptr。
	IRenderGeometry3D *Geometry = nullptr;
};
