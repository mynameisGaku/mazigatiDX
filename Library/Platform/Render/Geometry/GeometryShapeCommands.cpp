// SPDX-License-Identifier: MIT
#include "GeometryShapeCommands.h"
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
	using NGeometryCommand::Vertex;
	using NGeometryCommand::SetSpecular;

	// 登録したバッファの三角形を、GPU の共有メッシュとして描く、最小の添字の数（これより小さいと、頂点を展開して送るほうが、同じか速い）。
	constexpr int32 MinIndicesForSharedMesh = 512;
}

CGeometryShapeCommands::CGeometryShapeCommands( FGeometryExecution& InRun, CGeometryVertexSubmitter& InSubmitter )
	: Run( InRun )
	, Submitter( InSubmitter )
	, Textures( Run.Textures )
	, Buffers( Run.Buffers )
	, Geometry( Run.Geometry )
	, World( Run.World )
	, Camera( Run.Camera )
	, State( Run.State )
{
}

EGeometryCommandResult CGeometryShapeCommands::Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	// DxLib の立体図形の面は、アルファが 2 乗で効く。互換の見た目のときだけ、同じにする（255 は 255 のまま）。
	const int32 SolidAlpha = Run.bSolidShapeAlphaSquared ? ( Alpha * Alpha + 127 ) / 255 : Alpha;
	switch ( View.Type )
	{
	case EDrawCommand::Pixel3D:
	{
		if ( !View.CanRead<FDrawPixel3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawPixel3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // 点 1 つ。
		Vertices.Add( Vertex( Data.Pos, Data.Color ) );
		return Submitter.Submit( Vertices, EGeometryTopology::Points, Bright, Alpha, bAlphaBlend ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Cone3D:
	{
		if ( !View.CanRead<FDrawCone3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawCone3DData>(); // 頂点と、底面の中心。
		TArray<FDrawVertex3D> Vertices; // 面または線。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildCone( FVector3( Data.Top[0], Data.Top[1], Data.Top[2] ), FVector3( Data.Bottom[0], Data.Bottom[1], Data.Bottom[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submitter.Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, bFill ? SolidAlpha : Alpha, bAlphaBlend, -1, bFill ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Line3D:
	{
		if ( !View.CanRead<FDrawLine3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawLine3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // この命令の点列。
		Vertices.Add( Vertex( Data.P0, Data.Color ) );
		Vertices.Add( Vertex( Data.P1, Data.Color ) );
		return Submitter.Submit( Vertices, EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Triangle3D:
	{
		if ( !View.CanRead<FDrawTriangle3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawTriangle3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // この命令の点列。
		if ( View.Has( FDrawFlag::Fill ) )
		{
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
		}
		else
		{
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
		}
		return Submitter.Submit( Vertices, View.Has( FDrawFlag::Fill ) ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Cube3D:
	{
		if ( !View.CanRead<FDrawCube3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawCube3DData>(); // 検査後の固定部。
		if ( !Finite( Data.Min ) || !Finite( Data.Max ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		FDrawVertex3D Corners[8]; // XYZ各軸をbitで選んだ箱の角。
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			const float32 Point[3] = { Index & 1 ? Data.Max[0] : Data.Min[0], Index & 2 ? Data.Max[1] : Data.Min[1], Index & 4 ? Data.Max[2] : Data.Min[2] }; // 箱の角。
			Corners[Index] = Vertex( Point, Data.Diffuse );
		}
		const int32 Faces[] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,4,2, 2,4,6, 1,3,5, 3,7,5, 0,1,4, 1,5,4, 2,6,3, 3,6,7 }; // 外側を向く各面。
		const int32 Edges[] = { 0,1, 0,2, 0,4, 1,3, 1,5, 2,3, 2,6, 3,7, 4,5, 4,6, 5,7, 6,7 }; // 重複しない12辺。
		const float32 FaceNormals[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } }; // Faces の面の順（6頂点ずつ）の、外向きの法線。
		TArray<FDrawVertex3D> Vertices; // 展開済みの頂点列。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 面または辺。
		const int32* Indices = bFill ? Faces : Edges; // 同じ角を再利用する順序。
		Vertices.Reserve( bFill ? 36 : 24 ); // 1 回で確保する（足しながら広げると、何度も確保し直す）。
		for ( int32 Index = 0; Index < ( bFill ? 36 : 24 ); ++Index )
		{
			FDrawVertex3D Corner = Corners[Indices[Index]]; // 面ごとに法線が違うので、角を複写する。
			if ( bFill )
			{
				FMemory::Memcpy( Corner.Normal, FaceNormals[Index / 6], sizeof( Corner.Normal ) );
			}
			Vertices.Add( Corner );
		}
		SetSpecular( Vertices, Data.Specular );
		return Submitter.Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, bFill ? SolidAlpha : Alpha, bAlphaBlend, -1, bFill ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Sphere3D:
	{
		if ( !View.CanRead<FDrawSphere3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawSphere3DData>(); // ローカルの球。
		TArray<FDrawVertex3D> Vertices; // 面または緯線・経線。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildSphere( FVector3( Data.Center[0], Data.Center[1], Data.Center[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submitter.Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, bFill ? SolidAlpha : Alpha, bAlphaBlend, -1, bFill ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Capsule3D:
	{
		if ( !View.CanRead<FDrawCapsule3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawCapsule3DData>(); // 両端は半球の中心。
		TArray<FDrawVertex3D> Vertices; // GPUによらない展開結果。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildCapsule( FVector3( Data.P0[0], Data.P0[1], Data.P0[2] ), FVector3( Data.P1[0], Data.P1[1], Data.P1[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submitter.Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, bFill ? SolidAlpha : Alpha, bAlphaBlend, -1, bFill ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Quad3D:
	{
		if ( !View.CanRead<FDrawQuad3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawQuad3DData>(); // 向きと基準点を持つ画像。
		TArray<FDrawVertex3D> Vertices; // 表とUVを共通処理で作る。
		return CGeometryQuadBuilder::Build( Data, Camera, Vertices ) && Submitter.SubmitQuad( Vertices, Data.Handle, Bright, Alpha, true ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected; // Quad3Dは光を受ける。
	}
	case EDrawCommand::Billboard3D:
	case EDrawCommand::BillboardPivot3D:
	case EDrawCommand::Sprite3D:
	case EDrawCommand::SpriteStretch3D:
	case EDrawCommand::SpritePivot3D:
	{
		FDrawQuad3DData Quad; // 大きさ・基準点の異なる命令を同じ四角形へそろえる。
		bool bPixelSize = false, bPixelPivot = false; // 画素単位で受け取った項目。
		if ( View.Type == EDrawCommand::Billboard3D )
		{
			if ( !View.CanRead<FDrawBillboard3DData>() )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto Data = View.Read<FDrawBillboard3DData>(); // 中心を基準に横幅で指定する画像。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Size;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
		}
		else if ( View.Type == EDrawCommand::BillboardPivot3D )
		{
			if ( !View.CanRead<FDrawBillboardPivot3DData>() )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto Data = View.Read<FDrawBillboardPivot3DData>(); // 0〜1を基準とする画像。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Size;
			Quad.PivotX = Data.PivotX;
			Quad.PivotY = Data.PivotY;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
		}
		else if ( View.Type == EDrawCommand::Sprite3D )
		{
			if ( !View.CanRead<FDrawSprite3DData>() )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto Data = View.Read<FDrawSprite3DData>(); // 1画素あたりのワールド長。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Scale;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
			bPixelSize = true;
		}
		else if ( View.Type == EDrawCommand::SpriteStretch3D )
		{
			if ( !View.CanRead<FDrawSpriteStretch3DData>() )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto Data = View.Read<FDrawSpriteStretch3DData>(); // 横と縦で違う画素の大きさ。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Data.ScaleX;
			Quad.Height = Data.ScaleY;
			Quad.Handle = Data.Handle;
			bPixelSize = true;
		}
		else
		{
			if ( !View.CanRead<FDrawSpritePivot3DData>() )
			{
				return EGeometryCommandResult::Rejected;
			}
			const auto Data = View.Read<FDrawSpritePivot3DData>(); // 画素単位の基準点と拡大率。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Data.ScaleX;
			Quad.Height = Data.ScaleY;
			Quad.PivotX = Data.PivotX;
			Quad.PivotY = Data.PivotY;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
			bPixelSize = bPixelPivot = true;
		}
		FTextureDrawView Image; // 縦横比と画素寸法だけを使い、GPUの型は見ない。
		if ( Quad.Handle < 0 || !Textures || !Textures->GetTextureDrawView( Quad.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 || Image.Binding == 0 )
		{
			return EGeometryCommandResult::Rejected;
		}
		if ( bPixelSize )
		{
			Quad.Width *= Image.Width;
			Quad.Height *= Image.Height;
		}
		else
		{
			Quad.Height *= static_cast<float32>( Image.Height ) / Image.Width;
		}
		if ( bPixelPivot )
		{
			Quad.PivotX /= Image.Width;
			Quad.PivotY /= Image.Height;
		}
		if ( View.Has( FDrawFlag::FlipX ) )
		{
			Quad.U0 = 1;
			Quad.U1 = 0;
		}
		if ( View.Has( FDrawFlag::FlipY ) )
		{
			Quad.V0 = 1;
			Quad.V1 = 0;
		}
		TArray<FDrawVertex3D> Vertices; // 反転はUVだけに反映し、基準点を動かさない。
		return CGeometryQuadBuilder::Build( Quad, Camera, Vertices ) && Submitter.SubmitQuad( Vertices, Quad.Handle, Bright, Alpha ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::BillboardQuad3D:
	{
		if ( !View.CanRead<FDrawBillboardQuad3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawBillboardQuad3DData>(); // 四隅を自由に動かした画像。
		TArray<FDrawVertex3D> Vertices; // つぶれていない三角形だけを描く。
		return CGeometryQuadBuilder::BuildCorners( FVector3( Data.Pos[0], Data.Pos[1], Data.Pos[2] ), Data.Corner, Camera, Vertices ) && Submitter.SubmitQuad( Vertices, Data.Handle, Bright, Alpha ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Ribbon3D:
	{
		// 経路の点（ワールド座標）を、カメラへ面を向けた帯の三角形にして描く。ワールド行列は使わず、表裏の区別もしない。
		constexpr int32 DataWords = sizeof( FDrawRibbonData ) / 4; // 固定部の長さ（ワード）。
		constexpr int32 PointWords = sizeof( FDrawRibbonData::FPoint ) / 4; // 点1つの長さ（ワード）。
		if ( View.NumDataWords < DataWords || !Camera.IsValid() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawRibbonData>(); // 検査前の固定部。
		if ( Data.NumPoints < 2 || Data.NumPoints > FDrawRibbonData::MaxPoints || View.NumDataWords != DataWords + Data.NumPoints * PointWords )
		{
			return EGeometryCommandResult::Rejected;
		}
		TArray<FDrawRibbonData::FPoint> Points; // 借用メモリのalignmentに頼らず、所有コピーを作る。
		Points.SetNum( Data.NumPoints );
		FMemory::Memcpy( Points.GetData(), View.Data + DataWords, static_cast<usize>( Data.NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
		FDrawRibbonCamera RibbonCamera; // 帯の幅を向けるカメラ。
		RibbonCamera.Position = Camera.Position;
		RibbonCamera.Right = Camera.GetRight();
		RibbonCamera.Up = Camera.GetUpVector();
		RibbonCamera.Front = Camera.GetForward();
		FDrawRibbonMesh Mesh; // 画像の繰り返しは、整数のUの境界で分ける（DxLibの描画と同じ）。
		if ( !NDrawRibbonMesh::Build( Data, Points.GetData(), RibbonCamera, true, Mesh ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		TArray<FDrawVertex3D> Triangles; // 番号を解決した三角形のリスト。
		Triangles.Reserve( Mesh.Indices.Num() );
		for ( const uint16 Index : Mesh.Indices )
		{
			Triangles.Add( Mesh.Vertices[Index] );
		}
		const FMatrix4x4 OldWorld = World; // 成功・失敗のどちらでも元に戻す。
		const EGeometryCulling OldCulling = State.Culling;
		World = FMatrix4x4::Identity;
		State.Culling = EGeometryCulling::None;
		const bool bSuccess = Submitter.Submit( Triangles, EGeometryTopology::Triangles, Bright, bAlphaBlend ? Alpha : 255, true, Data.Texture, true ); // 上書きのときも、通常のアルファで描く。
		World = OldWorld;
		State.Culling = OldCulling;
		return bSuccess ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::Primitive3D:
	{
		if ( !View.CanRead<FDrawPrimitive3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitive3DData>(); // 検査後の固定部。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) || Data.NumVertices <= 0 || Data.NumIndices < 0
			|| FDrawPrimitive3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) != static_cast<size_t>( View.NumDataWords + 1 ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Type = static_cast<EDrawPrimitive>( Data.Type ); // 形式を検査したつなぎ方。
		const int32 Count = Data.NumIndices > 0 ? Data.NumIndices : Data.NumVertices; // 参照する要素数。
		if ( !NDrawPrimitive::IsValidCount( Type, Count ) || Count > 65536 )
		{
			return EGeometryCommandResult::Rejected;
		}
		TArray<FDrawVertex3D> Source; // 可変長の入力を所有コピーしてから調べる。
		Source.SetNum( Data.NumVertices );
		const auto* Bytes = reinterpret_cast<const uint8*>( View.Data ) + sizeof( Data ); // 頂点列の先頭。
		FMemory::Memcpy( Source.GetData(), Bytes, static_cast<usize>( Data.NumVertices ) * sizeof( FDrawVertex3D ) );
		const auto* Indices = Bytes + static_cast<usize>( Data.NumVertices ) * sizeof( FDrawVertex3D ); // index列の先頭。
		TArray<FDrawVertex3D> Ordered; // indexを解決した点列。
		Ordered.Reserve( Count );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			uint16 Value = 0; // 借用メモリのalignmentに頼らず読む。
			if ( Data.NumIndices > 0 )
			{
				FMemory::Memcpy( &Value, Indices + Index * sizeof( uint16 ), sizeof( Value ) );
			}
			const int32 VertexIndex = Data.NumIndices > 0 ? Value : Index; // 解決した参照先。
			if ( VertexIndex >= Source.Num() || !Finite( Source[VertexIndex].Position ) )
			{
				return EGeometryCommandResult::Rejected;
			}
			Ordered.Add( Source[VertexIndex] );
		}
		return Submitter.SubmitPrimitive( Ordered, Type, Data.Texture, Bright, Alpha, bAlphaBlend ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::PrimitiveBuffer3D:
	{
		if ( !Buffers || !View.CanRead<FDrawPrimitiveBuffer3DData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitiveBuffer3DData>(); // 記録時の登録番号と範囲。
		FGeometryVertexBufferView Vertices; // 同期の処理中だけ借用する。
		// 三角形のリストで、基準頂点も使わないなら、登録したバッファのまま、GPU の共有メッシュとして描ける（その数の上限は、大きい）。それ以外は、頂点を展開して描くので、65536 まで。
		const bool bSharedMeshCandidate = Data.IndexBuffer >= 0 && Data.BaseVertex == 0 && Data.Type == static_cast<int32>( EDrawPrimitive::Triangles ) && Data.Num % 3 == 0 && Data.Num >= MinIndicesForSharedMesh;
		const int32 MaxNum = bSharedMeshCandidate ? static_cast<int32>( NGraphicsPipelineLimits::MaxRegisteredIndices ) : 65536;
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) || Data.Start < 0 || Data.Num <= 0 || Data.Num > MaxNum
			|| !Buffers->GetVertexBufferView( Data.VertexBuffer, Vertices ) || !Vertices.Vertices || Data.NumBufferVertices != Vertices.NumVertices )
		{
			return EGeometryCommandResult::Rejected;
		}
		FGeometryIndexBufferView Indices; // indexありの場合だけ借りる。
		if ( Data.IndexBuffer >= 0 )
		{
			if ( !Buffers->GetIndexBufferView( Data.IndexBuffer, Indices ) || !Indices.Indices || Data.NumBufferIndices != Indices.NumIndices
				|| static_cast<int64>( Data.Start ) + Data.Num > Indices.NumIndices )
			{
				return EGeometryCommandResult::Rejected;
			}
		}
		else if ( Data.BaseVertex != 0 || Data.NumBufferIndices != 0 || static_cast<int64>( Data.Start ) + Data.Num > Vertices.NumVertices )
		{
			return EGeometryCommandResult::Rejected;
		}
		// 登録したバッファのまま、GPU の共有メッシュとして描く（頂点の展開・検査・送り直しをしない）。
		if ( bSharedMeshCandidate )
		{
			uint64 MeshKey = 0, MeshVersion = 0; // GPU 側で共有する頂点と添字の組。
			if ( Buffers->GetStableMeshCacheIdentity( Data.VertexBuffer, Data.IndexBuffer, MeshKey, MeshVersion ) )
			{
				if ( Submitter.SubmitCachedMesh( Vertices, Indices, MeshKey, MeshVersion, Data.Start, Data.Num, Bright, Alpha, bAlphaBlend, Data.Texture ) )
				{
					return EGeometryCommandResult::Executed;
				}
				if ( Data.Num > 65536 )
				{
					// 大きなメッシュが、GPU の共有メッシュで描けなかった（実行先が受け取らない・GPU が使えない）。次の経路は、毎フレーム頂点を展開して送るので、遅い。黙って遅くならないように、1 度だけ知らせる。
					NBackendSupport::ReportUnsupported( "DX12", "A registered mesh of more than 65536 indices on the GPU shared-mesh path", "the GPU path did not take it, so it is drawn by the slow path that expands the vertices every frame" );
				}
			}
		}
		// 頂点を展開して描く。GPU の共有メッシュで描けなかった大きな三角形のリスト（65536 を超える）は、頂点の上限以内の塊（65535 = 3 の倍数）へ分けて、続きを描く。
		const int32 ChunkSize = Data.Num > 65536 ? 65535 : Data.Num;
		for ( int32 ChunkStart = 0; ChunkStart < Data.Num; ChunkStart += ChunkSize )
		{
			const int32 ChunkCount = NMath::Min( ChunkSize, Data.Num - ChunkStart );
			TArray<FDrawVertex3D> Ordered; // 基準頂点を含めて参照を解決した列（この塊だけ）。
			Ordered.Reserve( ChunkCount );
			for ( int32 Index = ChunkStart; Index < ChunkStart + ChunkCount; ++Index )
			{
				const int64 VertexIndex = Data.IndexBuffer >= 0 ? static_cast<int64>( Indices.Indices[Data.Start + Index] ) + Data.BaseVertex : Data.Start + Index; // 加算は64bit。
				if ( VertexIndex < 0 || VertexIndex >= Vertices.NumVertices )
				{
					return EGeometryCommandResult::Rejected;
				}
				Ordered.Add( Vertices.Vertices[static_cast<int32>( VertexIndex )] );
			}
			if ( !Submitter.SubmitPrimitive( Ordered, static_cast<EDrawPrimitive>( Data.Type ), Data.Texture, Bright, Alpha, bAlphaBlend ) )
			{
				return EGeometryCommandResult::Rejected;
			}
		}
		return EGeometryCommandResult::Executed;
	}
	default:
		return EGeometryCommandResult::NotHandled;
	}
}

bool CGeometryShapeCommands::BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out )
{
	if ( !Geometry || Geometry->GetWidth() <= 0 || Geometry->GetHeight() <= 0 || !Camera.IsValid() )
	{
		return false;
	}
	TArray<FDrawVertex3D> Vertices; // 表とUVを、共通処理で作る。
	if ( !CGeometryQuadBuilder::Build( Data.Quad, Camera, Vertices ) )
	{
		return false;
	}
	const FCameraViewport Viewport = { Geometry->GetWidth(), Geometry->GetHeight() }; // 同じ論理画面で射影する。
	const FMatrix4x4 Transform = World * Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ローカルからクリップへの変換。
	TArray<FDrawShaderVertex3D> Result; // 全頂点が正しい場合だけ渡す。
	Result.Reserve( Vertices.Num() );
	for ( const FDrawVertex3D& Input : Vertices )
	{
		if ( !Finite( Input.Position ) || !NMath::IsFinite( Input.U ) || !NMath::IsFinite( Input.V ) )
		{
			return false;
		}
		FVector4 Clip = Transform.Transform( FVector4( Input.Position[0], Input.Position[1], Input.Position[2], 1 ) ); // 透視除算しない（wを渡す）。
		Clip.x += 2 * Camera.ScreenOffset.x / Viewport.Width * Clip.w;
		Clip.y -= 2 * Camera.ScreenOffset.y / Viewport.Height * Clip.w;
		FDrawShaderVertex3D Vertex; // 法線などは使わない（0）。
		FMemory::Memzero( &Vertex, sizeof( Vertex ) );
		FMemory::Memcpy( Vertex.Position, Input.Position, sizeof( Vertex.Position ) );
		Vertex.SubPosition[0] = Clip.x;
		Vertex.SubPosition[1] = Clip.y;
		Vertex.SubPosition[2] = Clip.z;
		Vertex.SubPosition[3] = Clip.w;
		FMemory::Memcpy( Vertex.Diffuse, Input.Diffuse, sizeof( Vertex.Diffuse ) );
		Vertex.U = Input.U;
		Vertex.V = Input.V;
		Vertex.SubU = Data.Extra[0]; // 四角形ごとの4つの数（ピクセルシェーダーのTEXCOORD1）。
		Vertex.SubV = Data.Extra[1];
		Vertex.Tangent[0] = Data.Extra[2];
		Vertex.Tangent[1] = Data.Extra[3];
		if ( !Finite( Vertex.SubPosition ) )
		{
			return false;
		}
		Result.Add( Vertex );
	}
	Out = MoveTemp( Result );
	return true;
}
