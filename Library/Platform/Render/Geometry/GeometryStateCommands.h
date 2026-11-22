// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Geometry/GeometryExecution.h"

// 3Dの描画の状態を替える命令（カメラ・ワールド行列・深度・表裏・ライト・フォグ・画像の変換・補間・異方性）を実行する。
class CGeometryStateCommands
{
public:
	explicit CGeometryStateCommands( FGeometryExecution& InRun );
	// 命令が、この役割のものなら実行する。ほかの命令なら NotHandled。
	EGeometryCommandResult Execute( const FDrawCommandView& View );

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
	bool& bAnisotropicFilter; // Run.bAnisotropicFilter の別名。
	int32& AnisotropyLevel; // Run.AnisotropyLevel の別名。
	TArray<FMatrix4x4>& WorldStack; // Run.WorldStack の別名。
	TArray<FCamera>& CameraStack; // Run.CameraStack の別名。
};
