#pragma once
#include "Library/Math/Quaternion.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// エディタ（ピッキング・ギズモ）が使う、光線と図形の交差の計算
//   純粋な関数（DxLib・ImGui を使わない）。座標は左手系・Y 上（Library/Math と同じ）。
//
//   図形（箱・球・カプセル）は、「光線の始点が図形の中にあるときは当たらない」。
//   カメラが大きな部屋や広い範囲のトリガーの中にあるとき、いつもそれが選ばれてしまわないように。
//   戻り値 true のとき OutT は、光線の始点から当たる点までの長さ（Direction が単位ベクトルなら距離）。
//=============================================================================
struct FEditorRay
{
	FVector3 Origin = FVector3::Zero;
	FVector3 Direction = FVector3::Forward;   // 長さ 1

	FVector3 PointAt( float32 T ) const { return Origin + Direction * T; }
};

namespace NWorldEditor
{
	// 軸に平行な箱（Min..Max）。OutNormal には、当たった面の外向きの法線が入る（省略できる）
	bool RayVsBox( const FEditorRay& Ray, const FVector3& Min, const FVector3& Max, float32& OutT, FVector3* OutNormal = nullptr );

	// 回転した箱（中心・回転・各軸の半分の長さ）
	bool RayVsOrientedBox( const FEditorRay& Ray, const FVector3& Center, const FQuaternion& Rotation, const FVector3& HalfExtents, float32& OutT, FVector3* OutNormal = nullptr );

	bool RayVsSphere( const FEditorRay& Ray, const FVector3& Center, float32 Radius, float32& OutT );

	// カプセル（線分 A-B から Radius 以内）
	bool RayVsCapsule( const FEditorRay& Ray, const FVector3& A, const FVector3& B, float32 Radius, float32& OutT );

	// 平面（両面。光線の後ろにある平面や、平行な平面は false）
	bool RayVsPlane( const FEditorRay& Ray, const FVector3& PlanePoint, const FVector3& PlaneNormal, float32& OutT );

	// 四角形（Center を中心に、HalfRight・HalfUp の向きと半分の長さ。両面）
	bool RayVsQuad( const FEditorRay& Ray, const FVector3& Center, const FVector3& HalfRight, const FVector3& HalfUp, float32& OutT );

	// 2 本の直線 P1 + D1 * T1 と P2 + D2 * T2 の、いちばん近い点の T1 / T2。ほぼ平行なら false
	bool ClosestPointsOnLines( const FVector3& P1, const FVector3& D1, const FVector3& P2, const FVector3& D2, float32& OutT1, float32& OutT2 );

	// 光線（始点より前だけ）と線分 A-B の、いちばん近い距離。OutRayT は光線の上、OutSegmentS は線分の上（0..1）の、いちばん近い点の位置
	float32 DistanceRayToSegment( const FEditorRay& Ray, const FVector3& A, const FVector3& B, float32& OutRayT, float32& OutSegmentS );
}
