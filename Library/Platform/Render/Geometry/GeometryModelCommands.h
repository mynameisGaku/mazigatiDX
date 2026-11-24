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
	// GPU スキニングのビュー（SkinVertices がある）も渡せる（固定した姿勢を、骨の行列ごと保持して描く）。
	EGeometryCommandResult DrawModelView( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend );
	// 一時的な配置で変形済みの描画範囲を描き、もとの配置を戻す。
	EGeometryCommandResult DrawModelViewAt( const FStaticModelDrawView& Model, const float32 ( &Matrix )[12], int32 Alpha, bool bAlphaBlend );
	// 変形済みビューを描画前に検査する。空のビューは描画不要として受け付ける。
	bool ValidateModelView( const FStaticModelDrawView& Model ) const;
	// ValidateModelView で検査済みのビューの一範囲を指定配置で描く。ビューと画像の内容は検査後に変更しない。
	EGeometryCommandResult DrawValidatedModelSectionAt( const FStaticModelDrawView& Model, int32 SectionIndex, const float32 ( &Matrix )[12], int32 Alpha, bool bAlphaBlend );
	// GPU スキニングのビューを、CPU で変形した頂点にする（シェーダーと同じ式。重みの合計で割り、法線は正規化する）。GPU で描けないときの続きに使う。
	static void SkinViewOnCpu( const FStaticModelDrawView& Model, TArray<FDrawVertex3D>& OutVertices );

private:
	// GPU スキニングのビュー（骨の変形の前の頂点と骨の行列）を、GPU で描く。描けなかった範囲から先は、Handle の変形済みの頂点（Handle が負なら、ビューを CPU で変形した頂点）で、CPU の経路が描く。
	EGeometryCommandResult DrawSkinnedView( const FStaticModelDrawView& Model, int32 Handle, int32 Alpha, bool bAlphaBlend );
	// DrawModelView の本体。bTryForward が false なら、GPU の経路を試さず、FirstSection の範囲から、CPU の経路で描く。
	EGeometryCommandResult DrawModelViewFrom( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend, bool bTryForward, int32 FirstSection, bool bValidated = false );
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
