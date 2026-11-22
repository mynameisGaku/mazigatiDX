// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"

// 平らな図形（四角・線・円・楕円・角丸・点・三角・四角形）を、2Dのキャンバスへ描く。変換と色は、実行の状態から取る。
class CCanvasShapePainter
{
public:
	explicit CCanvasShapePainter( FCanvasExecution& InRun );
	// 図形の命令なら実行して true。ほかの命令なら false。
	bool Execute( const FDrawCommandView& View );

private:
	// 凸な点列（XYの順）を、変換してから描く。
	void DrawPolygon( const float32* Points, int32 Count, FColor32 Value, bool bFill, float32 Thickness );
	// 楕円（半径が同じなら円）を、点列にして描く。
	void DrawEllipse( float32 X, float32 Y, float32 Rx, float32 Ry, FColor32 Value, bool bFill, float32 Thickness, int32 Segments );
	FColor32 ShapeColor( FDrawRgb Value );
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	IRenderCanvas2D*& Canvas; // Run.Canvas の別名。
	bool& bStopped; // Run.bStopped の別名。
	FCanvasTransform& Transform; // Run.State.Transform の別名。
};
