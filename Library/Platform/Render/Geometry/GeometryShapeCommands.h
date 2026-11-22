// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Geometry/GeometryExecution.h"
#include "Library/Platform/Render/Geometry/GeometryVertexSubmitter.h"

// 3Dの図形（点・線・円錐・三角形・立方体・球・カプセル・四角形・ビルボード・リボン・点列・GPUバッファの点列）の命令を、頂点にして、頂点の提出役へ渡す。
class CGeometryShapeCommands
{
public:
	CGeometryShapeCommands( FGeometryExecution& InRun, CGeometryVertexSubmitter& InSubmitter );
	// 命令が、この役割のものなら実行する。ほかの命令なら NotHandled。
	EGeometryCommandResult Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );
	// ShaderQuad3Dの四角形を、クリップ座標つきの頂点（三角形のリスト）にする。カメラ・ワールド行列・向きの決め方は、Quad3Dと同じ。失敗はfalse（Outを保つ）。
	bool BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out );

private:
	FGeometryExecution& Run; // このSpanだけの3Dの状態と、借りた実行先。
	CGeometryVertexSubmitter& Submitter; // 連携する役割。
	ITextureDrawSource*& Textures; // Run.Textures の別名。
	IGeometryBufferSource*& Buffers; // Run.Buffers の別名。
	IRenderGeometry3D*& Geometry; // Run.Geometry の別名。
	FMatrix4x4& World; // Run.World の別名。
	FCamera& Camera; // Run.Camera の別名。
	FGeometryDrawState& State; // Run.State の別名。
};
