// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"
#include "Library/Platform/Render/Canvas/CanvasTargetScopes.h"

// 画像の処理（フィルター・混ぜ合わせ）を実行する。変換先の画像のパスを開き、組み込みのピクセルシェーダーで描いて、元の描画先へ戻る。
class CCanvasImageEffects
{
public:
	explicit CCanvasImageEffects( FCanvasExecution& InRun, CCanvasTargetScopes& InTargetScopes );
	// 画像の処理の命令なら実行して true。ほかの命令なら false。
	bool Execute( const FDrawCommandView& View );

private:
	// 画像の処理（フィルター・混ぜ合わせ）。変換先の画像のパスを開き、その矩形へ、組み込みのピクセルシェーダーで描いて、元の描画先へ戻る。描けなければ false。
	// Constants は、シェーダーの定数（int4 を 4 つ。[0..1] = 種類・乗算済みか、[2] = 混ぜる強さ、[8..15] = 引数）。矩形の大きさと、元・先の左上は、ここで入れる。
	bool RunImageOperation( EBuiltInPixelShader Pixel, int32 DestHandle, int32 BaseHandle, int32 AuxHandle, bool bBlendToDest, bool bUseRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, int32 ( &Constants )[16] );
	// 画像の処理。元の画像（か、混ぜる画像）が変換先と同じなら、読みながら書けないので、一時の描画先へ処理してから、変換先へ写す（DxLibと同じく、その場の処理は、置き換える）。
	bool RunImageOperationBuffered( EBuiltInPixelShader Pixel, int32 DestHandle, int32 BaseHandle, int32 AuxHandle, bool bBlendToDest, bool bUseRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, int32 ( &Constants )[16] );
	void Unsupported( const FDrawCommandView& View, const char* Reason );
	bool GetImage( int32 Handle, FTextureDrawView& Image );
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	CCanvasTargetScopes& TargetScopes; // 連携する役割。
	IRenderSurface& Surface; // Run.Surface の別名。
	IRenderTargetDrawSource*& Targets; // Run.Targets の別名。
	IShaderDrawSource*& Shaders; // Run.Shaders の別名。
	CGeometryCommandExecutor& Geometry; // Run.Geometry の別名。
	bool& bStopped; // Run.bStopped の別名。
	TArray<FCanvasMaskState>& MaskStack; // Run.State.MaskStack の別名。
	FRenderTargetDrawView& CurrentTarget; // Run.TargetState.CurrentTarget の別名。
	bool& bMrtActive; // Run.TargetState.bMrtActive の別名。
};
