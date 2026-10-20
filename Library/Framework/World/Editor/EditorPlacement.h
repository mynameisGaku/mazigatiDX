#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldEdit.h"

//=============================================================================
// アセットをワールドに置く（ブラウザから視界へドラッグして置く、メニューから作る）
//   どのアセットがどの部品になるか、置く場所の決め方、置いたオブジェクトの初期値。純粋な計算と編集の操作（ImGui を使わない）。
//
//     モデル（.mv1 / .fbx / .obj ...） -> Model 部品のオブジェクト
//     画像（.png / .jpg / .bmp / .tga / .dds） -> Billboard 部品のオブジェクト（縦 1、横は画像の比率）
//     音（.wav / .ogg / .mp3） -> Sound 部品のオブジェクト（環境音）
//     ワールド（.world） -> Prefab 部品のオブジェクト（入れ子のワールド）
//     エフェクト（.effect.json、と、登録されたバックエンドが扱うファイル） -> Effect 部品のオブジェクト
//
//   置く場所: 視界の光線が、地面（GroundY の水平面）か、すでにあるオブジェクトの表面に最初に当たる点。何にも当たらなければ、視点から一定の距離。
//=============================================================================
enum class EEditorAssetKind : uint8
{
	Unknown,
	Model,
	Texture,
	Sound,
	World,
	Effect,
};

struct FEditorPlacementOptions
{
	float32 GroundY = 0.0f;           // 地面の高さ
	bool bOnSurfaces = true;          // すでにあるオブジェクトの表面にも置く
	float32 SnapStep = 0.0f;          // > 0 なら、X と Z をこの刻みに丸める（高さは、当たった面のまま）
	float32 FallbackDistance = 8.0f;  // 何にも当たらないときの、視点からの距離
};

struct FEditorPlacement
{
	FVector3 Position = FVector3::Zero;
	bool bHitGround = false;
	bool bHitObject = false;
	int32 HitObjectId = WorldNoId;
};

namespace NWorldEditor
{
	// 拡張子で分類する（大文字小文字は区別しない）
	EEditorAssetKind ClassifyAsset( const FString& Path );

	// "Models/Torch.mv1" -> "Torch"（フォルダと拡張子を除く）
	FString GetAssetDisplayName( const FString& Path );

	// 光線の先の置く場所。World が nullptr なら、表面は見ない
	FEditorPlacement ComputePlacement( const FEditorRay& Ray, const FWorldDefinition* World, const FEditorShapeContext& Context, const FEditorPlacementOptions& Options = FEditorPlacementOptions() );

	// アセットのオブジェクトを足して、Id を返す（Unknown の種類・パスが空なら WorldNoId）。WorldPosition はワールドでの位置
	//   ParentId の下に置くときは、ワールドの位置が変わらないように、親の中のローカルの値にする。Info は画像の大きさ（ビルボードの比率）に使う（nullptr なら正方形）
	int32 AddAssetObject( TWorldDocument& Document, const FString& AssetPath, int32 ParentId, const FVector3& WorldPosition, const IEditorAssetInfo* Info );

	// 何も付いていない空のオブジェクト（タグを付けて、コードの生成物を結び付ける場所）
	int32 AddEmptyObject( TWorldDocument& Document, const FString& Name, int32 ParentId, const FVector3& WorldPosition );

	// 当たり判定の形だけのオブジェクト（壁・床・トリガー）
	int32 AddColliderObject( TWorldDocument& Document, EWorldColliderShape Shape, bool bTrigger, int32 ParentId, const FVector3& WorldPosition );

	// 編集メッシュのオブジェクト（基本の形を持つ Mesh 部品。bCollider なら、Mesh 部品の三角形を使う当たり判定もつける）。名前は、形の名前
	int32 AddMeshObject( TWorldDocument& Document, EEditMeshPrimitive Kind, int32 ParentId, const FVector3& WorldPosition, bool bCollider );

	// Model 部品を持つオブジェクトを、編集メッシュに変える: モデルの三角形（IEditorAssetInfo::GetCollisionMesh）から Mesh 部品を作り（近い頂点はまとめる）、Model 部品を外す。
	// 画像・色・アニメーションは引き継がない（材質は既定の 1 つ）。モデルが無い・三角形が取れない・Mesh 部品がすでにあるなら false（何も変えない）
	bool ConvertModelToMesh( TWorldDocument& Document, int32 ObjectId, const IEditorAssetInfo& Assets );
}
