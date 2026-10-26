#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/Editor/EditorCollider.h"
#include "Library/Framework/World/Editor/EditorGeometry.h"
#include "Library/Framework/World/Editor/EditorGizmo.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "WorldEditorSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldEditorTest;

//=============================================================================
// ワールドエディタの計算（光線と図形・カメラ・図形の作り方・ピッキング・ギズモ）の確認
//   DxLib も ImGui も使わない。期待する値は、手で求めた数（幾何）か、別の経路で求めた値（往復・不変量）。
//=============================================================================
namespace
{
	FEditorRay MakeRay( const FVector3& Origin, const FVector3& Direction )
	{
		FEditorRay Ray;
		Ray.Origin = Origin;
		Ray.Direction = Direction.Normalized();
		return Ray;
	}

	//=========================================================================
	// 光線と図形
	//=========================================================================
	void TestGeometry( NAssetTest::FChecker& C )
	{
		C.Group = "editor-geometry";
		float32 T = 0.0f;
		FVector3 Normal;

		// 箱
		const FVector3 Lo( -1.0f, -1.0f, -1.0f ), Hi( 1.0f, 1.0f, 1.0f );
		C.Check( NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 0, -5 ), FVector3::Forward ), Lo, Hi, T, &Normal ) && NMath::Abs( T - 4.0f ) < 1e-4f && Near3( Normal, FVector3( 0, 0, -1 ) ), "box: front face at T=4, normal -Z (T=%.4f)", T );
		C.Check( NWorldEditor::RayVsBox( MakeRay( FVector3( 5, 0, 0 ), FVector3::Left ), Lo, Hi, T, &Normal ) && NMath::Abs( T - 4.0f ) < 1e-4f && Near3( Normal, FVector3( 1, 0, 0 ) ), "box: from +X, normal +X" );
		C.Check( NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 5, 0 ), FVector3::Down ), Lo, Hi, T, &Normal ) && NMath::Abs( T - 4.0f ) < 1e-4f && Near3( Normal, FVector3( 0, 1, 0 ) ), "box: from above, normal +Y" );
		C.Check( !NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 3, -5 ), FVector3::Forward ), Lo, Hi, T ), "box: a ray passing above misses" );
		C.Check( !NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 2, -5 ), FVector3::Forward ), Lo, Hi, T ), "box: a ray parallel to a slab and outside it misses" );
		C.Check( !NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 0, 5 ), FVector3::Forward ), Lo, Hi, T ), "box: a box behind the ray is not hit" );
		C.Check( !NWorldEditor::RayVsBox( MakeRay( FVector3( 0, 0, 0 ), FVector3::Forward ), Lo, Hi, T ), "box: a ray starting inside does not hit" );
		C.Check( NWorldEditor::RayVsBox( MakeRay( FVector3( -3, -3, -3 ), FVector3( 1, 1, 1 ) ), Lo, Hi, T ) && NMath::Abs( T - 2.0f * std::sqrt( 3.0f ) ) < 1e-3f, "box: a diagonal ray reaches the corner at 2*sqrt(3) (T=%.4f)", T );

		// 回転した箱: 長い辺（X 方向に半分 2）を、Y 軸まわりに 90 度回すと Z 方向になる
		const FQuaternion Yaw90 = FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI );
		C.Check( NWorldEditor::RayVsOrientedBox( MakeRay( FVector3( 5, 0, 0 ), FVector3::Left ), FVector3::Zero, Yaw90, FVector3( 2, 1, 1 ), T ) && NMath::Abs( T - 4.0f ) < 1e-3f, "oriented box: after a 90 degree yaw the X extent is 1, so the face from +X is at 4 (T=%.4f)", T );
		C.Check( NWorldEditor::RayVsOrientedBox( MakeRay( FVector3( 0, 0, -5 ), FVector3::Forward ), FVector3::Zero, Yaw90, FVector3( 2, 1, 1 ), T, &Normal ) && NMath::Abs( T - 3.0f ) < 1e-3f && Near3( Normal, FVector3( 0, 0, -1 ) ), "oriented box: and the Z extent is 2 (T=%.4f), the normal is in world space", T );
		C.Check( NWorldEditor::RayVsOrientedBox( MakeRay( FVector3( 10, 3, 0 ), FVector3::Left ), FVector3( 8, 3, 0 ), FQuaternion::Identity, FVector3::One, T ) && NMath::Abs( T - 1.0f ) < 1e-4f, "oriented box: a centre off the origin" );

		// 球
		C.Check( NWorldEditor::RayVsSphere( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 0, 0, 5 ), 1.0f, T ) && NMath::Abs( T - 4.0f ) < 1e-4f, "sphere: hit at T=4" );
		C.Check( !NWorldEditor::RayVsSphere( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 2, 0, 5 ), 1.0f, T ), "sphere: a ray 2 off the centre misses a radius-1 sphere" );
		C.Check( !NWorldEditor::RayVsSphere( MakeRay( FVector3( 0, 0, 5 ), FVector3::Forward ), FVector3( 0, 0, 5 ), 1.0f, T ), "sphere: a ray starting inside does not hit" );
		C.Check( !NWorldEditor::RayVsSphere( MakeRay( FVector3::Zero, FVector3::Backward ), FVector3( 0, 0, 5 ), 1.0f, T ), "sphere: a sphere behind the ray is not hit" );

		// カプセル（A=(0,0,0) B=(0,2,0) 半径 0.5）
		const FVector3 CapA( 0, 0, 0 ), CapB( 0, 2, 0 );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 5, 1, 0 ), FVector3::Left ), CapA, CapB, 0.5f, T ) && NMath::Abs( T - 4.5f ) < 1e-4f, "capsule: side hit at 4.5 (T=%.4f)", T );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0, 5, 0 ), FVector3::Down ), CapA, CapB, 0.5f, T ) && NMath::Abs( T - 2.5f ) < 1e-4f, "capsule: top of the upper cap at 2.5 (T=%.4f)", T );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0.3f, 5, 0 ), FVector3::Down ), CapA, CapB, 0.5f, T ) && NMath::Abs( T - ( 5.0f - ( 2.0f + std::sqrt( 0.25f - 0.09f ) ) ) ) < 1e-4f, "capsule: off-centre on the cap (T=%.4f)", T );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0, -5, 0 ), FVector3::Up ), CapA, CapB, 0.5f, T ) && NMath::Abs( T - 4.5f ) < 1e-4f, "capsule: bottom cap" );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0.3f, -5, 0 ), FVector3::Up ), CapA, CapB, 0.5f, T ) && NMath::Abs( T - ( 5.0f - std::sqrt( 0.25f - 0.09f ) ) ) < 1e-4f, "capsule: a ray parallel to the axis enters through the lower cap (T=%.4f)", T );
		C.Check( !NWorldEditor::RayVsCapsule( MakeRay( FVector3( 5, 3, 0 ), FVector3::Left ), CapA, CapB, 0.5f, T ), "capsule: passing above the upper cap misses" );
		C.Check( !NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0, 1, 0 ), FVector3::Left ), CapA, CapB, 0.5f, T ), "capsule: a ray starting inside does not hit" );
		C.Check( NWorldEditor::RayVsCapsule( MakeRay( FVector3( 0, 0, -5 ), FVector3::Forward ), CapA, CapA, 0.5f, T ) && NMath::Abs( T - 4.5f ) < 1e-4f, "capsule: a zero-length capsule is a sphere" );

		// 平面・四角形
		C.Check( NWorldEditor::RayVsPlane( MakeRay( FVector3( 1, 5, 2 ), FVector3::Down ), FVector3::Zero, FVector3::Up, T ) && NMath::Abs( T - 5.0f ) < 1e-4f, "plane: ground hit at 5" );
		C.Check( !NWorldEditor::RayVsPlane( MakeRay( FVector3( 0, 5, 0 ), FVector3::Forward ), FVector3::Zero, FVector3::Up, T ), "plane: a parallel ray misses" );
		C.Check( !NWorldEditor::RayVsPlane( MakeRay( FVector3( 0, 5, 0 ), FVector3::Up ), FVector3::Zero, FVector3::Up, T ), "plane: a plane behind the ray is not hit" );
		C.Check( NWorldEditor::RayVsPlane( MakeRay( FVector3( 0, -5, 0 ), FVector3::Up ), FVector3::Zero, FVector3::Up, T ) && NMath::Abs( T - 5.0f ) < 1e-4f, "plane: both faces" );
		const FVector3 QuadRight( 1, 0, 0 ), QuadUp( 0, 2, 0 );
		C.Check( NWorldEditor::RayVsQuad( MakeRay( FVector3( 0.5f, 1, -3 ), FVector3::Forward ), FVector3::Zero, QuadRight, QuadUp, T ) && NMath::Abs( T - 3.0f ) < 1e-4f, "quad: inside" );
		C.Check( !NWorldEditor::RayVsQuad( MakeRay( FVector3( 1.5f, 1, -3 ), FVector3::Forward ), FVector3::Zero, QuadRight, QuadUp, T ), "quad: outside to the right" );
		C.Check( !NWorldEditor::RayVsQuad( MakeRay( FVector3( 0.5f, 2.5f, -3 ), FVector3::Forward ), FVector3::Zero, QuadRight, QuadUp, T ), "quad: outside above (the half height is 2)" );
		C.Check( NWorldEditor::RayVsQuad( MakeRay( FVector3( 0.5f, 1, 3 ), FVector3::Backward ), FVector3::Zero, QuadRight, QuadUp, T ) && NMath::Abs( T - 3.0f ) < 1e-4f, "quad: both faces" );
		C.Check( !NWorldEditor::RayVsQuad( MakeRay( FVector3( 0, 0, -3 ), FVector3::Forward ), FVector3::Zero, FVector3::Zero, QuadUp, T ), "quad: a degenerate quad is never hit" );

		// 直線どうしの最近点
		float32 T1 = 0.0f, T2 = 0.0f;
		C.Check( NWorldEditor::ClosestPointsOnLines( FVector3::Zero, FVector3::Right, FVector3( 2, 3, 1 ), FVector3::Forward, T1, T2 ) && NMath::Abs( T1 - 2.0f ) < 1e-4f && NMath::Abs( T2 - -1.0f ) < 1e-4f, "lines: skew lines, closest at (2,0,0) and (2,3,0) (%.4f, %.4f)", T1, T2 );
		C.Check( !NWorldEditor::ClosestPointsOnLines( FVector3::Zero, FVector3::Right, FVector3( 0, 1, 0 ), FVector3::Right, T1, T2 ), "lines: parallel lines have no unique closest points" );

		// 光線と線分の距離
		float32 RayT = 0.0f, SegS = 0.0f;
		float32 Distance = NWorldEditor::DistanceRayToSegment( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 1, 0, 5 ), FVector3( 1, 2, 5 ), RayT, SegS );
		C.Check( NMath::Abs( Distance - 1.0f ) < 1e-3f && NMath::Abs( RayT - 5.0f ) < 1e-2f && NMath::Abs( SegS ) < 1e-3f, "ray-segment: distance 1 at ray T=5, segment start (%.4f, %.3f, %.3f)", Distance, RayT, SegS );
		Distance = NWorldEditor::DistanceRayToSegment( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 1, 0, -5 ), FVector3( 1, 2, -5 ), RayT, SegS );
		C.Check( NMath::Abs( Distance - std::sqrt( 26.0f ) ) < 1e-3f && RayT < 1e-3f, "ray-segment: a segment behind the origin is measured from the origin (%.4f)", Distance );
		Distance = NWorldEditor::DistanceRayToSegment( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 0, 3, 5 ), FVector3( 0, 6, 5 ), RayT, SegS );
		C.Check( NMath::Abs( Distance - 3.0f ) < 1e-3f && NMath::Abs( SegS ) < 1e-3f, "ray-segment: clamped to the segment's end (%.4f)", Distance );
		Distance = NWorldEditor::DistanceRayToSegment( MakeRay( FVector3::Zero, FVector3::Forward ), FVector3( 0, -1, 5 ), FVector3( 0, 1, 5 ), RayT, SegS );
		C.Check( Distance < 1e-3f && NMath::Abs( SegS - 0.5f ) < 1e-3f, "ray-segment: crossing the segment gives distance 0 at its middle" );
	}

	//=========================================================================
	// カメラ
	//=========================================================================
	void TestCamera( NAssetTest::FChecker& C )
	{
		C.Group = "editor-camera";
		const FEditorViewport View = MakeViewport();

		// 向き
		FEditorCamera Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		C.Check( Near3( Camera.GetEye(), FVector3( 0, 0, -10 ) ) && Near3( Camera.GetForward(), FVector3::Forward ) && Near3( Camera.GetRight(), FVector3::Right ) && Near3( Camera.GetUp(), FVector3::Up ), "yaw 0, pitch 0: looks along +Z from -Z; right = +X, up = +Y" );
		Camera = MakeCamera( NMath::HALF_PI, 0.0f, 10.0f );
		C.Check( Near3( Camera.GetEye(), FVector3( 10, 0, 0 ) ) && Near3( Camera.GetForward(), FVector3::Left ) && Near3( Camera.GetRight(), FVector3( 0, 0, 1 ) ), "yaw 90: the eye is on +X, looking toward -X, and the right is +Z (left-handed)" );
		Camera = MakeCamera( 0.0f, NMath::PI * 0.25f, 10.0f );
		C.Check( Camera.GetEye().y > 7.0f && Camera.GetEye().y < 7.2f && Camera.GetForward().y < 0.0f && Camera.GetUp().y > 0.0f, "positive pitch looks down from above" );
		const FVector3 F = Camera.GetForward(), R = Camera.GetRight(), U = Camera.GetUp();
		C.Check( NMath::Abs( F.Length() - 1.0f ) < 1e-4f && NMath::Abs( R.Length() - 1.0f ) < 1e-4f && NMath::Abs( U.Length() - 1.0f ) < 1e-4f && NMath::Abs( FVector3::Dot( F, R ) ) < 1e-4f && NMath::Abs( FVector3::Dot( F, U ) ) < 1e-4f && NMath::Abs( FVector3::Dot( R, U ) ) < 1e-4f,
				 "the camera basis is orthonormal" );

		// 画面との往復
		FEditorCamera Views[3] = { MakeCamera( 0.6f, 0.45f, 12.0f, FVector3( 1, 2, 3 ) ), MakeCamera( -2.0f, -0.3f, 3.0f, FVector3( -4, 0, 5 ) ), MakeCamera( 3.5f, 1.2f, 40.0f ) };
		bool bRoundTrip = true;
		bool bThrough = true;
		for ( const FEditorCamera& Cam : Views )
		{
			FVector2 Center;
			if ( !Cam.WorldToScreen( View, Cam.Target, Center ) || NMath::Abs( Center.x - 400.0f ) > 1e-2f || NMath::Abs( Center.y - 300.0f ) > 1e-2f ) bRoundTrip = false;
			for ( int32 i = 0; i < 12; ++i )
			{
				const float32 Px = 20.0f + 65.0f * static_cast<float32>( i ), Py = 15.0f + 47.0f * static_cast<float32>( i % 12 );
				const FEditorRay Ray = Cam.ScreenToRay( View, Px, Py );
				for ( const float32 Depth : { 1.0f, 7.5f, 60.0f } )
				{
					const FVector3 Point = Ray.PointAt( Depth );
					FVector2 Back;
					if ( !Cam.WorldToScreen( View, Point, Back ) || NMath::Abs( Back.x - Px ) > 0.05f || NMath::Abs( Back.y - Py ) > 0.05f ) bRoundTrip = false;
				}
				// 光線は視点から出て、長さ 1
				if ( !Near3( Ray.Origin, Cam.GetEye() ) || NMath::Abs( Ray.Direction.Length() - 1.0f ) > 1e-4f ) bThrough = false;
			}
		}
		C.Check( bRoundTrip, "the target projects to the viewport centre, and ScreenToRay -> point -> WorldToScreen returns the same pixel (3 cameras x 12 pixels x 3 depths)" );
		C.Check( bThrough, "rays start at the eye and are unit length" );

		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		FVector2 Pixel;
		float32 Depth = 0.0f;
		C.Check( Camera.WorldToScreen( View, FVector3( 0, 0, 0 ), Pixel, &Depth ) && NMath::Abs( Depth - 10.0f ) < 1e-4f, "WorldToScreen reports the depth along the view direction" );
		C.Check( !Camera.WorldToScreen( View, FVector3( 0, 0, -20 ), Pixel ), "a point behind the eye is not projected" );
		C.Check( !Camera.WorldToScreen( View, FVector3( 0, 0, -9.99f ), Pixel ), "a point nearer than the near plane is not projected" );
		Camera.WorldToScreen( View, FVector3( 1, 0, 0 ), Pixel );
		FVector2 Up1;
		Camera.WorldToScreen( View, FVector3( 0, 1, 0 ), Up1 );
		C.Check( Pixel.x > 400.0f && Up1.y < 300.0f, "world +X is to the right of the centre, world +Y is above it (screen Y points down)" );

		// 1 ピクセルの大きさ
		const float32 PixelSize = Camera.GetPixelSize( View, FVector3::Zero );
		C.Check( NMath::Abs( PixelSize - 2.0f * 10.0f * std::tan( 0.4f ) / 600.0f ) < 1e-5f, "pixel size = 2 * depth * tan(fov/2) / height (%.6f)", PixelSize );
		FVector2 Moved;
		Camera.WorldToScreen( View, FVector3( PixelSize * 50.0f, 0, 0 ), Moved );
		C.Check( NMath::Abs( Moved.x - 450.0f ) < 0.05f && NMath::Abs( Moved.y - 300.0f ) < 0.05f, "moving 50 pixel-sizes to the right moves 50 pixels (%.3f)", Moved.x );

		// 回す
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		Camera.Orbit( 30.0f, 0.0f );
		FVector2 FarPoint, NearPoint;
		C.Check( NMath::Abs( Camera.Yaw - -0.3f ) < 1e-5f && Near3( Camera.Target, FVector3::Zero ), "Orbit(30 px) lowers the yaw by 0.3 and keeps the target" );
		Camera.WorldToScreen( View, FVector3( 0, 0, 3 ), FarPoint );
		Camera.WorldToScreen( View, FVector3( 0, 0, -3 ), NearPoint );
		C.Check( NearPoint.x > 400.0f && FarPoint.x < 400.0f, "orbiting right: the near side of the target follows the mouse (moves right), the far side moves left" );
		Camera.Orbit( 0.0f, 1.0e6f );
		C.Check( Camera.Pitch == FEditorCamera::MaxPitch, "the pitch stops before straight down" );
		Camera.Orbit( 0.0f, -1.0e7f );
		C.Check( Camera.Pitch == -FEditorCamera::MaxPitch, "and before straight up" );

		// 見回す: 視点は動かない
		Camera = MakeCamera( 0.6f, 0.45f, 12.0f, FVector3( 1, 2, 3 ) );
		const FVector3 EyeBefore = Camera.GetEye();
		Camera.LookAround( 40.0f, 20.0f );
		C.Check( Near3( Camera.GetEye(), EyeBefore, 1e-3f ) && NMath::Abs( Camera.Yaw - 0.2f ) < 1e-5f && NMath::Abs( Camera.Pitch - 0.65f ) < 1e-5f, "LookAround keeps the eye and turns the view (yaw %.3f, pitch %.3f)", Camera.Yaw, Camera.Pitch );

		// 平行移動: つかんだ点がマウスについてくる
		Camera = MakeCamera( 0.6f, 0.45f, 12.0f, FVector3( 1, 2, 3 ) );
		FVector2 Before, After;
		Camera.WorldToScreen( View, Camera.Target, Before );
		const FVector3 OldTarget = Camera.Target;
		Camera.Pan( View, 50.0f, -20.0f );
		Camera.WorldToScreen( View, OldTarget, After );
		C.Check( NMath::Abs( After.x - ( Before.x + 50.0f ) ) < 0.05f && NMath::Abs( After.y - ( Before.y - 20.0f ) ) < 0.05f, "Pan(50, -20 px) moves the previously centred point by exactly (50, -20) pixels (%.3f, %.3f)", After.x - Before.x, After.y - Before.y );

		// ズーム
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		Camera.Zoom( 1.0f );
		C.Check( NMath::Abs( Camera.Distance - 9.0f ) < 1e-4f, "Zoom(1) is 10%% closer" );
		Camera.Zoom( -1.0f );
		C.Check( NMath::Abs( Camera.Distance - 10.0f ) < 1e-3f, "Zoom(-1) undoes it" );
		Camera.Zoom( 1000.0f );
		C.Check( Camera.Distance == FEditorCamera::MinDistance, "the distance has a floor" );
		Camera.Zoom( -100000.0f );
		C.Check( Camera.Distance == FEditorCamera::MaxDistance, "and a ceiling" );

		// 前後左右へ進む
		Camera = MakeCamera( 0.6f, 0.45f, 12.0f, FVector3( 1, 2, 3 ) );
		const FVector3 Eye0 = Camera.GetEye();
		Camera.MoveLocal( FVector3( 0, 0, 2 ) );
		C.Check( Near3( Camera.GetEye() - Eye0, Camera.GetForward() * 2.0f, 1e-3f ) && NMath::Abs( Camera.Distance - 12.0f ) < 1e-6f, "MoveLocal(0,0,2) moves the eye 2 along the view direction and keeps the distance" );
		const FVector3 Eye1 = Camera.GetEye();
		Camera.MoveLocal( FVector3( 1, 0, 0 ) );
		C.Check( Near3( Camera.GetEye() - Eye1, Camera.GetRight(), 1e-3f ), "MoveLocal(1,0,0) moves right" );

		// 寄る
		Camera = MakeCamera( 0.6f, 0.45f, 50.0f, FVector3::Zero );
		Camera.Focus( FVector3( 5, 0, 0 ), 2.0f, View );
		FVector2 TopPoint, SidePoint;
		Camera.WorldToScreen( View, FVector3( 5, 2, 0 ), TopPoint );
		const FVector3 SideDirection = Camera.GetRight() * 2.0f;
		Camera.WorldToScreen( View, FVector3( 5, 0, 0 ) + SideDirection, SidePoint );
		C.Check( Near3( Camera.Target, FVector3( 5, 0, 0 ) ) && Camera.Distance > 5.0f && Camera.Distance < 7.0f, "Focus centres on the sphere at a distance that fits it (%.3f)", Camera.Distance );
		C.Check( SidePoint.x > 400.0f && SidePoint.x < 700.0f && NMath::Abs( ( 300.0f - SidePoint.y ) ) < 1.0f, "Focus: the sphere's side stays inside the viewport with a margin (%.1f)", SidePoint.x );

		// 線分の投影（近い面で切る）
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		FVector2 PixelA, PixelB;
		C.Check( Camera.ProjectSegment( View, FVector3( 0, 0, 0 ), FVector3( 10, 0, -30 ), PixelA, PixelB ) && NMath::Abs( PixelA.x - 400.0f ) < 0.1f && PixelB.x > PixelA.x, "ProjectSegment clips a segment that crosses the eye plane (B ends at x=%.1f)", PixelB.x );
		C.Check( Camera.ProjectSegment( View, FVector3( -10, 0, -30 ), FVector3( 0, 0, 0 ), PixelA, PixelB ) && PixelA.x < PixelB.x, "and clips the first end the same way" );
		C.Check( !Camera.ProjectSegment( View, FVector3( 0, 0, -30 ), FVector3( 5, 0, -20 ), PixelA, PixelB ), "a segment entirely behind the eye gives nothing" );
		C.Check( Camera.ProjectSegment( View, FVector3( -1, 0, 0 ), FVector3( 1, 0, 0 ), PixelA, PixelB ) && PixelA.x < 400.0f && PixelB.x > 400.0f, "a segment in front is projected as is" );
	}

	//=========================================================================
	// 図形の作り方
	//=========================================================================
	FWorldObject MakeShapeObject( const char* Name )
	{
		FWorldObject Object;
		Object.Id = 1;
		Object.Name = Name;
		return Object;
	}

	void TestShapes( NAssetTest::FChecker& C )
	{
		C.Group = "editor-shapes";
		FFakeEditorAssets Assets;
		Assets.Models.Add( { "Models/Box.mv1", FVector3( -1, 0, -1 ), FVector3( 1, 2, 1 ) } );
		FEditorShapeContext Context;
		Context.Assets = &Assets;

		// モデル
		FWorldObject Model = MakeShapeObject( "Model" );
		FWorldModel Spec;
		Spec.Path = "Models/Box.mv1";
		Model.Model = Spec;
		FTransform At( FVector3( 10, 0, 0 ), FQuaternion::Identity, FVector3( 2, 2, 2 ) );
		TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Model, At, Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Box && Shapes[0].Role == EEditorShapeRole::Model && Near3( Shapes[0].Center, FVector3( 10, 2, 0 ) ) && Near3( Shapes[0].Half, FVector3( 2, 2, 2 ) ) && !Shapes[0].bApproximate,
				 "a model is its bounds box: centre (10,2,0), half (2,2,2) at scale 2" );
		At = FTransform( FVector3( 0, 0, 0 ), FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI ), FVector3::One );
		Shapes = NWorldEditor::GatherShapes( Model, At, Context );
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].Center, FVector3( 0, 1, 0 ) ) && Shapes[0].Rotation.Equals( At.Rotation, 1e-5f ), "the box takes the object's rotation (its centre is on the Y axis, so it does not move)" );
		At = FTransform( FVector3( 3, 0, 0 ), FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI ), FVector3::One );
		FWorldObject OffCenter = Model;
		Assets.Models.Add( { "Models/Off.mv1", FVector3( 0, 0, 0 ), FVector3( 4, 2, 2 ) } );
		OffCenter.Model.GetValue().Path = "Models/Off.mv1";
		Shapes = NWorldEditor::GatherShapes( OffCenter, At, Context );
		// 局所の中心 (2,1,1) を Y 軸まわりに 90 度回し、(3,0,0) へ動かす
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].Center, At.TransformPoint( FVector3( 2, 1, 1 ) ) ), "an off-centre bounds box has its centre rotated and moved with the object" );
		FWorldObject Unknown = Model;
		Unknown.Model.GetValue().Path = "Models/Missing.mv1";
		Shapes = NWorldEditor::GatherShapes( Unknown, FTransform::Identity, Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].bApproximate && Near3( Shapes[0].Half, FVector3( 0.5f, 0.5f, 0.5f ) ), "a model whose bounds cannot be read is a flagged 1 m placeholder box" );

		// 当たり判定
		FWorldObject Boxy = MakeShapeObject( "Boxy" );
		FWorldCollider Box;
		Box.Shape = EWorldColliderShape::Box;
		Box.Size = FVector3( 2, 4, 6 );
		Box.Offset = FVector3( 0, 1, 0 );
		Boxy.Collider = Box;
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3( 5, 0, 0 ) ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Role == EEditorShapeRole::Collider && Near3( Shapes[0].Half, FVector3( 1, 2, 3 ) ) && Near3( Shapes[0].Center, FVector3( 5, 1, 0 ) ), "a box collider: half = size / 2, centre = position + offset" );
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3( 5, 0, 0 ), FQuaternion::Identity, FVector3( -2, 1, 3 ) ), Context );
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].Half, FVector3( 2, 2, 9 ) ), "scale multiplies the half extents (a negative scale by its absolute value, as the physics does)" );
		Boxy.Collider.GetValue().Shape = EWorldColliderShape::Sphere;
		Boxy.Collider.GetValue().Radius = 0.5f;
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 2, 1, 1 ) ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Sphere && NMath::Abs( Shapes[0].Radius - 1.0f ) < 1e-5f, "a sphere collider's radius follows the largest scale" );
		Boxy.Collider.GetValue().Shape = EWorldColliderShape::Capsule;
		Boxy.Collider.GetValue().Radius = 0.5f;
		Boxy.Collider.GetValue().Height = 2.5f;
		Boxy.Collider.GetValue().Offset = FVector3::Zero;
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3::Zero ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Capsule && Near3( Shapes[0].A, FVector3( 0, -0.75f, 0 ) ) && Near3( Shapes[0].B, FVector3( 0, 0.75f, 0 ) ) && NMath::Abs( Shapes[0].Radius - 0.5f ) < 1e-5f,
				 "a capsule of height 2.5 and radius 0.5 has its end centres 0.75 above and below (the height includes the caps)" );
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Forward, NMath::HALF_PI ) ), Context );
		C.Check( Shapes.Num() == 1 && NMath::Abs( NMath::Abs( Shapes[0].B.x - Shapes[0].A.x ) - 1.5f ) < 1e-3f && NMath::Abs( Shapes[0].B.y - Shapes[0].A.y ) < 1e-3f, "a rolled capsule lies along X" );
		Shapes = NWorldEditor::GatherShapes( Boxy, FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 1, 2, 1 ) ), Context );
		C.Check( Shapes.Num() == 1 && NMath::Abs( ( Shapes[0].B - Shapes[0].A ).Length() - 3.0f ) < 1e-3f && NMath::Abs( Shapes[0].Radius - 0.5f ) < 1e-5f, "a capsule scaled 2x in Y has its end centres twice as far apart (3.0), with the same radius" );

		// ビルボード（World = 向きが固定）
		FWorldObject Board = MakeShapeObject( "Board" );
		FWorldBillboard BoardSpec;
		BoardSpec.Texture = "Tex/a.png";
		BoardSpec.Size = FVector2( 2.0f, 3.0f );
		BoardSpec.Pivot = FVector2( 0.5f, 1.0f );
		BoardSpec.Mode = EWorldBillboardMode::World;
		Board.Billboard = BoardSpec;
		Shapes = NWorldEditor::GatherShapes( Board, FTransform( FVector3( 1, 0, 0 ) ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Quad && Shapes[0].Role == EEditorShapeRole::Billboard && Near3( Shapes[0].Center, FVector3( 1, 1.5f, 0 ) ) && Near3( Shapes[0].HalfRight, FVector3( 1, 0, 0 ) ) && Near3( Shapes[0].HalfUp, FVector3( 0, 1.5f, 0 ) ),
				 "a billboard with its pivot at the feet stands on its position: centre 1.5 above, half size (1, 1.5)" );
		Shapes = NWorldEditor::GatherShapes( Board, FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 2, 2, 1 ) ), Context );
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].HalfRight, FVector3( 2, 0, 0 ) ) && Near3( Shapes[0].HalfUp, FVector3( 0, 3, 0 ) ), "the object's x/y scale multiplies the size" );
		Board.Billboard.GetValue().Pivot = FVector2( 0.0f, 0.0f );
		Shapes = NWorldEditor::GatherShapes( Board, FTransform( FVector3::Zero ), Context );
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].Center, FVector3( 1.0f, -1.5f, 0 ) ), "a top-left pivot puts the quad to the right of and below the position" );
		Board.Billboard.GetValue().Size = FVector2( 0.0f, 3.0f );
		Shapes = NWorldEditor::GatherShapes( Board, FTransform( FVector3::Zero ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Role == EEditorShapeRole::Marker, "a zero-size billboard falls back to a marker so it can still be selected" );

		// ビルボードの向き（カメラ）
		const FEditorViewport View = MakeViewport();
		FEditorCamera Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		FVector3 Right, UpAxis;
		NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode::Camera, FTransform::Identity, Camera.GetEye(), Camera.GetRight(), Camera.GetUp(), Right, UpAxis );
		C.Check( Near3( Right, FVector3::Right ) && Near3( UpAxis, FVector3::Up ), "Camera mode: the camera's right and up" );
		NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode::Camera, FTransform( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Forward, NMath::HALF_PI ) ), Camera.GetEye(), Camera.GetRight(), Camera.GetUp(), Right, UpAxis );
		C.Check( Near3( Right, FVector3::Up ) && Near3( UpAxis, FVector3::Left ), "Camera mode: a +90 degree roll tilts the right axis up (the same sense as the draw executor)" );
		Camera = MakeCamera( 0.0f, 0.8f, 10.0f );
		NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode::CameraY, FTransform::Identity, Camera.GetEye(), Camera.GetRight(), Camera.GetUp(), Right, UpAxis );
		C.Check( Near3( Right, FVector3::Right ) && Near3( UpAxis, FVector3::Up ), "CameraY: stays upright even when the camera looks down" );
		Camera = MakeCamera( NMath::HALF_PI, 0.0f, 10.0f );
		NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode::CameraY, FTransform::Identity, Camera.GetEye(), Camera.GetRight(), Camera.GetUp(), Right, UpAxis );
		C.Check( Near3( Right, FVector3( 0, 0, 1 ) ), "CameraY: faces the camera horizontally (camera on +X: the right axis is +Z)" );
		Camera = MakeCamera( 0.0f, 1.4f, 10.0f );
		NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode::CameraY, FTransform( FVector3( 0, 0, 0 ) ), FVector3( 0, 10, 0 ), Camera.GetRight(), Camera.GetUp(), Right, UpAxis );
		C.Check( Near3( Right, Camera.GetRight() ), "CameraY: looking straight down uses the camera's right instead" );
		Context.Camera = &Camera;
		Context.Viewport = View;
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		Board.Billboard.GetValue().Size = FVector2( 2.0f, 2.0f );
		Board.Billboard.GetValue().Pivot = FVector2( 0.5f, 0.5f );
		Board.Billboard.GetValue().Mode = EWorldBillboardMode::Camera;
		Shapes = NWorldEditor::GatherShapes( Board, FTransform( FVector3( 3, 4, 5 ) ), Context );
		C.Check( Shapes.Num() == 1 && Near3( Shapes[0].HalfRight, FVector3( 1, 0, 0 ) ) && Near3( Shapes[0].HalfUp, FVector3( 0, 1, 0 ) ), "with a camera in the context a Camera-mode billboard uses the camera's axes" );

		// 目印
		FWorldObject Empty = MakeShapeObject( "Empty" );
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		Context.Camera = &Camera;
		Shapes = NWorldEditor::GatherShapes( Empty, FTransform( FVector3::Zero ), Context );
		const float32 Expected = Camera.GetPixelSize( View, FVector3::Zero ) * Context.MarkerPixels;
		C.Check( Shapes.Num() == 1 && Shapes[0].Role == EEditorShapeRole::Marker && NMath::Abs( Shapes[0].Radius - Expected ) < 1e-5f, "an empty object is a marker whose radius is %.1f pixels on screen", Context.MarkerPixels );
		Context.Camera = nullptr;
		Shapes = NWorldEditor::GatherShapes( Empty, FTransform( FVector3::Zero ), Context );
		C.Check( Shapes.Num() == 1 && NMath::Abs( Shapes[0].Radius - 0.25f ) < 1e-6f, "and 0.25 without a camera" );
		FWorldObject Sound = MakeShapeObject( "Sound" );
		Sound.Sound = FWorldSound();
		Shapes = NWorldEditor::GatherShapes( Sound, FTransform( FVector3::Zero ), Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Role == EEditorShapeRole::Sound, "a sound object is a sound marker" );
		FWorldObject Multi = MakeShapeObject( "Multi" );
		Multi.Model = Spec;
		Multi.Collider = FWorldCollider();
		Shapes = NWorldEditor::GatherShapes( Multi, FTransform::Identity, Context );
		C.Check( Shapes.Num() == 2 && Shapes[0].Role == EEditorShapeRole::Model && Shapes[1].Role == EEditorShapeRole::Collider, "an object with a model and a collider has both shapes (and no marker)" );

		// 囲む箱
		FEditorShape Rotated;
		Rotated.Kind = EEditorShapeKind::Box;
		Rotated.Rotation = FQuaternion::FromAxisAngle( FVector3::Up, NMath::PI * 0.25f );
		Rotated.Half = FVector3( 1, 1, 1 );
		Rotated.Center = FVector3( 5, 0, 0 );
		FVector3 Min, Max;
		C.Check( NWorldEditor::ComputeShapesBounds( TArray<FEditorShape>{ Rotated }, Min, Max ) && Near3( Min, FVector3( 5.0f - std::sqrt( 2.0f ), -1, -std::sqrt( 2.0f ) ) ) && Near3( Max, FVector3( 5.0f + std::sqrt( 2.0f ), 1, std::sqrt( 2.0f ) ) ), "the bounds of a box turned 45 degrees reach sqrt(2) along X and Z" );
		C.Check( !NWorldEditor::ComputeShapesBounds( TArray<FEditorShape>(), Min, Max ), "no shapes: no bounds" );

		// 輪郭の線
		TArray<FVector3> Lines;
		NWorldEditor::AppendShapeWireLines( Rotated, 16, Lines );
		bool bOnCorners = Lines.Num() == 24;
		for ( const FVector3& Point : Lines )
		{
			const FVector3 Local = Rotated.Rotation.UnrotateVector( Point - Rotated.Center );
			if ( NMath::Abs( NMath::Abs( Local.x ) - 1.0f ) > 1e-3f || NMath::Abs( NMath::Abs( Local.y ) - 1.0f ) > 1e-3f || NMath::Abs( NMath::Abs( Local.z ) - 1.0f ) > 1e-3f ) bOnCorners = false;
		}
		C.Check( bOnCorners, "a box outline is 12 edges between the 8 corners" );
		FEditorShape Ball;
		Ball.Kind = EEditorShapeKind::Sphere;
		Ball.Center = FVector3( 1, 2, 3 );
		Ball.Radius = 1.5f;
		Lines.Reset();
		NWorldEditor::AppendShapeWireLines( Ball, 16, Lines );
		bool bOnSphere = Lines.Num() == 3 * 16 * 2;
		for ( const FVector3& Point : Lines ) bOnSphere = bOnSphere && NMath::Abs( FVector3::Distance( Point, Ball.Center ) - 1.5f ) < 1e-3f;
		C.Check( bOnSphere, "a sphere outline is three circles, every point at the radius" );
		FEditorShape Pill;
		Pill.Kind = EEditorShapeKind::Capsule;
		Pill.A = FVector3( 0, 0, 0 );
		Pill.B = FVector3( 1, 2, 0.5f );
		Pill.Radius = 0.4f;
		Lines.Reset();
		NWorldEditor::AppendShapeWireLines( Pill, 16, Lines );
		bool bOnCapsule = Lines.Num() > 40;
		for ( const FVector3& Point : Lines )
		{
			// 点から線分 A-B への距離が、半径に等しい
			const FVector3 Axis = Pill.B - Pill.A;
			const float32 Along = NMath::Clamp( FVector3::Dot( Point - Pill.A, Axis ) / Axis.LengthSquared(), 0.0f, 1.0f );
			if ( NMath::Abs( FVector3::Distance( Point, Pill.A + Axis * Along ) - 0.4f ) > 1e-3f ) bOnCapsule = false;
		}
		C.Check( bOnCapsule, "a capsule outline lies exactly on the capsule's surface (%d points)", Lines.Num() );
		FEditorShape Plate;
		Plate.Kind = EEditorShapeKind::Quad;
		Plate.Center = FVector3( 0, 1, 0 );
		Plate.HalfRight = FVector3( 2, 0, 0 );
		Plate.HalfUp = FVector3( 0, 1, 0 );
		Lines.Reset();
		NWorldEditor::AppendShapeWireLines( Plate, 16, Lines );
		C.Check( Lines.Num() == 8 && Near3( Lines[0], FVector3( 2, 2, 0 ) ), "a quad outline is 4 edges" );
	}

	//=========================================================================
	// ピッキング
	//=========================================================================
	FWorldObject MakeColliderObject( int32 Id, const char* Name, const FVector3& Position, EWorldColliderShape Shape, const FVector3& Size )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Transform.Position = Position;
		FWorldCollider Collider;
		Collider.Shape = Shape;
		Collider.Size = Size;
		Collider.Radius = Size.x * 0.5f;
		Object.Collider = Collider;
		return Object;
	}

	void TestPicking( NAssetTest::FChecker& C )
	{
		C.Group = "editor-picking";
		const FEditorViewport View = MakeViewport();
		FEditorCamera Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
		FFakeEditorAssets Assets;
		Assets.Models.Add( { "Models/Box.mv1", FVector3( -1, 0, -1 ), FVector3( 1, 2, 1 ) } );
		FEditorShapeContext Context;
		Context.Assets = &Assets;
		Context.Camera = &Camera;
		Context.Viewport = View;

		FWorldDefinition World;
		World.Objects.Add( MakeColliderObject( 10, "Near", FVector3( 0, 0, 0 ), EWorldColliderShape::Box, FVector3( 2, 2, 2 ) ) );
		World.Objects.Add( MakeColliderObject( 20, "Far", FVector3( 0, 0, 5 ), EWorldColliderShape::Sphere, FVector3( 2, 2, 2 ) ) );
		World.Objects.Add( MakeColliderObject( 30, "Side", FVector3( 3, 0, 0 ), EWorldColliderShape::Box, FVector3( 1, 1, 1 ) ) );
		World.NextId = 40;

		// 画面の中心の光線は Near（面は z=-1 で距離 9）、次に Far（面は z=4 で距離 14）
		const FEditorRay Center = Camera.ScreenToRay( View, 400.0f, 300.0f );
		TArray<FEditorHit> Hits = NWorldEditor::PickObjects( World, Center, Context );
		C.Check( Hits.Num() == 2 && Hits[0].ObjectId == 10 && Hits[1].ObjectId == 20 && NMath::Abs( Hits[0].Distance - 9.0f ) < 1e-3f && NMath::Abs( Hits[1].Distance - 14.0f ) < 1e-3f, "the ray through the centre hits Near (9) then Far (14), nearest first (%d hits)", Hits.Num() );
		C.Check( NWorldEditor::PickNext( Hits, 10 ) == 20 && NWorldEditor::PickNext( Hits, 20 ) == 10 && NWorldEditor::PickNext( Hits, 99 ) == 10 && NWorldEditor::PickNext( TArray<FEditorHit>(), 10 ) == WorldNoId, "PickNext cycles through the overlapped objects" );

		// 各オブジェクトの画面の位置をクリックすると、そのオブジェクト
		bool bAll = true;
		for ( const FWorldObject& Object : World.Objects )
		{
			if ( Object.Id == 20 ) continue;   // Far は、Near の真後ろにあって隠れている
			const TArray<FEditorHit> Clicked = NWorldEditor::PickObjects( World, RayThrough( Camera, View, Object.Transform.Position + FVector3( 0.2f, 0.1f, 0 ) ), Context );
			if ( Clicked.IsEmpty() || Clicked[0].ObjectId != Object.Id ) bAll = false;
		}
		C.Check( bAll, "clicking on each object picks it" );

		// 空
		C.Check( NWorldEditor::PickObjects( World, Camera.ScreenToRay( View, 5.0f, 5.0f ), Context ).IsEmpty(), "clicking on empty space picks nothing" );

		// 無効・当たり判定の絞り込み
		World.Objects[0].bEnabled = false;
		FEditorPickOptions Options;
		Options.bPickDisabled = false;
		Hits = NWorldEditor::PickObjects( World, Center, Context, Options );
		C.Check( Hits.Num() == 1 && Hits[0].ObjectId == 20, "disabled objects can be left out" );
		Hits = NWorldEditor::PickObjects( World, Center, Context );
		C.Check( Hits.Num() == 2 && Hits[0].ObjectId == 10, "and are picked by default" );
		World.Objects[0].bEnabled = true;
		Options = FEditorPickOptions();
		Options.bPickColliders = false;
		C.Check( NWorldEditor::PickObjects( World, Center, Context, Options ).IsEmpty(), "colliders can be left out (an object with only a collider is then not pickable)" );

		// 目印: 画面で 9 ピクセル
		FWorldDefinition Markers;
		FWorldObject Empty;
		Empty.Id = 1;
		Empty.Name = "Spawn";
		Empty.Transform.Position = FVector3( 0, 0, 0 );
		Markers.Objects.Add( Empty );
		Markers.NextId = 2;
		FVector2 Pixel;
		Camera.WorldToScreen( View, FVector3::Zero, Pixel );
		C.Check( !NWorldEditor::PickObjects( Markers, Camera.ScreenToRay( View, Pixel.x + 5.0f, Pixel.y ), Context ).IsEmpty() && NWorldEditor::PickObjects( Markers, Camera.ScreenToRay( View, Pixel.x + 20.0f, Pixel.y ), Context ).IsEmpty(), "an empty object's marker is clickable within about 9 pixels, not at 20" );
		Camera = MakeCamera( 0.0f, 0.0f, 60.0f );
		Camera.WorldToScreen( View, FVector3::Zero, Pixel );
		C.Check( !NWorldEditor::PickObjects( Markers, Camera.ScreenToRay( View, Pixel.x + 5.0f, Pixel.y ), Context ).IsEmpty(), "and stays that size on screen when the camera is far away" );
		Camera = MakeCamera( 0.0f, 0.0f, 10.0f );

		// 矩形選択
		const TArray<int32> InRect = NWorldEditor::FindObjectsInRect( World, Context, 0.0f, 0.0f, 799.0f, 599.0f );
		C.Check( InRect.Num() == 3, "a rectangle covering the viewport selects everything in front of the camera (%d)", InRect.Num() );
		FVector2 SidePixel;
		Camera.WorldToScreen( View, FVector3( 3, 0, 0 ), SidePixel );
		const TArray<int32> Right = NWorldEditor::FindObjectsInRect( World, Context, SidePixel.x - 20.0f, 0.0f, 799.0f, 599.0f );
		C.Check( Right.Num() == 1 && Right[0] == 30, "a rectangle around only the right object selects just it" );
		const TArray<int32> Reversed = NWorldEditor::FindObjectsInRect( World, Context, 799.0f, 599.0f, SidePixel.x - 20.0f, 0.0f );
		C.Check( Reversed.Num() == 1 && Reversed[0] == 30, "the corners may be given in either order" );
		FEditorShapeContext NoCamera;
		C.Check( NWorldEditor::FindObjectsInRect( World, NoCamera, 0, 0, 800, 600 ).IsEmpty(), "a rectangle needs a camera" );

		// 囲む球
		FVector3 SphereCenter;
		float32 Radius = 0.0f;
		TArray<int32> Both;
		Both.Add( 10 );
		Both.Add( 30 );
		C.Check( NWorldEditor::ComputeBoundingSphere( World, Both, NoCamera, SphereCenter, Radius ) && Near3( SphereCenter, FVector3( 1.25f, 0, 0 ) ) && Radius > 2.6f && Radius < 2.7f, "the bounding sphere of Near and Side is centred between them (%.2f)", Radius );
		TArray<int32> Missing;
		Missing.Add( 999 );
		C.Check( !NWorldEditor::ComputeBoundingSphere( World, Missing, NoCamera, SphereCenter, Radius ), "ids that do not exist give nothing" );

		// 階層: 親の姿勢が子に掛かる
		FWorldDefinition Nested;
		FWorldObject Parent;
		Parent.Id = 1;
		Parent.Name = "Parent";
		Parent.Transform.Position = FVector3( 0, 0, 20 );
		Parent.Transform.RotationDegrees = FVector3( 0, 0, 90 );
		Parent.Transform.Scale = FVector3( 2, 2, 2 );
		Nested.Objects.Add( Parent );
		FWorldObject Child = MakeColliderObject( 2, "Child", FVector3( 1, 0, 0 ), EWorldColliderShape::Sphere, FVector3( 1, 1, 1 ) );
		Child.Parent = 1;
		Nested.Objects.Add( Child );
		Nested.NextId = 3;
		// 子の位置 (1,0,0) は、親（Z 軸まわり 90 度・2 倍）で、(0,0,20) + Rotate((2,0,0)) = (0, 2, 20)（親の目印とは重ならない）
		const FVector3 ChildWorld = FTransform( FVector3( 0, 0, 20 ), FQuaternion::EulerToQuat( FVector3( 0, 0, NMath::HALF_PI ) ), FVector3( 2, 2, 2 ) ).TransformPoint( FVector3( 1, 0, 0 ) );
		const TArray<FEditorHit> ChildHits = NWorldEditor::PickObjects( Nested, RayThrough( Camera, View, ChildWorld ), Context );
		C.Check( !ChildHits.IsEmpty() && ChildHits[0].ObjectId == 2, "a child is picked where its parent's transform puts it" );
	}

	//=========================================================================
	// ギズモ
	//=========================================================================
	// フレームの軸 Axis の上の、原点から Fraction * Length の点
	// 表示している軸（カメラ側へ反転していることがある）の上の、原点から Fraction * Length の点
	FVector3 OnAxis( const FEditorGizmoFrame& Frame, int32 Axis, float32 Fraction )
	{
		FVector3 Start, End;
		NWorldEditor::GetGizmoAxisSegment( Frame, Axis, Start, End );
		return Start + ( End - Start ) * Fraction;
	}

	void TestGizmo( NAssetTest::FChecker& C )
	{
		C.Group = "editor-gizmo";
		const FEditorViewport View = MakeViewport();
		const FEditorCamera Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		const FEditorGizmoFrame Frame = NWorldEditor::MakeGizmoFrame( Camera, View, FVector3::Zero, FQuaternion::Identity );
		C.Check( NMath::Abs( Frame.Length - Camera.GetPixelSize( View, FVector3::Zero ) * 100.0f ) < 1e-5f && Near3( Frame.Axes[0], FVector3::Right ) && Near3( Frame.Axes[1], FVector3::Up ) && Near3( Frame.Axes[2], FVector3::Forward ), "the gizmo is 100 pixels long on screen, along the world axes" );
		const FEditorGizmoFrame Far = NWorldEditor::MakeGizmoFrame( MakeCamera( 0.6f, 0.45f, 40.0f ), View, FVector3::Zero, FQuaternion::Identity );
		FVector2 TipNear, TipFar, OriginNear, OriginFar;
		Camera.WorldToScreen( View, OnAxis( Frame, 1, 1.0f ), TipNear );
		Camera.WorldToScreen( View, FVector3::Zero, OriginNear );
		MakeCamera( 0.6f, 0.45f, 40.0f ).WorldToScreen( View, OnAxis( Far, 1, 1.0f ), TipFar );
		MakeCamera( 0.6f, 0.45f, 40.0f ).WorldToScreen( View, FVector3::Zero, OriginFar );
		C.Check( NMath::Abs( ( TipNear - OriginNear ).Length() - ( TipFar - OriginFar ).Length() ) < 0.5f, "its size on screen does not change with the distance" );
		C.Check( Frame.Flip[0] == 1.0f && Frame.Flip[1] == 1.0f && Frame.Flip[2] == -1.0f, "seen from (+x, +y, -z), the Z handle is flipped toward the camera and the others are not (%.0f %.0f %.0f)", Frame.Flip[0], Frame.Flip[1], Frame.Flip[2] );
		{
			FVector3 ZStart, ZEnd;
			NWorldEditor::GetGizmoAxisSegment( Frame, 2, ZStart, ZEnd );
			C.Check( Near3( ZEnd, FVector3( 0, 0, -Frame.Length ) ) && Near3( Frame.Axes[2], FVector3::Forward ), "the flipped Z handle is drawn toward -Z while the axis itself still points +Z" );
		}
		const FEditorGizmoFrame Turned = NWorldEditor::MakeGizmoFrame( Camera, View, FVector3( 1, 2, 3 ), FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI ) );
		C.Check( Near3( Turned.Axes[0], FVector3( 0, 0, -1 ) ) || Near3( Turned.Axes[0], FVector3( 0, 0, 1 ) ), "a local-space gizmo takes the orientation" );

		// 当たり: 移動
		// 面の四角が、軸の手前に重なって見える範囲もある（面が優先）。軸の先のほうで、何か所かは軸に当たること
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			int32 NumAxisHits = 0;
			for ( int32 Step = 0; Step <= 8; ++Step )
			{
				const float32 Fraction = 0.6f + 0.05f * static_cast<float32>( Step );
				if ( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Translate, RayThrough( Camera, View, OnAxis( Frame, Axis, Fraction ) ) ) == GetGizmoAxisHandle( Axis ) ) ++NumAxisHits;
			}
			C.Check( NumAxisHits >= 8, "translate: rays through axis %d (60-100%% along) hit that axis at %d of 9 places", Axis, NumAxisHits );
		}
		// どの向きから見ても、各軸の 90%% は、その軸に当たる（手前へ反転しているので、面に隠れない）
		{
			bool bEverywhere = true;
			int32 NumViews = 0;
			for ( int32 YawStep = 0; YawStep < 12; ++YawStep )
			{
				for ( const float32 Pitch : { -0.9f, -0.3f, 0.3f, 0.9f } )
				{
					const FEditorCamera Other = MakeCamera( NMath::TWO_PI * static_cast<float32>( YawStep ) / 12.0f + 0.1f, Pitch, 10.0f );
					const FEditorGizmoFrame OtherFrame = NWorldEditor::MakeGizmoFrame( Other, View, FVector3::Zero, FQuaternion::Identity );
					++NumViews;
					for ( int32 Axis = 0; Axis < 3; ++Axis )
					{
						// 軸が画面でほぼ点になる向き（真正面から見ている）は除く
						FVector2 StartPixel, EndPixel;
						FVector3 Start, End;
						NWorldEditor::GetGizmoAxisSegment( OtherFrame, Axis, Start, End );
						if ( !Other.WorldToScreen( View, Start, StartPixel ) || !Other.WorldToScreen( View, End, EndPixel ) || ( EndPixel - StartPixel ).Length() < 30.0f ) continue;
						if ( NWorldEditor::HitTestGizmo( OtherFrame, EEditorGizmoMode::Translate, RayThrough( Other, View, OnAxis( OtherFrame, Axis, 0.9f ) ) ) != GetGizmoAxisHandle( Axis ) ) bEverywhere = false;
					}
				}
			}
			C.Check( bEverywhere && NumViews == 48, "translate: from 48 viewpoints around the object, every axis that is not seen end-on is hit at 90%% of its length" );
		}
		bool bPlanes = true;
		for ( int32 Normal = 0; Normal < 3; ++Normal )
		{
			FVector3 Corners[4];
			NWorldEditor::GetGizmoPlaneCorners( Frame, Normal, Corners );
			const EEditorGizmoHandle Hit = NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Translate, RayThrough( Camera, View, ( Corners[0] + Corners[2] ) * 0.5f ) );
			if ( Hit != GetGizmoPlaneHandle( Normal ) ) bPlanes = false;
		}
		C.Check( bPlanes, "translate: a ray through the centre of each plane square hits that plane" );
		C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Translate, RayThrough( Camera, View, FVector3::Zero ) ) == EEditorGizmoHandle::Free, "translate: the centre is the free handle (it wins over the axes there)" );
		C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Translate, Camera.ScreenToRay( View, 20.0f, 20.0f ) ) == EEditorGizmoHandle::None, "translate: far from the gizmo hits nothing" );
		C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Scale, RayThrough( Camera, View, OnAxis( Frame, 0, 0.9f ) ) ) == EEditorGizmoHandle::AxisX, "scale: the X axis" );
		C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Scale, RayThrough( Camera, View, ( [&]() { FVector3 Corners[4]; NWorldEditor::GetGizmoPlaneCorners( Frame, 2, Corners ); return ( Corners[0] + Corners[2] ) * 0.5f; } )() ) ) != GetGizmoPlaneHandle( 2 ), "scale has no plane handles" );

		// 当たり: 回転（カメラ側の輪の点）
		bool bRings = true;
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			const FVector3 U = Frame.Axes[( Axis + 1 ) % 3], V = Frame.Axes[( Axis + 2 ) % 3];
			FVector3 Best = FVector3::Zero;
			float32 BestFacing = -1.0e9f;
			for ( int32 i = 0; i < 72; ++i )
			{
				const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / 72.0f;
				const FVector3 Point = ( U * std::cos( Angle ) + V * std::sin( Angle ) ) * NWorldEditor::GetGizmoRingRadius( Frame );
				const float32 Facing = FVector3::Dot( Point, Camera.GetEye() );
				if ( Facing > BestFacing )
				{
					BestFacing = Facing;
					Best = Point;
				}
			}
			if ( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Rotate, RayThrough( Camera, View, Best ) ) != GetGizmoAxisHandle( Axis ) ) bRings = false;
		}
		C.Check( bRings, "rotate: the camera-facing point of each ring hits that ring" );
		C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Rotate, RayThrough( Camera, View, FVector3::Zero ) ) == EEditorGizmoHandle::None, "rotate: the centre of the rings hits nothing" );
		// 向こう側の輪は選ばない
		{
			const FVector3 U = Frame.Axes[1], V = Frame.Axes[2];   // X 軸の輪
			FVector3 Back = FVector3::Zero;
			float32 Worst = 1.0e9f;
			for ( int32 i = 0; i < 72; ++i )
			{
				const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / 72.0f;
				const FVector3 Point = ( U * std::cos( Angle ) + V * std::sin( Angle ) ) * NWorldEditor::GetGizmoRingRadius( Frame );
				const float32 Facing = FVector3::Dot( Point, Camera.GetEye() );
				if ( Facing < Worst )
				{
					Worst = Facing;
					Back = Point;
				}
			}
			C.Check( NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Rotate, RayThrough( Camera, View, Back ) ) != EEditorGizmoHandle::AxisX, "rotate: the far side of a ring (behind the centre) is not picked" );
		}

		// ドラッグ: 軸に沿った移動
		FEditorGizmoDrag Drag;
		const FVector3 Grab = OnAxis( Frame, 0, 0.5f );
		C.Check( Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::AxisX, RayThrough( Camera, View, Grab ), Camera ) && Drag.IsActive(), "a translate drag begins on the X axis" );
		FEditorGizmoDelta Delta = Drag.Update( RayThrough( Camera, View, Grab + FVector3( 2, 0, 0 ) ) );
		C.Check( Near3( Delta.Translation, FVector3( 2, 0, 0 ), 2e-3f ), "dragging 2 units along the axis moves by (2,0,0) (%s)", *Delta.Translation.ToString() );
		Delta = Drag.Update( RayThrough( Camera, View, Grab + FVector3( 2, 0.7f, -0.4f ) ) );
		C.Check( NMath::Abs( Delta.Translation.y ) < 1e-5f && NMath::Abs( Delta.Translation.z ) < 1e-5f && Delta.Translation.x > 0.5f, "a mouse off the axis still moves only along it" );
		Delta = Drag.Update( RayThrough( Camera, View, Grab - FVector3( 3, 0, 0 ) ) );
		C.Check( Near3( Delta.Translation, FVector3( -3, 0, 0 ), 3e-3f ), "and backwards" );
		Delta = Drag.Update( RayThrough( Camera, View, Grab + FVector3( 1.3f, 0, 0 ) ), 0.5f );
		C.Check( Near3( Delta.Translation, FVector3( 1.5f, 0, 0 ), 3e-3f ), "with a 0.5 snap, 1.3 becomes 1.5 (%s)", *Delta.Translation.ToString() );
		Delta = Drag.Update( RayThrough( Camera, View, Grab + FVector3( 1.1f, 0, 0 ) ), 0.5f );
		C.Check( Near3( Delta.Translation, FVector3( 1.0f, 0, 0 ), 3e-3f ), "and 1.1 becomes 1.0" );
		const FEditorGizmoDelta Held = Drag.Update( MakeRay( Grab - Frame.Axes[0] * 5.0f + FVector3( 0, 0, 0 ), Frame.Axes[0] ), 0.0f );
		C.Check( Near3( Held.Translation, Delta.Translation, 1e-5f ), "a ray parallel to the axis keeps the last value instead of jumping" );
		Drag.End();
		C.Check( !Drag.IsActive() && !Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::AxisX, MakeRay( FVector3( -20, 0, 0 ), Frame.Axes[0] ), Camera ), "a drag cannot begin with the ray parallel to the axis" );
		C.Check( !Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::None, RayThrough( Camera, View, Grab ), Camera ), "or on no handle" );
		// ほぼ平行（3 度ほどずれた向き）でも、つかまない（軸の上の位置が、わずかなずれで大きく動いてしまうため）。少し離れれば、つかめる
		{
			const FVector3 Nearly = ( Frame.Axes[0] + FVector3( 0.0f, 0.05f, 0.0f ) ).Normalized();
			C.Check( !Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::AxisX, MakeRay( FVector3( -20, 0.1f, 0 ), Nearly ), Camera ), "a drag cannot begin with a ray only 3 degrees off the axis (the position along it would be unstable)" );
			const FVector3 Looser = ( Frame.Axes[0] + FVector3( 0.0f, 0.3f, 0.0f ) ).Normalized();
			C.Check( Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::AxisX, MakeRay( FVector3( -20, 0.1f, 0 ), Looser ), Camera ), "but it can with a ray 17 degrees off" );
			Drag.End();
		}

		// 面の中の移動
		FVector3 PlaneCorners[4];
		NWorldEditor::GetGizmoPlaneCorners( Frame, 1, PlaneCorners );   // 法線 Y: XZ 面
		const FVector3 PlaneGrab = ( PlaneCorners[0] + PlaneCorners[2] ) * 0.5f;
		Drag = FEditorGizmoDrag();
		C.Check( Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::PlaneXZ, RayThrough( Camera, View, PlaneGrab ), Camera ), "a plane drag begins" );
		Delta = Drag.Update( RayThrough( Camera, View, PlaneGrab + FVector3( 1, 0, 2 ) ) );
		C.Check( Near3( Delta.Translation, FVector3( 1, 0, 2 ), 3e-3f ), "dragging in the XZ plane by (1, 2) moves by (1,0,2), never in Y (%s)", *Delta.Translation.ToString() );
		Delta = Drag.Update( RayThrough( Camera, View, PlaneGrab + FVector3( 1.3f, 0, 2.2f ) ), 0.5f );
		C.Check( Near3( Delta.Translation, FVector3( 1.5f, 0, 2.0f ), 3e-3f ), "with a snap, each in-plane axis is rounded" );

		// 画面に平行な移動
		Drag = FEditorGizmoDrag();
		C.Check( Drag.Begin( Frame, EEditorGizmoMode::Translate, EEditorGizmoHandle::Free, RayThrough( Camera, View, FVector3::Zero ), Camera ), "a free drag begins at the centre" );
		Delta = Drag.Update( RayThrough( Camera, View, Camera.GetRight() * 1.5f + Camera.GetUp() * 0.5f ) );
		C.Check( Near3( Delta.Translation, Camera.GetRight() * 1.5f + Camera.GetUp() * 0.5f, 3e-3f ), "the free handle moves parallel to the screen" );

		// 回転: 回すと、つかんだ点がマウスに付いてくる
		Drag = FEditorGizmoDrag();
		const FVector3 RingStart = Frame.Axes[2] * NWorldEditor::GetGizmoRingRadius( Frame );   // Y 軸の輪（XZ 面）の Z 軸上の点
		// Y 軸の輪の上の、カメラ側にある点を使う: 視線に近い向きの点
		FVector3 V0 = FVector3::Zero;
		float32 Facing0 = -1.0e9f;
		for ( int32 i = 0; i < 72; ++i )
		{
			const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / 72.0f;
			const FVector3 Point = ( Frame.Axes[2] * std::cos( Angle ) + Frame.Axes[0] * std::sin( Angle ) ) * NWorldEditor::GetGizmoRingRadius( Frame );
			const float32 Facing = FVector3::Dot( Point, Camera.GetEye() );
			if ( Facing > Facing0 )
			{
				Facing0 = Facing;
				V0 = Point;
			}
		}
		(void)RingStart;
		C.Check( Drag.Begin( Frame, EEditorGizmoMode::Rotate, EEditorGizmoHandle::AxisY, RayThrough( Camera, View, V0 ), Camera ), "a rotate drag begins on the Y ring" );
		const float32 R = NWorldEditor::GetGizmoRingRadius( Frame );
		const auto OnRing = [&]( float32 AngleFromStart )
		{
			// V0 を Y 軸まわりに回した点（外積で作る。回す向きの約束に依らない）
			const FVector3 Perp = FVector3::Cross( FVector3::Up, V0 ).Normalized();
			return V0 * std::cos( AngleFromStart ) + Perp * ( R * std::sin( AngleFromStart ) );
		};
		Delta = Drag.Update( RayThrough( Camera, View, OnRing( 0.5f ) ) );
		C.Check( NMath::Abs( NMath::Abs( Delta.AngleRadians ) - 0.5f ) < 5e-3f, "moving 0.5 rad around the ring reports 0.5 rad (%.4f)", Delta.AngleRadians );
		// 回転の向き: つかんだ点を回すと、いまの点へ向かう
		const FVector3 Target0 = OnRing( 0.5f );
		const FVector3 Rotated0 = Delta.Rotation.Rotate( V0 );
		C.Check( Rotated0.Normalized().Equals( Target0.Normalized(), 0.02f ), "the delta rotation takes the grabbed point to the mouse point" );
		Delta = Drag.Update( RayThrough( Camera, View, OnRing( -0.8f ) ) );
		C.Check( NMath::Abs( Delta.AngleRadians - -0.8f ) < 5e-3f || NMath::Abs( Delta.AngleRadians + ( -0.8f ) ) < 5e-3f, "and the other way gives the opposite sign (%.4f)", Delta.AngleRadians );
		const float32 SignA = Delta.AngleRadians;
		Delta = Drag.Update( RayThrough( Camera, View, OnRing( 0.8f ) ) );
		C.Check( NMath::Abs( Delta.AngleRadians + SignA ) < 1e-2f, "+0.8 and -0.8 are exact opposites" );
		Delta = Drag.Update( RayThrough( Camera, View, OnRing( 0.52f ) ), 15.0f * NMath::DEG2RAD );
		C.Check( NMath::Abs( NMath::Abs( Delta.AngleRadians ) - 30.0f * NMath::DEG2RAD ) < 1e-4f, "with a 15 degree snap, 0.52 rad (29.8 degrees) becomes 30 degrees (%.4f)", Delta.AngleRadians );

		// 回転: 1 周を超えても続けて数える
		Drag = FEditorGizmoDrag();
		Drag.Begin( Frame, EEditorGizmoMode::Rotate, EEditorGizmoHandle::AxisY, RayThrough( Camera, View, V0 ), Camera );
		float32 Last = 0.0f;
		for ( int32 Step = 1; Step <= 18; ++Step ) Last = Drag.Update( RayThrough( Camera, View, OnRing( 0.5f * static_cast<float32>( Step ) ) ) ).AngleRadians;
		C.Check( NMath::Abs( NMath::Abs( Last ) - 9.0f ) < 0.05f, "stepping 0.5 rad eighteen times accumulates 9 rad, past a full turn (%.3f)", Last );

		// 拡大
		Drag = FEditorGizmoDrag();
		const FVector3 ScaleGrab = OnAxis( Frame, 0, 0.5f );
		C.Check( Drag.Begin( Frame, EEditorGizmoMode::Scale, EEditorGizmoHandle::AxisX, RayThrough( Camera, View, ScaleGrab ), Camera ), "a scale drag begins on X" );
		Delta = Drag.Update( RayThrough( Camera, View, OnAxis( Frame, 0, 1.0f ) ) );
		C.Check( NMath::Abs( Delta.Scale.x - 1.5f ) < 5e-3f && Delta.Scale.y == 1.0f && Delta.Scale.z == 1.0f, "dragging by half the gizmo length scales X by 1.5 (%s)", *Delta.Scale.ToString() );
		Delta = Drag.Update( RayThrough( Camera, View, OnAxis( Frame, 0, 1.0f ) ), 0.25f );
		C.Check( NMath::Abs( Delta.Scale.x - 1.5f ) < 1e-4f, "snapped to 0.25 steps it is exactly 1.5" );
		Delta = Drag.Update( RayThrough( Camera, View, OnAxis( Frame, 0, -3.0f ) ) );
		C.Check( Delta.Scale.x == 0.01f, "dragging far below zero stops at a small positive factor (%.4f)", Delta.Scale.x );
		// 奥向きで、手前へ反転して出している Z 軸: 表示している先の向き（外側）へ引くと大きくなる
		Drag = FEditorGizmoDrag();
		C.Check( Frame.Flip[2] == -1.0f && Drag.Begin( Frame, EEditorGizmoMode::Scale, EEditorGizmoHandle::AxisZ, RayThrough( Camera, View, OnAxis( Frame, 2, 0.5f ) ), Camera ), "a scale drag begins on the flipped Z axis" );
		Delta = Drag.Update( RayThrough( Camera, View, OnAxis( Frame, 2, 1.0f ) ) );
		C.Check( NMath::Abs( Delta.Scale.z - 1.5f ) < 5e-3f && Delta.Scale.x == 1.0f && Delta.Scale.y == 1.0f, "pulling the flipped Z handle outward (toward the camera) scales Z by 1.5, not 0.5 (%.3f)", Delta.Scale.z );
		Delta = Drag.Update( RayThrough( Camera, View, OnAxis( Frame, 2, 0.0f ) ) );
		C.Check( NMath::Abs( Delta.Scale.z - 0.5f ) < 5e-3f, "and pushing it back to the centre halves it (%.3f)", Delta.Scale.z );
		Drag = FEditorGizmoDrag();
		Drag.Begin( Frame, EEditorGizmoMode::Scale, EEditorGizmoHandle::Free, RayThrough( Camera, View, FVector3::Zero ), Camera );
		const FVector3 Diagonal = ( Camera.GetRight() + Camera.GetUp() ).Normalized();
		Delta = Drag.Update( RayThrough( Camera, View, Diagonal * ( Frame.Length * 0.5f ) ) );
		C.Check( NMath::Abs( Delta.Scale.x - 1.5f ) < 5e-3f && Delta.Scale.x == Delta.Scale.y && Delta.Scale.y == Delta.Scale.z, "uniform scale: up-right by half the gizmo length is 1.5 on all axes (%s)", *Delta.Scale.ToString() );
		Delta = Drag.Update( RayThrough( Camera, View, -Diagonal * ( Frame.Length * 0.25f ) ) );
		C.Check( NMath::Abs( Delta.Scale.x - 0.75f ) < 5e-3f, "and down-left shrinks it" );

		// 適用
		FTransform Start( FVector3( 2, 0, 0 ), FQuaternion::Identity, FVector3::One );
		FEditorGizmoDelta Move;
		Move.Translation = FVector3( 1, 2, 3 );
		C.Check( Near3( NWorldEditor::ApplyGizmoDelta( Start, Move, EEditorGizmoMode::Translate, Frame ).Position, FVector3( 3, 2, 3 ) ), "apply: translate adds" );
		FEditorGizmoDelta Turn;
		Turn.Rotation = FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI );
		Turn.AngleRadians = NMath::HALF_PI;
		FEditorGizmoFrame AtPivot = Frame;
		AtPivot.Origin = FVector3( 1, 0, 0 );
		Start.Rotation = FQuaternion::FromAxisAngle( FVector3::Forward, 0.3f );
		const FTransform Turned2 = NWorldEditor::ApplyGizmoDelta( Start, Turn, EEditorGizmoMode::Rotate, AtPivot );
		C.Check( Near3( Turned2.Position, AtPivot.Origin + Turn.Rotation.Rotate( FVector3( 1, 0, 0 ) ) ) && NMath::Abs( FVector3::Distance( Turned2.Position, AtPivot.Origin ) - 1.0f ) < 1e-4f, "apply: rotate orbits the position around the gizmo origin, keeping the distance" );
		C.Check( Near3( Turned2.Rotation.Rotate( FVector3::Forward ), Turn.Rotation.Rotate( Start.Rotation.Rotate( FVector3::Forward ) ), 1e-4f ), "apply: and turns the object's own orientation by the world rotation" );
		FTransform Skewed( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Up, NMath::HALF_PI ), FVector3( 1, 2, 3 ) );
		FEditorGizmoDelta Stretch;
		Stretch.Scale = FVector3( 2, 1, 1 );
		FEditorGizmoFrame LocalFrame = Frame;
		LocalFrame.Axes[0] = Skewed.Rotation.Rotate( FVector3::Right );
		LocalFrame.Axes[1] = Skewed.Rotation.Rotate( FVector3::Up );
		LocalFrame.Axes[2] = Skewed.Rotation.Rotate( FVector3::Forward );
		C.Check( Near3( NWorldEditor::ApplyGizmoDelta( Skewed, Stretch, EEditorGizmoMode::Scale, LocalFrame ).Scale, FVector3( 2, 2, 3 ) ), "apply: scale in a local frame multiplies that local axis only" );
		C.Check( Near3( NWorldEditor::ApplyGizmoDelta( Skewed, Stretch, EEditorGizmoMode::Scale, Frame ).Scale, FVector3( 1, 2, 6 ) ), "apply: scale along the world X of an object turned 90 degrees stretches its local Z" );
		FEditorGizmoDelta Uniform;
		Uniform.Scale = FVector3( 1.5f, 1.5f, 1.5f );
		FTransform Tilted( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Up, NMath::PI * 0.25f ), FVector3( 1, 2, 3 ) );
		C.Check( Near3( NWorldEditor::ApplyGizmoDelta( Tilted, Uniform, EEditorGizmoMode::Scale, Frame ).Scale, FVector3( 1.5f, 3, 4.5f ) ), "apply: a uniform scale is exactly uniform for any orientation" );
	}
}

