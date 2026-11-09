// SPDX-License-Identifier: MIT
#include "GeometryCommandExecutor.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// ワールド座標や行列に非有限値がないか。
	template<usize Count>
	bool Finite( const float32 ( &Values )[Count] )
	{
		for ( float32 Value : Values )
		{
			if ( !NMath::IsFinite( Value ) )
			{
				return false;
			}
		}
		return true;
	}
	// 共通の12要素形式からアフィン行列へ戻す。
	FMatrix4x4 ToMatrix( const FDrawMatrixData& Data )
	{
		FMatrix4x4 Result; // 最終列は(0,0,0,1)。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 3; ++Column )
			{
				Result.m[Row][Column] = Data.M[Row * 3 + Column];
			}
		}
		return Result;
	}
	// 図形の座標と色を共通頂点へ移す。
	FDrawVertex3D Vertex( const float32 ( &Position )[3], FDrawRgb Color )
	{
		FDrawVertex3D Result; // 法線と画像座標は使わない。
		FMemory::Memcpy( Result.Position, Position, sizeof( Position ) );
		Result.Diffuse[0] = Color.R;
		Result.Diffuse[1] = Color.G;
		Result.Diffuse[2] = Color.B;
		return Result;
	}
}

FGeometryCommandExecutor::FGeometryCommandExecutor( IRenderGeometry3D* InGeometry ) : Geometry( InGeometry )
{
}

bool FGeometryCommandExecutor::Submit( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	if ( !Geometry || Vertices.IsEmpty() || Vertices.Num() > 65536 || Geometry->GetWidth() <= 0 || Geometry->GetHeight() <= 0 || !Camera.IsValid() )
	{
		return false;
	}
	const FCameraViewport Viewport = { Geometry->GetWidth(), Geometry->GetHeight() }; // 同じ論理画面で射影する。
	const FMatrix4x4 Transform = World * Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ローカルからクリップへの変換。
	TArray<FGeometryVertex> Output; // 全頂点が正しい場合だけ送る所有コピー。
	Output.Reserve( Vertices.Num() );
	for ( const auto& Input : Vertices )
	{
		if ( !Finite( Input.Position ) )
		{
			return false;
		}
		FVector4 Clip = Transform.Transform( FVector4( Input.Position[0], Input.Position[1], Input.Position[2], 1 ) ); // 透視除算しない。
		Clip.x += 2 * Camera.ScreenOffset.x / Viewport.Width * Clip.w;
		Clip.y -= 2 * Camera.ScreenOffset.y / Viewport.Height * Clip.w;
		FGeometryVertex Value; // GPUが頂点色を透視補間する。
		Value.Position[0] = Clip.x;
		Value.Position[1] = Clip.y;
		Value.Position[2] = Clip.z;
		Value.Position[3] = Clip.w;
		if ( !Finite( Value.Position ) )
		{
			return false;
		}
		Value.Color = FColor32( static_cast<uint8>( NMath::Min( 255, Input.Diffuse[0] + Input.Specular[0] ) * Bright.R / 255 ),
			static_cast<uint8>( NMath::Min( 255, Input.Diffuse[1] + Input.Specular[1] ) * Bright.G / 255 ),
			static_cast<uint8>( NMath::Min( 255, Input.Diffuse[2] + Input.Specular[2] ) * Bright.B / 255 ), static_cast<uint8>( Input.Diffuse[3] * Alpha / 255 ) );
		Output.Add( Value );
	}
	State.Topology = Topology;
	State.bAlphaBlend = bAlphaBlend;
	return Geometry->DrawGeometry( Output.GetData(), Output.Num(), State );
}

