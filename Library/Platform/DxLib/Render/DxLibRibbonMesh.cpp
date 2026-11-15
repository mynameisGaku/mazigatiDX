// SPDX-License-Identifier: MIT
#include "DxLibRibbonMesh.h"

#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"

bool NDxLibRibbonMesh::Build( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, bool bSplitTexture, FDxLibRibbonMesh& Mesh )
{
	// 呼び出しごとに出力を空へ戻し、成功時だけ今回の結果を有効にする。
	Mesh.Vertices.Reset();
	Mesh.Indices.Reset();
	// 帯の幅を向けるカメラ（DxLibのいまのカメラ）。
	FDrawRibbonCamera Camera;
	const VECTOR Position = GetCameraPosition();
	const VECTOR Right = GetCameraRightVector();
	const VECTOR Up = GetCameraUpVector();
	const VECTOR Front = GetCameraFrontVector();
	Camera.Position = FVector3( Position.x, Position.y, Position.z );
	Camera.Right = FVector3( Right.x, Right.y, Right.z );
	Camera.Up = FVector3( Up.x, Up.y, Up.z );
	Camera.Front = FVector3( Front.x, Front.y, Front.z );
	FDrawRibbonMesh Shared;
	if ( !NDrawRibbonMesh::Build( Data, Points, Camera, bSplitTexture, Shared ) )
	{
		return false;
	}
	Mesh.Vertices.SetNum( Shared.Vertices.Num() );
	for ( int32 Index = 0; Index < Shared.Vertices.Num(); ++Index )
	{
		const FDrawVertex3D& Source = Shared.Vertices[Index];
		VERTEX3D& Vertex = Mesh.Vertices[Index];
		Vertex = {};
		Vertex.pos = VGet( Source.Position[0], Source.Position[1], Source.Position[2] );
		Vertex.norm = VGet( Source.Normal[0], Source.Normal[1], Source.Normal[2] );
		Vertex.dif = GetColorU8( Source.Diffuse[0], Source.Diffuse[1], Source.Diffuse[2], Source.Diffuse[3] );
		Vertex.spc = GetColorU8( Source.Specular[0], Source.Specular[1], Source.Specular[2], Source.Specular[3] );
		Vertex.u = Source.U;
		Vertex.v = Source.V;
	}
	Mesh.Indices.SetNum( Shared.Indices.Num() );
	for ( int32 Index = 0; Index < Shared.Indices.Num(); ++Index )
	{
		Mesh.Indices[Index] = Shared.Indices[Index];
	}
	return true;
}