namespace
{
	const FEditorShape* FindShape( const TArray<FEditorShape>& Shapes, EEditorShapeRole Role )
	{
		for ( const FEditorShape& Shape : Shapes )
		{
			if ( Shape.Role == Role ) return &Shape;
		}
		return nullptr;
	}

	void TestMeshShapes( NAssetTest::FChecker& C )
	{
		C.Group = "editor-mesh-collider";
		FFakeEditorAssets Assets;
		Assets.Models.Add( { "Models/Box.mv1", FVector3( -1, 0, -1 ), FVector3( 1, 2, 1 ) } );
		Assets.Models.Add( { "Models/Off.mv1", FVector3( 0, 0, 0 ), FVector3( 4, 2, 2 ) } );
		FEditorShapeContext Context;
		Context.Assets = &Assets;

		// メッシュの当たり判定の図形は、三角形を取るモデルの大きさの箱（選ぶ・囲むため）。Offset は使わない
		FWorldObject Floor = MakeShapeObject( "Floor" );
		FWorldModel Spec;
		Spec.Path = "Models/Box.mv1";
		Floor.Model = Spec;
		FWorldCollider Mesh;
		Mesh.Shape = EWorldColliderShape::Mesh;
		Mesh.Offset = FVector3( 0, 5, 0 );
		Floor.Collider = Mesh;
		const FTransform At( FVector3( 10, 0, 0 ), FQuaternion::Identity, FVector3( 2, 2, 2 ) );
		TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Floor, At, Context );
		const FEditorShape* Box = FindShape( Shapes, EEditorShapeRole::Collider );
		C.Check( Shapes.Num() == 2 && Box != nullptr && Box->Kind == EEditorShapeKind::Box && !Box->bApproximate && Near3( Box->Center, FVector3( 10, 2, 0 ) ) && Near3( Box->Half, FVector3( 2, 2, 2 ) ),
				 "a mesh collider is the box of the model it takes its triangles from (the Offset is not used)" );

