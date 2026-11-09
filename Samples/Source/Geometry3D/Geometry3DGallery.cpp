// SPDX-License-Identifier: MIT
#include "Geometry3DGallery.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Camera/Camera.h"

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
	for ( int32 Index = -5; Index <= 5; ++Index )
	{
		const float32 Position = static_cast<float32>( Index ); // 地面の格子の位置。
		Draw.Line3D( FVector3( Position, -1.2f, -5 ), FVector3( Position, -1.2f, 5 ), FColor32( 80, 80, 80 ) );
		Draw.Line3D( FVector3( -5, -1.2f, Position ), FVector3( 5, -1.2f, Position ), FColor32( 80, 80, 80 ) );
	}
	Draw.PopCamera();
	Draw.Text( 20, 80, "Camera / world transform / depth / vertex color", FColor32( 255, 255, 255 ) );
}
