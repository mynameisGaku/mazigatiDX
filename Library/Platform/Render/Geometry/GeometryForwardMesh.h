// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Geometry/GeometryExecution.h"

// GPU が変換と光を計算する経路（共有のメッシュを GPU に置いて描く）。影・フォグ・画素ごとの光などは、この経路では描けない。
class CGeometryForwardMesh
{
public:
	explicit CGeometryForwardMesh( FGeometryExecution& InRun );
	// 光・材質・カメラの定数（b6 と同じ並び。FForwardMeshDraw::NumLightFloats 個）を、いまのライトの状態から作る。
	void FillLightConstants( float32* Constants, FDrawRgb Bright ) const;
	// 共有のメッシュを GPU で描けるか。
	bool CanDrawForward( const FStaticModelDrawView& Model ) const;
	// 頂点と添字を材質範囲ごとにGPUへ渡す。bTransientなら登録を省き、Bright・bLitは呼出側の規約を使う。
	// 描けた範囲の数を返す。描けなかった範囲から先は呼び出し側が描く。
	int32 DrawMeshForward( const FStaticModelDrawView& Model, int32 ModelAlpha, bool bAlphaBlend, bool bInferAlpha = true, FDrawRgb Bright = FDrawRgb(), bool bLit = true, bool bTransient = false );

private:
	FGeometryExecution& Run; // このSpanだけの3Dの状態と、借りた実行先。
	IRenderGeometry3D*& Geometry; // Run.Geometry の別名。
	FMatrix4x4& World; // Run.World の別名。
	FCamera& Camera; // Run.Camera の別名。
	FGeometryDrawState& State; // Run.State の別名。
	CVertexLighting& Lighting; // Run.Lighting の別名。
	FDrawFogData& Fog; // Run.Fog の別名。
	FDrawFogData& HeightFog; // Run.HeightFog の別名。
	FDrawTextureTransformData& TextureTransform; // Run.TextureTransform の別名。
	IShaderDrawSource*& ShaderSource; // Run.ShaderSource の別名。
	bool& bUseForwardPath; // Run.bUseForwardPath の別名。
	bool& bShadowCasting; // Run.bShadowCasting の別名。
	int32& ReceiverSlot; // Run.ReceiverSlot の別名。
};