		Floor.Collider->Mesh = "Models/Off.mv1";
		Shapes = NWorldEditor::GatherShapes( Floor, At, Context );
		Box = FindShape( Shapes, EEditorShapeRole::Collider );
		C.Check( Box != nullptr && !Box->bApproximate && Near3( Box->Center, At.TransformPoint( FVector3( 2, 1, 1 ) ) ) && Near3( Box->Half, FVector3( 4, 2, 2 ) ), "with its own file, the box is that file's bounds" );

		Floor.Collider->Mesh = "Models/Missing.mv1";
		Shapes = NWorldEditor::GatherShapes( Floor, FTransform::Identity, Context );
		Box = FindShape( Shapes, EEditorShapeRole::Collider );
		C.Check( Box != nullptr && Box->bApproximate && Near3( Box->Half, FVector3( 0.5f, 0.5f, 0.5f ) ), "a file whose bounds are unknown is a flagged 1 m box" );

		FWorldObject NoModel = MakeShapeObject( "NoModel" );
		NoModel.Collider = Mesh;
		Shapes = NWorldEditor::GatherShapes( NoModel, FTransform::Identity, Context );
		Box = FindShape( Shapes, EEditorShapeRole::Collider );
		C.Check( Shapes.Num() == 1 && Box != nullptr && Box->bApproximate, "and so is a mesh collider with no model at all" );

