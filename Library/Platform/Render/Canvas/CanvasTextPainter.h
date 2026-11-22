// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"
#include "Library/Platform/Render/Canvas/CanvasImagePainter.h"

// 文字列（既定・フォント指定・位置揃え・拡大・回転つき）を、2Dのキャンバスへ描く。
class CCanvasTextPainter
{
public:
	CCanvasTextPainter( FCanvasExecution& InRun, CCanvasImagePainter& InImages );
	// 文字の命令なら実行して true。ほかの命令なら false。
	bool Execute( const FDrawCommandView& View );

private:
	void Unsupported( const FDrawCommandView& View, const char* Reason );
	FColor32 Color( FDrawRgb Value );
	bool GetImage( int32 Handle, FTextureDrawView& Image );
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	CCanvasImagePainter& Images; // 絵文字などの画像を貼るために使う。
	IFontDrawSource*& Fonts; // Run.Fonts の別名。
	IRenderCanvas2D*& Canvas; // Run.Canvas の別名。
	bool& bStopped; // Run.bStopped の別名。
	FCanvasTransform& Transform; // Run.State.Transform の別名。
};
