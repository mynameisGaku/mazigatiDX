// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_WORLD_MESH_EDITMESH_H
#define FRAMEWORK_WORLD_MESH_EDITMESH_H
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

// 編集可能なmeshの頂点、面、材質と保存・検証操作をまとめる。

// 材質
struct FEditMeshMaterial
{
	FString Name;
	FColor32 Color = FColor32( 200, 200, 200, 255 );   // 画像に掛かる色（A は使わない。不透明）
	FString Texture;                                    // 画像のファイル（アセットのルートからの相対）。空なら、色だけ
	float32 Tile = 1.0f;                                // 画像 1 枚が、ローカルのどれだけの長さに当たるか
	FVector2 Offset = FVector2( 0.0f, 0.0f );           // 画像の貼る位置のずれ（画像の枚数。0..1 で 1 周）
	bool bTwoSided = false;                             // true なら、裏も描く
	// 無効が既定の、時間変化する材質効果。
	FMeshMaterialEffectDefinition Effect;
};

// 面
struct FEditMeshFace
{
	TArray<int32> Indices;   // 頂点の番号（3 つ以上）。見ている人から時計回りが表
	int32 Material = 0;
};

// 検証の結果（1 件）
struct FEditMeshIssue
{
	bool bError = false;     // false なら警告（使える）
	int32 Face = -1;         // 面についてなら、その面の番号
	FString Message;
};

struct FEditMesh
{
	TArray<FVector3> Vertices;
	TArray<FEditMeshFace> Faces;
	TArray<FEditMeshMaterial> Materials;
	// 頂点で、面の法線が、この角度（度）以内の面どうしを、なめらかにつなぐ（陰を連続させる）。0 なら、すべて平ら
	float32 SmoothAngle = 0.0f;

	// 1 つのメッシュの大きさの上限（保存・描画・当たり判定が現実的な範囲）
	static constexpr int32 MaxVertices = 200000;
	static constexpr int32 MaxFaces = 200000;

	int32 GetNumVertices() const { return Vertices.Num(); }
	int32 GetNumFaces() const { return Faces.Num(); }
	bool IsEmpty() const { return Faces.IsEmpty(); }

	// 範囲外なら、既定の材質
	const FEditMeshMaterial& GetMaterial( int32 Index ) const;

	// 面が使える形か（頂点が 3 つ以上で、すべて範囲内で、同じ頂点を 2 度使わない）
	bool IsFaceValid( int32 Face ) const;
	// 面の法線（長さ 1。表の向き）と面積。面が使えない・つぶれているなら、Zero / 0
	FVector3 ComputeFaceNormal( int32 Face ) const;
	float32 ComputeFaceArea( int32 Face ) const;
	// 面の中心（頂点の平均）
	FVector3 ComputeFaceCenter( int32 Face ) const;

	// 使える面だけの三角形（頂点の番号は、この Vertices のもの。1 つの面は、先頭の頂点を中心にした扇形）。当たり判定・取り込みに使う。
	// OutFaceOfTriangle があれば、三角形ごとの、元の面の番号が入る
	TArray<uint32> BuildTriangles( TArray<int32>* OutFaceOfTriangle = nullptr ) const;
	int32 GetNumTriangles() const;

	// 使える面が使う頂点の、軸に平行な箱。使える面がなければ false
	bool GetBounds( FVector3& OutMin, FVector3& OutMax ) const;
	// 閉じた形の体積（表が外向きなら正）。閉じていない形では、意味のある値ではない
	float32 ComputeVolume() const;

	// 内容の識別（頂点・面・材質・なめらかさ。キャッシュの合図）
	uint64 ComputeHash() const;

	// 問題の一覧（面の頂点の番号が範囲外・面が小さすぎる・つぶれている・使われない頂点・数が多すぎる・材質の番号・非数）
	TArray<FEditMeshIssue> Validate() const;

	// JSON:
	//   { "Vertices": [[x, y, z], ...], "Faces": [[i, j, k, ...], ...], "FaceMaterials": [0, 1, ...] (すべて 0 なら出さない),
	//     "Materials": [{ "Name": "", "Color": "#C8C8C8", "Texture": "", "Tile": 1, "Offset": [0, 0], "TwoSided": false, "Effect": { ... } (既定なら出さない) }], "Smooth": 30 (0 なら出さない) }
	NlohmannJson ToJson() const;
	// 読み込む。オブジェクトでなければ false（Out は変えない）。形の崩れは、読めるところまで読んで、Warnings に理由を足す
	static bool FromJson( const NlohmannJson& Json, FEditMesh& Out, TArray<FString>* Warnings = nullptr );
};

#endif