		// 三角形の枠線: 共有している辺は 1 本
		FEditorCollisionMesh Quad;
		Quad.Vertices.Add( FVector3( 0, 0, 0 ) );
		Quad.Vertices.Add( FVector3( 1, 0, 0 ) );
		Quad.Vertices.Add( FVector3( 1, 0, 1 ) );
		Quad.Vertices.Add( FVector3( 0, 0, 1 ) );
		for ( const uint32 Index : { 0u, 1u, 2u, 0u, 2u, 3u } ) Quad.Indices.Add( Index );
		C.Check( Quad.GetNumTriangles() == 2, "a quad is 2 triangles" );
		TArray<FVector3> Lines;
		C.Check( NWorldEditor::AppendMeshWireLines( Quad, FTransform::Identity, 100, Lines ) && Lines.Num() == 10, "two triangles sharing an edge: 5 edges (10 points), all drawn (%d)", Lines.Num() );
		Lines.Reset();
		C.Check( NWorldEditor::AppendMeshWireLines( Quad, FTransform( FVector3( 1, 2, 3 ) ), 100, Lines ) && Lines.Num() == 10 && Near3( Lines[0], FVector3( 1, 2, 3 ) ) && Near3( Lines[1], FVector3( 2, 2, 3 ) ), "the lines are moved by the transform" );
		Lines.Reset();
		C.Check( !NWorldEditor::AppendMeshWireLines( Quad, FTransform::Identity, 1, Lines ) && Lines.Num() == 6, "with a limit of 1 triangle: its 3 edges, and false because the rest was left out (%d)", Lines.Num() );
		Lines.Reset();
		C.Check( !NWorldEditor::AppendMeshWireLines( Quad, FTransform::Identity, 0, Lines ) && Lines.IsEmpty() && !NWorldEditor::AppendMeshWireLines( Quad, FTransform::Identity, -5, Lines ) && Lines.IsEmpty(), "a limit of 0 (or less) draws nothing" );
		FEditorCollisionMesh Empty;
		C.Check( NWorldEditor::AppendMeshWireLines( Empty, FTransform::Identity, 10, Lines ) && NWorldEditor::AppendMeshWireLines( Empty, FTransform::Identity, -1, Lines ) && Lines.IsEmpty(), "an empty mesh draws nothing, and nothing was left out (whatever the limit)" );
		FEditorCollisionMesh Broken = Quad;
		Broken.Indices.Reset();
		for ( const uint32 Index : { 0u, 1u, 99u } ) Broken.Indices.Add( Index );   // 頂点の数を超えた番号
		Lines.Reset();
		C.Check( NWorldEditor::AppendMeshWireLines( Broken, FTransform::Identity, 10, Lines ) && Lines.Num() == 6, "an index past the last vertex does not crash (%d)", Lines.Num() );

