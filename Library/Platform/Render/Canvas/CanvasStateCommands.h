// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"

// 2Dの描画の状態を替える命令（消去・合成・補間・明るさ・変換・クリップ・マスク・足す色・アルファテスト）を実行する。
class CCanvasStateCommands
{
public:
	explicit CCanvasStateCommands( FCanvasExecution& InRun );
	// アルファテストで、この命令を描かないか（平らな図形は、アルファが通らなければ丸ごと、画像・文字は、通りうる画素が無いときだけ）。
	bool ShouldSkipForAlphaTest( const FDrawCommandView& View );
	// 状態の命令なら実行して true。ほかの命令なら false。
	bool Execute( const FDrawCommandView& View );

private:
	bool GetImage( int32 Handle, FTextureDrawView& Image );
	void ApplyMask();
	bool AlphaTestPasses( int32 Value );
	bool AlphaTestMayPass();
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	IRenderSurface& Surface; // Run.Surface の別名。
	IRenderCanvas2D*& Canvas; // Run.Canvas の別名。
	CGeometryCommandExecutor& Geometry; // Run.Geometry の別名。
	bool& bStopped; // Run.bStopped の別名。
	bool& bLinearFilter; // Run.State.bLinearFilter の別名。
	bool& bExplicitFilter; // Run.State.bExplicitFilter の別名。
	bool& bAlphaBlend; // Run.State.bAlphaBlend の別名。
	FDrawRgb& Bright; // Run.State.Bright の別名。
	int32& Alpha; // Run.State.Alpha の別名。
	int32 ( &AddColorValue )[3]; // Run.State.AddColorValue の別名。
	bool& bAlphaTest; // Run.State.bAlphaTest の別名。
	EDrawCompare& AlphaTestMode; // Run.State.AlphaTestMode の別名。
	int32& AlphaTestReference; // Run.State.AlphaTestReference の別名。
	int32& ClipDepth; // Run.State.ClipDepth の別名。
	FCanvasTransform& Transform; // Run.State.Transform の別名。
	TArray<FCanvasMaskState>& MaskStack; // Run.State.MaskStack の別名。
};
