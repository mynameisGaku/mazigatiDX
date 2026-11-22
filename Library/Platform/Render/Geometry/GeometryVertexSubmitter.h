// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Geometry/GeometryExecution.h"
#include "Library/Platform/Render/Geometry/GeometryForwardMesh.h"

// 3Dの頂点を、ワールドからクリップ座標へ移し、光・フォグ・影を計算して、3Dの実行先へ渡す。GPUが変換と光を計算できる共有のメッシュは、CGeometryForwardMesh へ渡す。
class CGeometryVertexSubmitter
{
public:
	CGeometryVertexSubmitter( FGeometryExecution& InRun, CGeometryForwardMesh& InForward );
	// ワールド座標を同次クリップ座標にして記録する。全入力を検査してからGPUへ渡す。
	// bLit なら、頂点の法線と現在のライティングから、頂点ごとに色を計算する（光の計算を切っているときは、拡散色＋鏡面色）。
	bool Submit( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, int32 Texture = -1, bool bLit = false, bool bAllowTransient = true );
	// 両面・画像の透明度を一命令だけ適用する。カリングは失敗時も戻す。
	bool SubmitQuad( const TArray<FDrawVertex3D>& Vertices, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bLit = false );
	// indexを解決した点列をリストへ展開する。GPU・画像に依存しない。
	bool SubmitPrimitive( const TArray<FDrawVertex3D>& Ordered, EDrawPrimitive Type, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend );

private:
	// 影の絵を描いているとき、図形を光から見た深度として描く。影を落とさない図形（線・点・画像つき・半透明）は、何もせず成功。
	bool SubmitShadowCaster( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, int32 Alpha, int32 Texture );
	FGeometryExecution& Run; // このSpanだけの3Dの状態と、借りた実行先。
	CGeometryForwardMesh& Forward; // 連携する役割。
	ITextureDrawSource*& Textures; // Run.Textures の別名。
	IRenderGeometry3D*& Geometry; // Run.Geometry の別名。
	FMatrix4x4& World; // Run.World の別名。
	FCamera& Camera; // Run.Camera の別名。
	FGeometryDrawState& State; // Run.State の別名。
	CVertexLighting& Lighting; // Run.Lighting の別名。
	FDrawFogData& Fog; // Run.Fog の別名。
	FDrawFogData& HeightFog; // Run.HeightFog の別名。
	FDrawTextureTransformData& TextureTransform; // Run.TextureTransform の別名。
	IShaderDrawSource*& ShaderSource; // Run.ShaderSource の別名。
	bool& bShadowCasting; // Run.bShadowCasting の別名。
	FMatrix4x4& LightViewProjection; // Run.LightViewProjection の別名。
	int32 ( &ShadowSlotMaps )[3]; // Run.ShadowSlotMaps の別名。
	int32& ReceiverSlot; // Run.ReceiverSlot の別名。
};
