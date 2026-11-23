// SPDX-License-Identifier: MIT
#include "ModelPoseSnapshot.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/Model/Model.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"

FModelPoseSnapshot FModelPoseSnapshot::Capture( const CModelInstance& Instance )
{
	if ( !IsInGameThread() ) { return {}; }
	IStaticModelSource* Source = GetAppSubsystem<IStaticModelSource>(); // 既存のモデル描画窓口へ問い合わせる。
	return Source ? Capture( *Source, Instance.GetHandle() ) : FModelPoseSnapshot();
}

FModelPoseSnapshot FModelPoseSnapshot::Capture( IStaticModelSource& Source, int32 Handle )
{
	FModelPoseSnapshot Result; // 有効な所有付きビューだけを公開する。
	if ( !IsInGameThread() || Handle < 0 ) { return Result; }
	FStaticModelDrawView View; // 個体の最新の評価済み姿勢を保持する。
	// GPU でスキニングできる個体は、基準の頂点（共有）と、骨と重み（共有）と、骨の行列（この姿勢のぶん。小さい）だけを保持する（変形済みの頂点のコピーを作らない）。
	const bool bSkinned = Source.GetStaticModelSkinnedDrawView( Handle, View ) && View.SkinVertices && View.BoneMatrices && View.NumBones > 0 && View.SkinOwner && View.SkinOwner.Get() == View.BoneMatrices;
	if ( !bSkinned )
	{
		View = {};
		if ( !Source.GetStaticModelDrawView( Handle, View ) ) { return Result; }
	}
	if ( View.NumIndices == 0 && View.NumSections == 0 )
	{
		View = {}; // 空の形状は画像や頂点を借りない。
	}
	else if ( !View.VertexOwner || View.VertexOwner.Get() != View.Vertices || !View.Indices || !View.Sections || View.NumVertices <= 0 || View.NumIndices <= 0 || View.NumSections <= 0 )
	{
		return Result;
	}
	Result.Data = MakeShared<FStaticModelDrawView>( MoveTemp( View ) );
	Result.Owner = &Source;
	return Result;
}

const FStaticModelDrawView* FModelPoseSnapshot::Resolve( const FDrawSpan& Span, const FDrawCommandView& Command, const IStaticModelSource* ExpectedSource )
{
	const FModelPoseSnapshot* Pose = Span.ModelPose; // 実行範囲に付いた唯一のモデル姿勢。
	if ( Command.Type != EDrawCommand::ModelPoseSnapshot3D || Command.Flags != 0 || Command.NumDataWords != 0 || !Pose || ( ExpectedSource && ExpectedSource != Pose->Owner ) ) { return nullptr; }
	return Pose->Data.Get();
}
