#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// クリックと矩形でオブジェクトを選ぶ（ピッキング）
//   画面の光線と、EditorShapes.h の図形との交差で調べる。純粋な計算（DxLib・ImGui を使わない）。
//   1 つのオブジェクトは、いちばん近い図形で 1 回だけ数える。
//
//   const TArray<FEditorHit> Hits = NWorldEditor::PickObjects( World, Camera.ScreenToRay( View, X, Y ), Context );
//   if ( !Hits.IsEmpty() ) Select( Hits[0].ObjectId );          // いちばん手前
//   NWorldEditor::PickNext( Hits, CurrentId );                   // 同じ場所を続けてクリックしたとき、重なった次のもの
//=============================================================================
struct FEditorHit
{
	int32 ObjectId = WorldNoId;
	float32 Distance = 0.0f;   // 光線の始点から（Direction が単位ベクトルなら距離）
	EEditorShapeRole Role = EEditorShapeRole::Marker;
};

struct FEditorPickOptions
{
	bool bPickColliders = true;    // 当たり判定の形でも選ぶ
	bool bPickDisabled = true;     // 無効（bEnabled = false）のオブジェクトも選ぶ
};

namespace NWorldEditor
{
	bool RayVsShape( const FEditorRay& Ray, const FEditorShape& Shape, float32& OutT );

	// 光線に当たったオブジェクトを、近い順に（同じ距離なら、配列の順）
	TArray<FEditorHit> PickObjects( const FWorldDefinition& World, const FEditorRay& Ray, const FEditorShapeContext& Context, const FEditorPickOptions& Options = FEditorPickOptions() );

	// 重なっているものを順に選ぶ。CurrentId が Hits の中にあれば、その次（最後なら最初）、無ければ最初の ObjectId。Hits が空なら WorldNoId
	int32 PickNext( const TArray<FEditorHit>& Hits, int32 CurrentId );

	// 矩形（ビューポートのピクセル。X0 > X1 でもよい）の中に、図形の代表の点が入るオブジェクト（配列の順）
	TArray<int32> FindObjectsInRect( const FWorldDefinition& World, const FEditorShapeContext& Context, float32 X0, float32 Y0, float32 X1, float32 Y1,
									 const FEditorPickOptions& Options = FEditorPickOptions() );

	// 選んだものを囲む球（寄る・ギズモの位置に使う）。Ids の中に存在するものが無ければ false
	bool ComputeBoundingSphere( const FWorldDefinition& World, const TArray<int32>& Ids, const FEditorShapeContext& Context, FVector3& OutCenter, float32& OutRadius );
}
