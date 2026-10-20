#pragma once
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Framework/World/Editor/EditorGeometry.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"

//=============================================================================
// 編集メッシュの、頂点・辺・面を選ぶ（クリック・矩形）
//   純粋な計算（DxLib・ImGui を使わない）。画面の位置（ビューポートのピクセル）から、メッシュのどの要素かを求める。
//   頂点・辺は、画面での近さ（ピクセル）で選ぶ（遠くの小さな頂点も選べる）。面は、光線が当たる、手前の面。
//   手前の面に隠れた頂点・辺は、既定では選ばない（bOnlyVisible）。隠れているかは、視点から、頂点・辺の真ん中・面の中心への線が、ほかの面を通るかで決める。
//=============================================================================
struct FEditMeshPickOptions
{
	float32 VertexRadius = 9.0f;   // 頂点を選べる、画面での距離（ピクセル）
	float32 EdgeRadius = 6.0f;     // 辺を選べる、画面での距離（ピクセル）
	bool bOnlyVisible = true;      // 手前の面に隠れた頂点・辺は選ばない
};

struct FEditMeshHit
{
	bool bHit = false;
	NEditMesh::EEditMeshElement Element = NEditMesh::EEditMeshElement::Vertex;
	int32 Index = -1;         // 頂点・面の番号
	int64 Edge = 0;           // 辺（NEditMesh::MakeEdgeKey）
	float32 Distance = 0.0f;  // 頂点・辺: 画面での距離（ピクセル）。面: 視点からの距離
};

namespace NWorldEditor
{
	// 画面の (X, Y) で、いちばん近い Element の種類の要素。World はメッシュのローカル -> ワールド
	FEditMeshHit PickMeshElement( const FEditMesh& Mesh, const FTransform& World, const FEditorCamera& Camera, const FEditorViewport& Viewport, float32 X, float32 Y, NEditMesh::EEditMeshElement Element,
								  const FEditMeshPickOptions& Options = FEditMeshPickOptions() );

	// 矩形（ビューポートのピクセル。X0 > X1 でもよい）の中の要素: 頂点（画面の位置が中）、辺（両端が中）、面（中心が中）。結果は、その種類の集合だけが入った選択。
	// bOnlyVisible なら、隠れたもの（頂点・辺の真ん中・面の中心）は除く
	FEditMeshSelection FindMeshElementsInRect( const FEditMesh& Mesh, const FTransform& World, const FEditorCamera& Camera, const FEditorViewport& Viewport, float32 X0, float32 Y0, float32 X1, float32 Y1, NEditMesh::EEditMeshElement Element,
											   bool bOnlyVisible );

	// ワールドの点 Point が、視点 Eye から、メッシュの面に隠れずに見えるか（Point を使う面は、隠さない）
	bool IsMeshPointVisible( const FEditMesh& Mesh, const FTransform& World, const FVector3& Eye, const FVector3& Point );
}