FGeometryCommandExecutor::EResult FGeometryCommandExecutor::Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	switch ( View.Type )
	{
	case EDrawCommand::PushCamera:
	{
		if ( !View.CanRead<FDrawCameraData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCameraData>(); // 検査後の固定部。
		FCamera Next; // 検査完了まで現在のカメラを保つ。
		Next.Position = FVector3( Data.Position[0], Data.Position[1], Data.Position[2] );
		Next.Target = FVector3( Data.Target[0], Data.Target[1], Data.Target[2] );
		Next.Up = FVector3( Data.Up[0], Data.Up[1], Data.Up[2] );
		Next.Projection = Data.Orthographic ? ECameraProjection::Orthographic : ECameraProjection::Perspective;
		Next.FovY = Data.FovY;
		Next.OrthoHeight = Data.OrthoHeight;
		Next.Near = Data.Near;
		Next.Far = Data.Far;
		Next.HorizontalStretch = Data.HorizontalStretch;
		Next.ScreenOffset = FVector2( Data.ScreenOffsetX, Data.ScreenOffsetY );
		if ( !Next.IsValid() )
		{
			return EResult::Rejected;
		}
		CameraStack.Add( Camera );
		Camera = Next;
		return EResult::Executed;
	}
	case EDrawCommand::PopCamera:
	{
		if ( !View.CanRead<FDrawPopCameraData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopCameraData>(); // 検査後の固定部。
		(void)Data;
		if ( !CameraStack.IsEmpty() )
		{
			Camera = CameraStack.Pop();
		}
		return EResult::Executed;
	}
	case EDrawCommand::WorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EResult::Rejected;
		}
		World = ToMatrix( Data );
		return EResult::Executed;
	}
	case EDrawCommand::PushWorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EResult::Rejected;
		}
		WorldStack.Add( World );
		World = ToMatrix( Data ) * World;
		return EResult::Executed;
	}
	case EDrawCommand::PopWorldMatrix:
	{
		if ( !View.CanRead<FDrawPopWorldMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopWorldMatrixData>(); // 検査後の固定部。
		(void)Data;
		if ( !WorldStack.IsEmpty() )
		{
			World = WorldStack.Pop();
		}
		return EResult::Executed;
	}
	case EDrawCommand::Depth:
	{
		if ( !View.CanRead<FDrawDepthData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawDepthData>(); // 検査後の固定部。
		if ( Data.Compare < 0 || Data.Compare > 7 )
		{
			return EResult::Rejected;
		}
		State.bTestDepth = Data.TestDepth != 0;
		State.bWriteDepth = Data.WriteDepth != 0;
		State.Compare = static_cast<EGeometryCompare>( Data.Compare );
		State.DepthBias = Data.Bias;
		return EResult::Executed;
	}
	case EDrawCommand::Culling:
	{
		if ( !View.CanRead<FDrawCullingData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCullingData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > 2 )
		{
			return EResult::Rejected;
		}
		State.Culling = static_cast<EGeometryCulling>( Data.Mode );
		return EResult::Executed;
	}
	case EDrawCommand::ClearDepth:
	{
		if ( !View.CanRead<FDrawClearDepthData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawClearDepthData>(); // 検査後の固定部。
		(void)Data;
		if ( !Geometry )
		{
			return EResult::Rejected;
		}
		Geometry->ClearDepth();
		return EResult::Executed;
	}
	case EDrawCommand::UseLighting:
	{
		if ( !View.CanRead<FDrawUseLightingData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawUseLightingData>(); // 検査後の固定部。
		return Data.Enabled == 0 ? EResult::Executed : EResult::Rejected; // 現段階は頂点色だけ。照明を要求されたら描かない。
	}
	case EDrawCommand::Line3D:
	{
		if ( !View.CanRead<FDrawLine3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawLine3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // この命令の点列。
		Vertices.Add( Vertex( Data.P0, Data.Color ) );
		Vertices.Add( Vertex( Data.P1, Data.Color ) );
		return Submit( Vertices, EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Triangle3D:
	{
		if ( !View.CanRead<FDrawTriangle3DData>() )
		{
			return EResult::Rejected;
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
		return Submit( Vertices, View.Has( FDrawFlag::Fill ) ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Cube3D:
	{
		if ( !View.CanRead<FDrawCube3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCube3DData>(); // 検査後の固定部。
		if ( !Finite( Data.Min ) || !Finite( Data.Max ) )
		{
			return EResult::Rejected;
		}
		FDrawVertex3D Corners[8]; // XYZ各軸をbitで選んだ箱の角。
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			const float32 Point[3] = { Index & 1 ? Data.Max[0] : Data.Min[0], Index & 2 ? Data.Max[1] : Data.Min[1], Index & 4 ? Data.Max[2] : Data.Min[2] }; // 箱の角。
			Corners[Index] = Vertex( Point, Data.Diffuse );
		}
		const int32 Faces[] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,4,2, 2,4,6, 1,3,5, 3,7,5, 0,1,4, 1,5,4, 2,6,3, 3,6,7 }; // 外側を向く各面。
		const int32 Edges[] = { 0,1, 0,2, 0,4, 1,3, 1,5, 2,3, 2,6, 3,7, 4,5, 4,6, 5,7, 6,7 }; // 重複しない12辺。
		TArray<FDrawVertex3D> Vertices; // 展開済みの頂点列。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 面または辺。
		const int32* Indices = bFill ? Faces : Edges; // 同じ角を再利用する順序。
		for ( int32 Index = 0; Index < ( bFill ? 36 : 24 ); ++Index )
		{
			Vertices.Add( Corners[Indices[Index]] );
		}
		return Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Primitive3D:
	{
		if ( !View.CanRead<FDrawPrimitive3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitive3DData>(); // 検査後の固定部。
		if ( Data.Texture >= 0 || Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) || Data.NumVertices <= 0 || Data.NumIndices < 0
			|| FDrawPrimitive3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) != static_cast<size_t>( View.NumDataWords + 1 ) )
		{
			return EResult::Rejected;
		}
		const auto Type = static_cast<EDrawPrimitive>( Data.Type ); // 形式を検査したつなぎ方。
		const int32 Count = Data.NumIndices > 0 ? Data.NumIndices : Data.NumVertices; // 参照する要素数。
		if ( !NDrawPrimitive::IsValidCount( Type, Count ) || Count > 65536 )
		{
			return EResult::Rejected;
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
				return EResult::Rejected;
			}
			Ordered.Add( Source[VertexIndex] );
		}
		TArray<FDrawVertex3D> Output; // リストへ展開した点列。
		EGeometryTopology Topology = EGeometryTopology::Triangles; // 出力のつなぎ方。
		if ( Type == EDrawPrimitive::Points || Type == EDrawPrimitive::Lines || Type == EDrawPrimitive::Triangles )
		{
			Topology = Type == EDrawPrimitive::Points ? EGeometryTopology::Points : Type == EDrawPrimitive::Lines ? EGeometryTopology::Lines : EGeometryTopology::Triangles;
			Output = MoveTemp( Ordered );
		}
		else if ( Type == EDrawPrimitive::LineStrip )
		{
			Topology = EGeometryTopology::Lines;
			if ( Count > 32769 )
			{
				return EResult::Rejected;
			}
			for ( int32 Index = 1; Index < Count; ++Index )
			{
				Output.Add( Ordered[Index - 1] );
				Output.Add( Ordered[Index] );
			}
		}
		else
		{
			if ( Count > 21847 )
			{
				return EResult::Rejected;
			}
			for ( int32 Index = 2; Index < Count; ++Index )
			{
				Output.Add( Ordered[Type == EDrawPrimitive::TriangleFan ? 0 : Index - ( Index % 2 ? 1 : 2 )] );
				Output.Add( Ordered[Type == EDrawPrimitive::TriangleFan ? Index - 1 : Index - ( Index % 2 ? 2 : 1 )] );
				Output.Add( Ordered[Index] );
			}
		}
		return Submit( Output, Topology, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	default:
		return EResult::NotHandled;
	}
}
