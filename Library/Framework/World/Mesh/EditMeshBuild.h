#pragma once
#include "Library/Core/Interfaces/Render/MeshTypes.h"
#include "Library/Framework/World/Mesh/EditMesh.h"

//=============================================================================
// 編集メッシュを、描く形にする（純粋な計算。DxLib を使わない）
//   面の角ごとに頂点を作る（位置・法線・画像の座標・色。色は、面の材質の色）。多角形は、先頭の頂点を中心にした扇形の三角形にする。
//   三角形は、材質ごとにまとめて並べる（1 つの材質 = 1 回の描画）。使えない面・面積のない面は、出さない。
//
//   法線: 面ごとに平ら。SmoothAngle が 0 より大きいときは、頂点を共有する面のうち、法線が SmoothAngle 度以内の面の法線を、面積で重みを付けて混ぜる。
//   画像の座標: 面の法線でいちばん大きい軸に沿った平面に、ローカルの座標を落とす（箱の貼り付け）。材質の Tile（長さ）と Offset で決まる。
//=============================================================================
struct FEditMeshBatch
{
	int32 Material = -1;     // FEditMesh::Materials の番号。範囲外の材質（既定）は -1
	int32 StartIndex = 0;    // Indices の何番目から
	int32 NumIndices = 0;    // 何個（3 の倍数）
};

struct FEditMeshBuild
{
	TArray<FMeshVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FEditMeshBatch> Batches;   // 材質の番号の小さい順（既定の材質 = -1 が先頭）。三角形のある材質だけ

	int32 GetNumTriangles() const { return Indices.Num() / 3; }
};

namespace NEditMesh
{
	FEditMeshBuild BuildRenderMesh( const FEditMesh& Mesh );

	// ローカルの位置の、箱の貼り付けでの画像の座標（左上が 0, 0。右が U、下が V の向き）。Normal は面の法線（長さは問わない）
	FVector2 ComputeBoxUV( const FVector3& Position, const FVector3& Normal, const FEditMeshMaterial& Material );
}
