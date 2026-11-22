// SPDX-License-Identifier: MIT
#include "CanvasSceneCommands.h"
#include "Library/Framework/Render/Mesh/ModelPoseSnapshot.h"
#include "Library/Framework/Render/Mesh/ShaderMeshSnapshot.h"

CCanvasSceneCommands::CCanvasSceneCommands( FCanvasExecution& InRun, CCanvasTargetScopes& InTargetScopes )
	: Run( InRun )
	, TargetScopes( InTargetScopes )
{
}

void CCanvasSceneCommands::Execute( const FDrawCommandView& View )
{
	const bool bAlphaBlend = Run.State.bAlphaBlend; // 3Dへ渡す合成方式。
	if ( View.Type == EDrawCommand::SharedShaderMesh3D )
	{
		const int32 NumParts = Run.Span.ShaderMesh ? Run.Span.ShaderMesh->GetNumParts() : 0; // 一つの描画項目の中で全ての部分を再生する。
		if ( NumParts == 0 )
		{
			FCanvasExecution::Unsupported( View, "Invalid shared shader mesh" );
		}
		for ( int32 PartIndex = 0; PartIndex < NumParts; ++PartIndex )
		{
			FDrawCommandView Part = View; // 同じ材質状態を使う既存の三角形描画に解決する。
			if ( !FShaderMeshSnapshot::Resolve( Run.Span, Part, PartIndex ) || Run.ShaderExecutor.Execute( Part, bAlphaBlend ) != CShaderCommandExecutor::EResult::Executed )
			{
				FCanvasExecution::Unsupported( View, "A shared shader mesh part was rejected" );
				break;
			}
		}
		return;
	}
	if ( CShaderCommandExecutor::IsShaderCommand( View.Type ) )
	{
		// シェーダーの命令。描けなかった命令だけを飛ばして、続きを描く。
		if ( Run.ShaderExecutor.Execute( View, bAlphaBlend ) != CShaderCommandExecutor::EResult::Executed )
		{
			FCanvasExecution::Unsupported( View, "The shader command was rejected (no shader, an unsupported shader or a failed draw) and skipped" );
		}
		if ( View.Type == EDrawCommand::ShaderOutputTarget && !Run.ShaderExecutor.HasOutputTargets() )
		{
			TargetScopes.LeaveMrt(); // 出力を外したら、1つの出力のパスへ戻る（そのあとの描画・読み出しのため）。
		}
		return;
	}
	if ( CMaterialEffectCommandExecutor::IsMaterialEffectCommand( View.Type ) )
	{
		// 材質付きの3D描画。描けなかった命令だけを飛ばして、続きを描く。
		if ( Run.MaterialExecutor.Execute( View, Run.State.Bright, Run.State.Alpha, bAlphaBlend ) != CShaderCommandExecutor::EResult::Executed )
		{
			FCanvasExecution::Unsupported( View, "The material effect command was rejected (invalid input, an unsupported blend mode or a failed draw) and skipped" );
		}
		return;
	}
	if ( View.Type == EDrawCommand::ModelPoseSnapshot3D )
	{
		const FStaticModelDrawView* Pose = Run.Models ? FModelPoseSnapshot::Resolve( Run.Span, View, Run.Models ) : nullptr; // 同じ資源所有先の形状だけを描く。
		if ( !Pose || Run.Geometry.DrawModelView( *Pose, Run.State.Alpha, bAlphaBlend ) != CGeometryCommandExecutor::EResult::Executed )
		{
			FCanvasExecution::Unsupported( View, "Invalid or unsupported model pose snapshot" );
		}
		return;
	}
	const auto Result = Run.Geometry.Execute( View, Run.State.Bright, Run.State.Alpha, bAlphaBlend ); // 同じ順序で3Dへ渡す。
	if ( Result == CGeometryCommandExecutor::EResult::Executed )
	{
		return;
	}
	// 描けない命令は、その命令だけを飛ばす（前後の命令は描く）。
	FCanvasExecution::Unsupported( View, Result == CGeometryCommandExecutor::EResult::Rejected ? "The command was rejected (unsupported input or a failed draw) and skipped" : "The command is skipped" );
}
