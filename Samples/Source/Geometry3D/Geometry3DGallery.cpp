// SPDX-License-Identifier: MIT
#include "Geometry3DGallery.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Asset/AssetSubsystem.h"

void CGeometry3DGallery::OnSpawn()
{
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		FPixelImage Image( 32, 32, FColor32( 40, 90, 210 ) ); // 表示に必要な画素を起動時に固定する。
		for ( int32 Y = 0; Y < 32; ++Y )
		{
			for ( int32 X = 0; X < 32; ++X )
			{
				if ( ( X / 4 + Y / 4 ) % 2 == 0 )
				{
					Image.Set( X, Y, FColor32( 250, 220, 100 ) );
				}
			}
		}
		Texture = Assets->CreateDynamicTexture( "Geometry3D.Checker", Image );
	}
	FMeshVertex Points[4]; // 右側に置く四角形。
	FVertex3D BufferPoints[4]; // 汎用バッファにも同じ形を渡す。
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Points[Index].Position[0] = Index & 2 ? 4.0f : 2.0f;
		Points[Index].Position[1] = Index & 1 ? 1.0f : -1.0f;
		Points[Index].U = Index & 2 ? 1.0f : 0.0f;
		Points[Index].V = Index & 1 ? 0.0f : 1.0f;
		BufferPoints[Index] = FVertex3D::Make( FVector3( Points[Index].Position[0], Points[Index].Position[1], 0 ), FVector3( 0, 0, -1 ), FColor32( 255, 255, 255 ), Points[Index].U, Points[Index].V );
	}
	const uint32 MeshIndices[6] = { 0, 1, 2, 2, 1, 3 }; // メッシュ用の32bitの面。
	const uint16 BufferIndices[6] = { 0, 1, 2, 2, 1, 3 }; // 汎用バッファ用の16bitの面。
	Mesh = CMeshBuffer::Create( TArrayView<const FMeshVertex>( Points, 4 ), TArrayView<const uint32>( MeshIndices, 6 ) );
	Vertices = CVertexBuffer::Create( TArrayView<const FVertex3D>( BufferPoints, 4 ) );
	Indices = CIndexBuffer::Create( TArrayView<const uint16>( BufferIndices, 6 ) );
}

void CGeometry3DGallery::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CGeometry3DGallery::Draw( FDrawContext& Draw )
{
	const FCamera Camera = FCamera::MakePerspective( FVector3( 5, 4, -9 ), FVector3( 0, 0, 0 ) ); // 箱全体を見るカメラ。
	if ( !Draw.PushCamera( Camera ) )
	{
		return;
	}
	Draw.UseLighting( false );
	Draw.WorldMatrix( FMatrix4x4::RotateY( Time * 0.5f ) );
	Draw.Cube3D( FVector3( -1, -1, -1 ), FVector3( 1, 1, 1 ), FColor32( 50, 160, 240 ) );
	Draw.Cube3D( FVector3( -1.03f, -1.03f, -1.03f ), FVector3( 1.03f, 1.03f, 1.03f ), FColor32( 255, 255, 255 ), FColor32( 0, 0, 0 ), false );
	Draw.WorldMatrix( FMatrix4x4() );
	Draw.Triangle3D( FVector3( -4, -1, 0 ), FVector3( -3, 1, 0 ), FVector3( -2, -1, 0 ), FColor32( 240, 80, 60 ) );
	// 奥の面を後から描いても、手前の三角形を覆わない。
	Draw.Triangle3D( FVector3( -4.3f, -1.3f, 1 ), FVector3( -3, 1.3f, 1 ), FVector3( -1.7f, -1.3f, 1 ), FColor32( 80, 190, 80 ) );
	Draw.TextureAddress( EDrawTextureAddress::Clamp, EDrawTextureAddress::Clamp );
	Draw.Filter( EDrawFilter::Nearest );
	if ( Mesh.IsValid() && Texture.IsValid() )
	{
		Draw.Mesh3D( Mesh->GetVertexBuffer(), Mesh->GetIndexBuffer(), Mesh->GetNumVertices(), 0, Mesh->GetNumIndices(), Texture->GetHandle(), FMatrix4x4(), true );
	}
	Draw.Filter( EDrawFilter::Bilinear );
	Draw.WorldMatrix( FMatrix4x4::Translate( FVector3( 0, 2.5f, 0 ) ) );
	Draw.PrimitiveIndexedBuffer3D( Vertices, Indices, EDrawPrimitive::Triangles, Texture );
	Draw.WorldMatrix( FMatrix4x4() );
	for ( int32 Index = -5; Index <= 5; ++Index )
	{
		const float32 Position = static_cast<float32>( Index ); // 地面の格子の位置。
		Draw.Line3D( FVector3( Position, -1.2f, -5 ), FVector3( Position, -1.2f, 5 ), FColor32( 80, 80, 80 ) );
		Draw.Line3D( FVector3( -5, -1.2f, Position ), FVector3( 5, -1.2f, Position ), FColor32( 80, 80, 80 ) );
	}
	Draw.PopCamera();
	Draw.Text( 20, 80, "Camera / transform / depth / textured mesh and buffers", FColor32( 255, 255, 255 ) );
}