		// 編集メッシュ（Mesh 部品）: 見える・選べる箱は、メッシュの大きさ（拡大つき）。メッシュの当たり判定も、その箱
		FWorldObject Hill = MakeShapeObject( "Hill" );
		Hill.Mesh = NEditMesh::MakeBox( FVector3( 2, 4, 6 ) );
		const FTransform HillAt( FVector3( 10, 0, 0 ), FQuaternion::Identity, FVector3( 2, 1, 1 ) );
		Shapes = NWorldEditor::GatherShapes( Hill, HillAt, Context );
		const FEditorShape* HillBox = FindShape( Shapes, EEditorShapeRole::Model );
		C.Check( Shapes.Num() == 1 && HillBox != nullptr && HillBox->Kind == EEditorShapeKind::Box && !HillBox->bApproximate && Near3( HillBox->Center, FVector3( 10, 2, 0 ) ) && Near3( HillBox->Half, FVector3( 2, 2, 3 ) ), "a Mesh component is a box round its faces (its size, scaled), not a flagged guess" );
		FWorldCollider HillCollider;
		HillCollider.Shape = EWorldColliderShape::Mesh;
		Hill.Collider = HillCollider;
		Shapes = NWorldEditor::GatherShapes( Hill, HillAt, Context );
		const FEditorShape* HillBody = FindShape( Shapes, EEditorShapeRole::Collider );
		C.Check( Shapes.Num() == 2 && HillBody != nullptr && !HillBody->bApproximate && Near3( HillBody->Center, FVector3( 10, 2, 0 ) ) && Near3( HillBody->Half, FVector3( 2, 2, 3 ) ), "a mesh collider over a Mesh component is the same box (not the model's, not a guess)" );

