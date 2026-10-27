// SPDX-License-Identifier: MIT
#include "EffectMeshGeometry.h"


#include "Library/Core/Types/Utility.h"

namespace
{
	// XY面の頂点へ位置・法線・UVをまとめて設定する。
	FMeshVertex MakeVertex( float32 X, float32 Y, float32 U, float32 V )
	{
		// 描画に渡す固定頂点。
		FMeshVertex Vertex;
		Vertex.Position[0] = X;
		Vertex.Position[1] = Y;
		Vertex.Position[2] = 0.0f;
		Vertex.Normal[0] = 0.0f;
		Vertex.Normal[1] = 0.0f;
		Vertex.Normal[2] = 1.0f;
		Vertex.U = U;
		Vertex.V = V;
		return Vertex;
	}
}

bool NEffectMesh::BuildGeometry( const FEffectMeshDefinition& Definition, FEffectMeshGeometry& Out, FString* Error )
{
	if ( !Definition.IsValidSettings() || Definition.Kind == FEffectMeshDefinition::EKind::None )
	{
		if ( Error != nullptr ) *Error = "meshの種類または設定が無効です";
		return false;
	}
	// 出力を壊さず、完成後に置き換える作業用geometry。
	FEffectMeshGeometry Geometry;
	if ( Definition.Kind == FEffectMeshDefinition::EKind::Plane )
	{
		Geometry.Vertices.Add( MakeVertex( -0.5f, -0.5f, 0.0f, 0.0f ) );
		Geometry.Vertices.Add( MakeVertex( 0.5f, -0.5f, 1.0f, 0.0f ) );
		Geometry.Vertices.Add( MakeVertex( 0.5f, 0.5f, 1.0f, 1.0f ) );
		Geometry.Vertices.Add( MakeVertex( -0.5f, 0.5f, 0.0f, 1.0f ) );
		Geometry.Indices.Add( 0 ); Geometry.Indices.Add( 1 ); Geometry.Indices.Add( 2 );
		Geometry.Indices.Add( 0 ); Geometry.Indices.Add( 2 ); Geometry.Indices.Add( 3 );
	}
	else if ( Definition.Kind == FEffectMeshDefinition::EKind::Ring )
	{
		// 各区間の両端で内外二頂点を追加し、継ぎ目UVを重ねて閉じる。
		for ( int32 Segment = 0; Segment <= Definition.Segments; ++Segment )
		{
			if ( Segment == Definition.Segments )
			{
				// 継ぎ目の位置と法線は先頭から複写し、Uだけ1にする。
				FMeshVertex InnerSeam = Geometry.Vertices[0];
				FMeshVertex OuterSeam = Geometry.Vertices[1];
				InnerSeam.U = 1.0f;
				OuterSeam.U = 1.0f;
				Geometry.Vertices.Add( InnerSeam );
				Geometry.Vertices.Add( OuterSeam );
				continue;
			}
			// 継ぎ目を含む周方向の割合。
			const float32 U = static_cast<float32>( Segment ) / static_cast<float32>( Definition.Segments );
			// 同一点の角度を最初と最後で共有する。
			const float32 Angle = U * 6.2831853071795864769f;
			// 内外円の共通方向成分。
			const float32 Cosine = NMath::Cos( Angle );
			const float32 Sine = NMath::Sin( Angle );
			Geometry.Vertices.Add( MakeVertex( Cosine * 0.5f * Definition.InnerRadius, Sine * 0.5f * Definition.InnerRadius, U, 0.0f ) );
			Geometry.Vertices.Add( MakeVertex( Cosine * 0.5f, Sine * 0.5f, U, 1.0f ) );
		}
		for ( int32 Segment = 0; Segment < Definition.Segments; ++Segment )
		{
			// 現区間と次区間の内外頂点番号。
			const uint32 Inner = static_cast<uint32>( Segment * 2 );
			const uint32 Outer = Inner + 1;
			const uint32 NextInner = Inner + 2;
			const uint32 NextOuter = Inner + 3;
			Geometry.Indices.Add( Inner ); Geometry.Indices.Add( Outer ); Geometry.Indices.Add( NextOuter );
			Geometry.Indices.Add( Inner ); Geometry.Indices.Add( NextOuter ); Geometry.Indices.Add( NextInner );
		}
	}
	else
	{
		if ( Error != nullptr ) *Error = "meshの種類が未対応です";
		return false;
	}
	Out = MoveTemp( Geometry );
	if ( Error != nullptr ) Error->Empty();
	return true;
}
