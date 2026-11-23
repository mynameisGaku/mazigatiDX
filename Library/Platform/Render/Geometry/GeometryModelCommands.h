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
	// GPU スキニングのビュー（骨の変形の前の頂点と骨の行列）を、GPU で描く。描けなかった範囲から先は、Handle の変形済みの頂点で、CPU の経路が描く。
	EGeometryCommandResult DrawSkinnedView( const FStaticModelDrawView& Model, int32 Handle, int32 Alpha, bool bAlphaBlend );
	// DrawModelView の本体。bTryForward が false なら、GPU の経路を試さず、FirstSection の範囲から、CPU の経路で描く。
	EGeometryCommandResult DrawModelViewFrom( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend, bool bTryForward, int32 FirstSection );
	// 材質範囲が、添字を隙間なく覆い、画像が使えるか。
	bool AreSectionsValid( const FStaticModelDrawView& Model ) const;

	FGeometryExecution& Run; // このSpanだけの3Dの状態と、借りた実行先。
	CGeometryVertexSubmitter& Submitter; // 連携する役割。
	CGeometryForwardMesh& Forward; // 連携する役割。
	ITextureDrawSource*& Textures; // Run.Textures の別名。
	IStaticModelSource*& Models; // Run.Models の別名。
	IGeometryBufferSource*& Buffers; // Run.Buffers の別名。
	FMatrix4x4& World; // Run.World の別名。
	FGeometryDrawState& State; // Run.State の別名。
};
