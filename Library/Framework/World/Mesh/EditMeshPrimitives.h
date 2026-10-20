#pragma once
#include "Library/Framework/World/Mesh/EditMesh.h"

//=============================================================================
// 編集メッシュの基本の形（箱・平面・円柱・球・斜面・階段）
//   どれも、底（y = 0）の中心が原点。面の表は外向き（EditMesh.h）。頂点は、面どうしで共有する（閉じた形は、すべての辺が 2 つの面に共有される。階段を除く）。
//   大きさは長さ（メートル）。引数が小さすぎる・ゼロ以下のときは、使える最小の値に直す（空のメッシュは返さない）。
//=============================================================================
enum class EEditMeshPrimitive : uint8
{
	Box,
	Plane,
	Cylinder,
	Sphere,
	Ramp,
	Stairs,
};

namespace NEditMesh
{
	const char* ToString( EEditMeshPrimitive Kind );
	constexpr int32 NumPrimitives = 6;

	FEditMesh MakeBox( const FVector3& Size );
	// y = 0 の、上向きの平らな面。X 方向・Z 方向に、それぞれ分ける
	FEditMesh MakePlane( float32 Width, float32 Depth, int32 DivisionsX = 1, int32 DivisionsZ = 1 );
	// Y 軸まわりの円柱。Segments は 3 以上。bCaps が false なら、上下の蓋がない（筒）
	FEditMesh MakeCylinder( float32 Radius, float32 Height, int32 Segments = 16, bool bCaps = true );
	// 中心が (0, Radius, 0) の球（底が y = 0）。Segments は 3 以上、Rings は 2 以上
	FEditMesh MakeSphere( float32 Radius, int32 Segments = 16, int32 Rings = 8 );
	// +Z 側が高い斜面（底は長方形、+X と -X は三角形、+Z は垂直の壁）
	FEditMesh MakeRamp( const FVector3& Size );
	// +Z に向かって上がる階段（中身の詰まった形）。Steps は 1 以上
	FEditMesh MakeStairs( const FVector3& Size, int32 Steps );

	// 種類ごとの、既定の大きさ（エディタの「作る」メニュー用）
	FEditMesh MakePrimitive( EEditMeshPrimitive Kind );
}