		// 編集メッシュの枠線: 共有している辺は 1 本
		const FEditMesh Slab = NEditMesh::MakeBox( FVector3::One );
		Lines.Reset();
		C.Check( NWorldEditor::AppendEditMeshWireLines( Slab, FTransform::Identity, 100, Lines ) && Lines.Num() == 24, "a box: 12 edges (24 points), each shared edge once (%d)", Lines.Num() );
		Lines.Reset();
		NWorldEditor::AppendEditMeshWireLines( Slab, FTransform( FVector3( 1, 2, 3 ) ), 100, Lines );
		float32 LeastX = 1.0e9f, MostY = -1.0e9f;
		for ( const FVector3& P : Lines )
		{
			LeastX = NMath::Min( LeastX, P.x );
			MostY = NMath::Max( MostY, P.y );
		}
		C.Check( NMath::Abs( LeastX - 0.5f ) < 1.0e-3f && NMath::Abs( MostY - 3.0f ) < 1.0e-3f, "the lines are moved by the transform (the box x -0.5..0.5, y 0..1 moved by (1, 2, 3))" );
		Lines.Reset();
		C.Check( !NWorldEditor::AppendEditMeshWireLines( Slab, FTransform::Identity, 1, Lines ) && Lines.Num() == 8, "with a limit of 1 face: its 4 edges, and false because the rest was left out (%d)", Lines.Num() );
		Lines.Reset();
		C.Check( !NWorldEditor::AppendEditMeshWireLines( Slab, FTransform::Identity, 0, Lines ) && Lines.IsEmpty() && !NWorldEditor::AppendEditMeshWireLines( Slab, FTransform::Identity, -3, Lines ) && Lines.IsEmpty(), "a limit of 0 (or less) draws nothing" );
		const FEditMesh NoFaces;
		C.Check( NWorldEditor::AppendEditMeshWireLines( NoFaces, FTransform::Identity, 5, Lines ) && Lines.IsEmpty(), "a mesh with no faces draws nothing, and nothing was left out" );

