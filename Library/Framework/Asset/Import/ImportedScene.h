#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FImportedScene
//   FBX / OBJ などから読んだ、モデルの中間データ（どの形式にも依存しない）。
//   これを .x（DirectX のテキスト形式）に書き出して DxLib に読ませ、.mv1 として保存する（ModelExporter）。
//
//   座標系: 取り込みが返すのは右手系（Y が上）。ConvertToLeftHanded で、DxLib（左手系・Y が上）にする
//   （Z を反転する。FBX で -Z を向いていた正面が、DxLib の既定のカメラ（+Z を向く）から見て手前になる）。
//
//   行列は DirectX と同じ行ベクトル規約（v * M。移動は 4 行目）。
//=============================================================================
struct FImportMatrix
{
	float32 M[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	// 全要素の差が Tolerance 以下か
	bool NearlyEquals( const FImportMatrix& Other, float32 Tolerance = 1.0e-5f ) const;
};

// 骨（階層）。Parent は FImportedScene::Nodes の番号（-1 で根）
struct FImportedNode
{
	FString Name;
	int32 Parent = -1;
	FImportMatrix Local;   // 親から見た位置・向き・拡大（アニメーションの無いときの姿）
};

// 骨 1 本に結びついた頂点（スキニング）
struct FImportedCluster
{
	int32 BoneNode = -1;           // FImportedScene::Nodes の番号
	FImportMatrix Offset;          // メッシュの頂点（メッシュの座標）から骨の座標への変換（バインド時）
	TArray<int32> Vertices;        // FImportedMesh の頂点の番号
	TArray<float32> Weights;       // 同じ並びの重み
};

// 三角形のメッシュ。頂点は座標・法線・UV の組ごとに 1 つ（共有しない分は複製済み）
struct FImportedMesh
{
	FString Name;
	int32 Node = -1;                       // 付いている骨（ノード）。スキンのあるメッシュは、骨の動きだけで動く
	TArray<FVector3> Positions;
	TArray<FVector3> Normals;              // Positions と同じ数
	TArray<FVector2> UVs;                  // 無ければ空。あれば Positions と同じ数（V は上が 0 に直してある）
	TArray<int32> Indices;                 // 3 つで 1 つの三角形
	TArray<int32> TriangleMaterials;       // 三角形ごとの FImportedScene::Materials の番号（-1 で既定）
	TArray<FImportedCluster> Clusters;     // 空ならスキンなし

	FORCEINLINE bool IsSkinned() const { return !Clusters.IsEmpty(); }
	FORCEINLINE int32 GetNumTriangles() const { return Indices.Num() / 3; }
};

struct FImportedTexture
{
	FString File;                  // ファイルが書いていたパス（元のファイルからの相対）。埋め込みなら、ファイル名として使う名前
	TArray<uint8> EmbeddedData;    // 埋め込まれた画像（PNG など）。空ならファイルを参照する
};

struct FImportedMaterial
{
	FString Name;
	float32 Diffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float32 Specular[3] = { 0.0f, 0.0f, 0.0f };
	float32 Emissive[3] = { 0.0f, 0.0f, 0.0f };
	float32 Power = 5.0f;
	int32 Texture = -1;            // FImportedScene::Textures の番号（拡散色の画像）
};

// ノードの動き。キーは時間順に 1 サンプルずつ（毎秒 FImportedAnimation::Fps コマ）
struct FImportedTrack
{
	int32 Node = -1;
	TArray<FImportMatrix> Keys;    // FImportedAnimation::NumKeys 個
};

struct FImportedAnimation
{
	FString Name;
	float32 Fps = 30.0f;
	int32 NumKeys = 0;
	TArray<FImportedTrack> Tracks; // 動くノードだけ
};

struct FImportedScene
{
	TArray<FImportedNode> Nodes;
	TArray<FImportedMesh> Meshes;
	TArray<FImportedMaterial> Materials;
	TArray<FImportedTexture> Textures;
	TArray<FImportedAnimation> Animations;

	FORCEINLINE bool HasMesh() const { return !Meshes.IsEmpty(); }
};

// 1 つの頂点に影響する骨と重み
struct FImportInfluence
{
	int32 Cluster = 0;
	float32 Weight = 0.0f;
};

namespace NImportedScene
{
	// DxLib（DirectX 形式）が 1 頂点で使える骨の数
	constexpr int32 MaxInfluencesPerVertex = 4;

	// Z を反転した座標系に変換する行列（S * M * S。S = diag( 1, 1, -1, 1 )）
	FImportMatrix MirrorZ( const FImportMatrix& Matrix );

	// 右手系 -> 左手系（DxLib）。座標・法線の Z、三角形の向き、ノードとアニメーションの行列、スキンのオフセットをまとめて変換する。
	// 二度呼ぶと元に戻らない（呼ぶのは 1 回だけ）
	void ConvertToLeftHanded( FImportedScene& Scene );

	// 影響の大きい順に Max 個だけ残し（0 以下の重みは捨てる）、合計が 1 になるように直す。全部捨てられたら空になる
	void LimitInfluences( TArray<FImportInfluence>& Influences, int32 Max = MaxInfluencesPerVertex );

	// X ファイルのフレーム名にできる形にする（空白・記号は '_'。DxLib は空白で名前が切れる）。空なら Fallback
	FString SanitizeName( const FString& Name, const FString& Fallback );

	// ノードごとの、X ファイルのフレーム名。名前が重なったら _2, _3 ... を付けて、全部違う名前にする。決まった入力から決まった結果になる
	TArray<FString> MakeFrameNames( const FImportedScene& Scene );

	// 構造の確認（番号の範囲・数の整合）。問題を文章で返す。空なら問題なし
	TArray<FString> Validate( const FImportedScene& Scene );
}
