// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// FDrawQuad3DData の四角形の、向きと四隅（Quad3D と ShaderQuad3D が、同じ決め方を使う）
//   Camera / CameraY は、いまのカメラ（DxLib のカメラ）から向きを決める。Fixed は、Right / Up のまま。
//=============================================================================
namespace NDxLibQuad3D
{
	// 四角形の横（OutRight）と縦（OutUp）の向き
	inline void ComputeBasis( const FDrawQuad3DData& Data, VECTOR& OutRight, VECTOR& OutUp )
	{
		const VECTOR Position = VGet( Data.Pos[0], Data.Pos[1], Data.Pos[2] );
		OutRight = VGet( Data.Right[0], Data.Right[1], Data.Right[2] );
		OutUp = VGet( Data.Up[0], Data.Up[1], Data.Up[2] );
		switch ( static_cast<EDrawQuadOrient>( Data.Orient ) )
		{
		case EDrawQuadOrient::Fixed:
			break;
		case EDrawQuadOrient::CameraY:
		{
			// カメラ -> 物の向き（水平だけ）。真上・真下から見ているときは、カメラの横向きを使う
			const VECTOR CameraPosition = GetCameraPosition();
			VECTOR Forward = VGet( Position.x - CameraPosition.x, 0.0f, Position.z - CameraPosition.z );
			OutUp = VGet( 0.0f, 1.0f, 0.0f );
			if ( VSquareSize( Forward ) < 1.0e-8f )
			{
				OutRight = GetCameraRightVector();
			}
			else
			{
				Forward = VNorm( Forward );
				OutRight = VCross( OutUp, Forward );
			}
			break;
		}
		case EDrawQuadOrient::Camera:
		default:
		{
			const VECTOR CameraRight = GetCameraRightVector();
			const VECTOR CameraUp = GetCameraUpVector();
			const float Cos = NMath::Cos( Data.Angle );
			const float Sin = NMath::Sin( Data.Angle );
			// Angle だけ回す。正の向きは、オブジェクトを視線（Forward）の軸まわりに正の Roll で回したときと同じ（右向きが、上へ傾く）
			OutRight = VAdd( VScale( CameraRight, Cos ), VScale( CameraUp, Sin ) );
			OutUp = VAdd( VScale( CameraUp, Cos ), VScale( CameraRight, -Sin ) );
			break;
		}
		}
	}

	// 四隅（画像の左上・右上・左下・右下の順）。基準点（PivotX, PivotY）が Pos に来る
	inline void ComputeCorners( const FDrawQuad3DData& Data, const VECTOR& Right, const VECTOR& Up, VECTOR OutCorners[4] )
	{
		const VECTOR Position = VGet( Data.Pos[0], Data.Pos[1], Data.Pos[2] );
		const float Left = -Data.PivotX * Data.Width;
		const float RightEdge = ( 1.0f - Data.PivotX ) * Data.Width;
		const float Top = Data.PivotY * Data.Height;
		const float Bottom = -( 1.0f - Data.PivotY ) * Data.Height;
		const auto Corner = [&]( float A, float B ) { return VAdd( Position, VAdd( VScale( Right, A ), VScale( Up, B ) ) ); };
		OutCorners[0] = Corner( Left, Top );
		OutCorners[1] = Corner( RightEdge, Top );
		OutCorners[2] = Corner( Left, Bottom );
		OutCorners[3] = Corner( RightEdge, Bottom );
	}
}