		// 見せ方の分類
		FWorldSettings Settings;
		FWorldCollisionRule Rule;
		Rule.Tag = "Decoration";
		Rule.bCollide = false;
		Settings.CollisionRules.Add( Rule );
		const auto Make = [&]( bool bCollider, bool bTrigger, bool bDecoration, EWorldCollisionMode Mode )
		{
			FWorldObject Object = MakeShapeObject( "o" );
			if ( bCollider )
			{
				FWorldCollider Collider;
				Collider.bTrigger = bTrigger;
				Object.Collider = Collider;
			}
			if ( bDecoration ) Object.AddTag( "Decoration" );
			Object.Collision = Mode;
			return Object;
		};
		const auto Display = [&]( const FWorldObject& Object ) { return NWorldEditor::ClassifyColliderDisplay( Settings, Object ); };
		C.Check( Display( Make( true, false, false, EWorldCollisionMode::Auto ) ) == EEditorColliderDisplay::Solid, "display: collides by default" );
		C.Check( Display( Make( true, false, true, EWorldCollisionMode::Auto ) ) == EEditorColliderDisplay::Off, "off by a tag rule" );
		C.Check( Display( Make( true, false, true, EWorldCollisionMode::On ) ) == EEditorColliderDisplay::SolidForced, "forced on by the object" );
		C.Check( Display( Make( true, false, false, EWorldCollisionMode::Off ) ) == EEditorColliderDisplay::OffForced, "forced off by the object" );
		C.Check( Display( Make( true, true, false, EWorldCollisionMode::Auto ) ) == EEditorColliderDisplay::Trigger && Display( Make( true, true, false, EWorldCollisionMode::On ) ) == EEditorColliderDisplay::Trigger, "a trigger that is on is a trigger, however it was turned on" );
		C.Check( Display( Make( true, true, true, EWorldCollisionMode::Auto ) ) == EEditorColliderDisplay::Off && Display( Make( true, true, false, EWorldCollisionMode::Off ) ) == EEditorColliderDisplay::OffForced, "a trigger that is off is shown as off" );
		C.Check( Display( Make( false, false, false, EWorldCollisionMode::On ) ) == EEditorColliderDisplay::Off, "no collider shape: off" );
		bool bLabelsDiffer = true;
		for ( const EEditorColliderDisplay A : { EEditorColliderDisplay::Solid, EEditorColliderDisplay::SolidForced, EEditorColliderDisplay::Trigger, EEditorColliderDisplay::Off, EEditorColliderDisplay::OffForced } )
		{
			for ( const EEditorColliderDisplay B : { EEditorColliderDisplay::Solid, EEditorColliderDisplay::SolidForced, EEditorColliderDisplay::Trigger, EEditorColliderDisplay::Off, EEditorColliderDisplay::OffForced } )
			{
				if ( A != B && FString( NWorldEditor::GetColliderDisplayLabel( A ) ) == FString( NWorldEditor::GetColliderDisplayLabel( B ) ) ) bLabelsDiffer = false;
			}
			if ( FString( NWorldEditor::GetColliderDisplayLabel( A ) ).IsEmpty() ) bLabelsDiffer = false;
		}
		C.Check( bLabelsDiffer, "every display has its own label" );

