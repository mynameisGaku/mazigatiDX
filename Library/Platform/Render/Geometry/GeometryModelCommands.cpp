// SPDX-License-Identifier: MIT
#include "GeometryModelCommands.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/Render/Geometry/GeometryCommandHelpers.h"
#include "Library/Platform/Render/Geometry/GeometryQuadBuilder.h"
#include "Library/Platform/Render/Geometry/GeometryShapeBuilder.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	using NGeometryCommand::Finite;
	using NGeometryCommand::ToMatrix;
}

CGeometryModelCommands::CGeometryModelCommands( FGeometryExecution& InRun, CGeometryVertexSubmitter& InSubmitter, CGeometryForwardMesh& InForward )
	: Run( InRun )
	, Submitter( InSubmitter )
	, Forward( InForward )
	, Textures( Run.Textures )
	, Models( Run.Models )
	, Buffers( Run.Buffers )
	, World( Run.World )
	, State( Run.State )
{
}

EGeometryCommandResult CGeometryModelCommands::Execute( const FDrawCommandView& View, int32 Alpha, bool bAlphaBlend )
{
	switch ( View.Type )
	{
	case EDrawCommand::Model3D:
	{
		if ( !Models || !View.CanRead<FDrawModelData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		return DrawModel( View.Read<FDrawModelData>(), Alpha, bAlphaBlend ); // 個体の配置と、静的モデルの登録番号。
	}
	// 次に描くモデルの、アニメーションの状態（DxLibのMV1SetAttachAnimTime / MV1SetAttachAnimBlendRate / MV1SetAttachAnimBlendRateToFrame）。
	case EDrawCommand::ModelAnim:
	{
		if ( !View.CanRead<FDrawModelAnimData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawModelAnimData>();
		if ( Models )
		{
			Models->SetStaticModelAnimTime( Data.Handle, Data.AttachIndex, Data.Time );
			Models->SetStaticModelAnimRate( Data.Handle, Data.AttachIndex, Data.Rate );
		}
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::ModelAnimFrame:
	{
		if ( !View.CanRead<FDrawModelAnimFrameData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawModelAnimFrameData>();
		if ( Models && Data.Handle >= 0 && Data.AttachIndex >= 0 && Data.Frame >= 0 && NMath::IsFinite( Data.Rate ) )
		{
			Models->SetStaticModelAnimFrameRate( Data.Handle, Data.AttachIndex, Data.Frame, NMath::Clamp( Data.Rate, 0.0f, 1.0f ), Data.bIncludeChildren != 0 );
		}
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::ModelFramePose3D:
	{
		// 描く間だけ骨を上書きして、描いたあとに元へ戻す（DxLib 版の ExecuteModelFramePose と同じ）。
		FDrawModelFramePoseData Header;
		if ( !Models || !NDrawModelFramePose::IsValidCommand( View, Header ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		FDrawModelData Draw;
		Draw.Handle = Header.Handle;
		FMemory::Memcpy( Draw.M, Header.M, sizeof( Draw.M ) );
		Draw.AnimIndex = Header.AnimIndex;
		Draw.AnimTime = Header.AnimTime;
		if ( Header.NumOverrides == 0 )
		{
			return DrawModel( Draw, Alpha, bAlphaBlend );
		}
		IModelBackend* Backend = Models->GetStaticModelBackend();
		if ( !Backend )
		{
			return EGeometryCommandResult::Rejected;
		}
		TArray<FDrawModelFrameOverrideData> Overrides;
		for ( int32 Index = 0; Index < Header.NumOverrides; ++Index )
		{
			FDrawModelFrameOverrideData Override;
			if ( !NDrawModelFramePose::ReadOverride( View, Index, Override ) )
			{
				return EGeometryCommandResult::Rejected;
			}
			Overrides.Add( Override );
		}
		FModelFrameOverrideScope Scope( *Backend, Header.Handle, Overrides.GetData(), Overrides.Num() );
		if ( !Scope.IsApplied() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const EGeometryCommandResult Result = DrawModel( Draw, Alpha, bAlphaBlend );
		Scope.Restore();
		return Result;
	}
	case EDrawCommand::Mesh3D:
	{
		if ( !Buffers || !View.CanRead<FDrawMeshData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawMeshData>(); // 固定したメッシュの描画範囲。
		FGeometryVertexBufferView Vertices; // ローカル頂点。
		FGeometryIndexBufferView Indices; // 三角形の参照順。
		if ( !Finite( Data.M ) || Data.NumVertices < 3 || Data.StartIndex < 0 || Data.NumIndices <= 0 || Data.NumIndices > 65536 || Data.NumIndices % 3 != 0
			|| !Buffers->GetVertexBufferView( Data.VertexBuffer, Vertices ) || !Vertices.Vertices || Data.NumVertices > Vertices.NumVertices
			|| !Buffers->GetIndexBufferView( Data.IndexBuffer, Indices ) || !Indices.Indices || static_cast<int64>( Data.StartIndex ) + Data.NumIndices > Indices.NumIndices )
		{
			return EGeometryCommandResult::Rejected;
		}
		TArray<FDrawVertex3D> Ordered; // 全indexを検査してから記録する。
		for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
		{
			const uint32 VertexIndex = Indices.Indices[Data.StartIndex + Index]; // この面が参照する頂点。
			if ( VertexIndex >= static_cast<uint32>( Data.NumVertices ) )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto& Vertex = Vertices.Vertices[VertexIndex]; // GPUへ渡す前にも数値を検査する。
			if ( !Finite( Vertex.Position ) || !Finite( Vertex.Normal ) || !NMath::IsFinite( Vertex.U ) || !NMath::IsFinite( Vertex.V ) )
			{
				return EGeometryCommandResult::Rejected;
			}
		}
		const FMatrix4x4 OldWorld = World; // この1命令の後で必ず復元する。
		const EGeometryCulling OldCulling = State.Culling; // 両面指定はこのメッシュに限る。
		FDrawMatrixData Matrix; // 同じ12要素の形式。
		FMemory::Memcpy( Matrix.M, Data.M, sizeof( Data.M ) );
		World = ToMatrix( Matrix );
		if ( View.Has( FDrawFlag::TwoSided ) )
		{
			State.Culling = EGeometryCulling::None;
		}
		// Mesh3Dは既存の契約に合わせBrightを重ねず、頂点色を使う。
		uint64 MeshKey = 0, MeshVersion = 0; // GPU側で共有する頂点と添字の組。
		FStaticModelDrawSection Section = { Data.StartIndex, Data.NumIndices, Data.Texture }; // 指定された範囲だけ描く。
		FStaticModelDrawView Mesh = { Vertices.Vertices, Data.NumVertices, Indices.Indices, Indices.NumIndices, &Section, 1 };
		bool bForward = false; // GPUが受け取らなければ一度だけCPUで描く。
		if ( Buffers->GetMeshCacheIdentity( Data.VertexBuffer, Data.IndexBuffer, MeshKey, MeshVersion ) )
		{
			Mesh.MeshKey = MeshKey;
			Mesh.MeshVersion = MeshVersion;
			bForward = Forward.CanDrawForward( Mesh ) && Forward.DrawMeshForward( Mesh, Alpha, bAlphaBlend, false ) == 1;
		}
		if ( !bForward )
		{
			Ordered.Reserve( Data.NumIndices );
			for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
			{
				Ordered.Add( Vertices.Vertices[Indices.Indices[Data.StartIndex + Index]] );
			}
		}
		const bool bResult = bForward || Submitter.Submit( Ordered, EGeometryTopology::Triangles, FDrawRgb(), Alpha, bAlphaBlend, Data.Texture, true, false ); // 失敗時も状態を戻す。
		World = OldWorld;
		State.Culling = OldCulling;
		return bResult ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	default:
		return EGeometryCommandResult::NotHandled;
	}
}

EGeometryCommandResult CGeometryModelCommands::DrawModel( const FDrawModelData& Data, int32 Alpha, bool bAlphaBlend )
{
	FStaticModelDrawView Model; // 同期実行中だけ借りる不変形状。
	const bool bFiniteMatrix = Finite( Data.M );
	if ( bFiniteMatrix )
	{
		if ( Data.AnimIndex >= 0 )
		{
			Models->SetStaticModelAnimTime( Data.Handle, Data.AnimIndex, Data.AnimTime ); // DxLibと同じく、付けていなくても、そのまま描く。
		}
		Models->SetStaticModelPlacement( Data.Handle, Data.M );
	}
	if ( !bFiniteMatrix || !Models->GetStaticModelDrawView( Data.Handle, Model ) ) { return EGeometryCommandResult::Rejected; }
	const FMatrix4x4 OldWorld = World; // 従来命令では個体の配置を使い、後続へ漏らさない。
	FDrawMatrixData Matrix; // 個体の配置の12要素を読む。
	FMemory::Memcpy( Matrix.M, Data.M, sizeof( Data.M ) );
	World = ToMatrix( Matrix );
	const EGeometryCommandResult Result = DrawModelView( Model, Alpha, bAlphaBlend ); // 固定姿勢と同じ検査と描画経路を使う。
	World = OldWorld;
	return Result;
}

EGeometryCommandResult CGeometryModelCommands::DrawModelView( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend )
{
	if ( Model.NumIndices == 0 && Model.NumSections == 0 )
	{
		return EGeometryCommandResult::Executed; // 描くものが無い（形の無いモデル・すべて非表示）。
	}
	if ( !Model.Vertices || !Model.Indices || !Model.Sections
		|| Model.NumVertices <= 0 || Model.NumVertices > 786432 || Model.NumIndices <= 0 || Model.NumIndices > 786432 || Model.NumSections <= 0 || Model.NumSections > 4096
		|| !NMath::IsFinite( Model.Opacity ) || Model.Opacity < 0 || Model.Opacity > 1 )
	{
		return EGeometryCommandResult::Rejected;
	}
	int32 ExpectedIndex = 0; // すべての範囲を描く前に検査する。
	for ( int32 Index = 0; Index < Model.NumSections; ++Index )
	{
		const auto& Section = Model.Sections[Index]; // 連続した材質範囲。
		if ( Section.FirstIndex != ExpectedIndex || Section.NumIndices <= 0 || Section.NumIndices % 3 != 0 || static_cast<int64>( Section.FirstIndex ) + Section.NumIndices > Model.NumIndices )
		{
			return EGeometryCommandResult::Rejected;
		}
		if ( Section.Texture >= 0 )
		{
			FTextureDrawView Image; // 後の材質の画像が無い場合も先に失敗する。
			if ( !Textures || !Textures->GetTextureDrawView( Section.Texture, Image ) || Image.Binding == 0 || Image.Width <= 0 || Image.Height <= 0 )
			{
				return EGeometryCommandResult::Rejected;
			}
		}
		ExpectedIndex += Section.NumIndices;
	}
	if ( ExpectedIndex != Model.NumIndices )
	{
		return EGeometryCommandResult::Rejected;
	}
	for ( int32 Index = 0; Index < Model.NumVertices; ++Index )
	{
		const auto& Vertex = Model.Vertices[Index]; // 数値の検査も描画より前に済ませる。
		if ( !Finite( Vertex.Position ) || !NMath::IsFinite( Vertex.U ) || !NMath::IsFinite( Vertex.V ) )
		{
			return EGeometryCommandResult::Rejected;
		}
	}
	for ( int32 Index = 0; Index < Model.NumIndices; ++Index )
	{
		if ( Model.Indices[Index] >= static_cast<uint32>( Model.NumVertices ) )
		{
			return EGeometryCommandResult::Rejected;
		}
	}
	if ( Model.Opacity == 0 )
	{
		return EGeometryCommandResult::Executed;
	}
	const int32 ModelAlpha = NMath::Clamp( static_cast<int32>( Alpha * Model.Opacity + 0.5f ), 0, 255 ); // 個体の不透明度を一度だけ掛ける。
	const int32 NumForward = Forward.CanDrawForward( Model ) ? Forward.DrawMeshForward( Model, ModelAlpha, bAlphaBlend ) : 0; // GPU が変換と光を計算できる範囲は、共有のメッシュで描く。
	bool bSuccess = true; // GPU作成失敗時も配置は復元する。
	for ( int32 SectionIndex = NumForward; SectionIndex < Model.NumSections && bSuccess; ++SectionIndex )
	{
		const auto& Section = Model.Sections[SectionIndex]; // 三角形順を保つ範囲。
		for ( int32 Offset = 0; Offset < Section.NumIndices && bSuccess; )
		{
			const int32 Count = NMath::Min( 65535, Section.NumIndices - Offset ); // 三角形単位でGPU上限以内へ分ける。
			TArray<FDrawVertex3D> Ordered; // 今回の分割だけを所有する。
			Ordered.Reserve( Count );
			bool bUseAlpha = bAlphaBlend || ModelAlpha < 255 || Section.Texture >= 0; // 画像の透明画素も合成する。
			for ( int32 Index = 0; Index < Count; ++Index )
			{
				const auto& Vertex = Model.Vertices[Model.Indices[Section.FirstIndex + Offset + Index]]; // 検査済みの参照。
				Ordered.Add( Vertex );
				bUseAlpha = bUseAlpha || Vertex.Diffuse[3] < 255;
			}
			bSuccess = Submitter.Submit( Ordered, EGeometryTopology::Triangles, FDrawRgb(), ModelAlpha, bUseAlpha, Section.Texture, true, false );
			Offset += Count;
		}
	}
	return bSuccess ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
}
