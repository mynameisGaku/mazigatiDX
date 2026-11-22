// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Geometry/GeometryExecution.h"
#include "Library/Platform/Render/Geometry/GeometryVertexSubmitter.h"
#include "Library/Platform/Render/Geometry/GeometryForwardMesh.h"

// 静的モデル（配置・アニメーション・固定姿勢・メッシュ）の命令を実行する。GPUで描ける共有のメッシュは、CGeometryForwardMesh で描き、それ以外は、CPU で頂点にして、提出役へ渡す。
class CGeometryModelCommands
{
public:
	CGeometryModelCommands( FGeometryExecution& InRun, CGeometryVertexSubmitter& InSubmitter, CGeometryForwardMesh& InForward );
	// 命令が、この役割のものなら実行する。ほかの命令なら NotHandled。
	EGeometryCommandResult Execute( const FDrawCommandView& View, int32 Alpha, bool bAlphaBlend );
	// 静的モデルを 1 つ描く（アニメーションの時間と配置を設定してから、いまの姿で描く）。Alpha・bAlphaBlend は、Execute へ渡された値。
	EGeometryCommandResult DrawModel( const FDrawModelData& Data, int32 Alpha, bool bAlphaBlend );
	// 現在の配置で固定姿勢を描く。モデル登録へのアクセスや更新は行わない。
	EGeometryCommandResult DrawModelView( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend );

private:
	FGeometryExecution& Run; // このSpanだけの3Dの状態と、借りた実行先。
	CGeometryVertexSubmitter& Submitter; // 連携する役割。
	CGeometryForwardMesh& Forward; // 連携する役割。
	ITextureDrawSource*& Textures; // Run.Textures の別名。
	IStaticModelSource*& Models; // Run.Models の別名。
	IGeometryBufferSource*& Buffers; // Run.Buffers の別名。
	FMatrix4x4& World; // Run.World の別名。
	FGeometryDrawState& State; // Run.State の別名。
};