		// モデルの箱に合わせる
		const FVector3 Lo( -1, 0, -1 ), Hi( 1, 2, 3 );
		FWorldCollider Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Box, Lo, Hi );
		C.Check( Fit.Shape == EWorldColliderShape::Box && Near3( Fit.Size, FVector3( 2, 2, 4 ) ) && Near3( Fit.Offset, FVector3( 0, 1, 1 ) ), "fit a box: the bounds' size, centred on them" );
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Sphere, Lo, Hi );
		C.Check( Fit.Shape == EWorldColliderShape::Sphere && NMath::Abs( Fit.Radius - 2.0f ) < 1e-5f && Near3( Fit.Offset, FVector3( 0, 1, 1 ) ), "fit a sphere: half of the longest side" );
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Capsule, Lo, Hi );
		C.Check( Fit.Shape == EWorldColliderShape::Capsule && NMath::Abs( Fit.Radius - 2.0f ) < 1e-5f && NMath::Abs( Fit.Height - 4.0f ) < 1e-5f && Near3( Fit.Offset, FVector3( 0, 1, 1 ) ),
				 "fit a capsule: the radius from the wider of X and Z, and a height of at least 2 radii (%.3f, %.3f)", Fit.Radius, Fit.Height );
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Capsule, FVector3( 0, 0, 0 ), FVector3( 2, 1, 2 ) );
		C.Check( NMath::Abs( Fit.Radius - 1.0f ) < 1e-5f && NMath::Abs( Fit.Height - 2.0f ) < 1e-5f, "a capsule over a flat model is as high as 2 radii, not lower (%.3f)", Fit.Height );
		FWorldCollider Base;
		Base.bTrigger = true;
		Base.Mesh = "Models/Keep.mv1";
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Mesh, Lo, Hi, Base );
		C.Check( Fit.Shape == EWorldColliderShape::Mesh && Fit.bTrigger && Fit.Mesh == "Models/Keep.mv1", "fitting to a mesh changes the shape only; the trigger flag and the file stay" );
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Box, FVector3( 1, 1, 1 ), FVector3( 1, 1, 1 ), Base );
		C.Check( Fit.bTrigger && Fit.Size.x > 0.0f && Fit.Size.y > 0.0f && Fit.Size.z > 0.0f, "an empty bounds still gives a valid (non-zero) box, and keeps the trigger flag" );
		Fit = NWorldEditor::FitColliderToBounds( EWorldColliderShape::Sphere, FVector3( 1, 1, 1 ), FVector3( 1, 1, 1 ) );
		C.Check( Fit.Radius > 0.0f, "and a valid sphere" );
	}
}

void NWorldTest::RunEditorMathTests( NAssetTest::FChecker& C )
{
	TestGeometry( C );
	TestCamera( C );
	TestShapes( C );
	TestPicking( C );
	TestGizmo( C );
	TestMeshShapes( C );
}
