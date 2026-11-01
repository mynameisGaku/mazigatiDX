// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// モデルの構造（フレーム・メッシュ・材質・テクスチャ）と、材質の色の値の型
//   IAssetBackend::GetModelStructure が埋め、CModel::GetStructure が読み込んだときの値として覚える。
//   番号（Index）は、バックエンドの中の並び（DxLib では MV1 の番号）。-1 は「無い」。
//=============================================================================

// フレーム（骨・節点）1 つ。親子で木になる
struct FModelFrameInfo
{
	FString Name;
	// 親のフレームの番号（いちばん上は -1）
	int32 Parent = -1;
	// 子のフレームの番号
	TArray<int32> Children;
	// このフレームが持つメッシュの番号
	TArray<int32> Meshes;
};

// メッシュ（同じ材質で描く三角形のまとまり）1 つ
struct FModelMeshInfo
{
	// 持ち主のフレームの番号
	int32 Frame = -1;
	// 使う材質の番号
	int32 Material = -1;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
	// モデルのローカル座標での、頂点の範囲
	FVector3 BoundsMin;
	FVector3 BoundsMax;
};

// 材質の色（光の当たり方）。色は 0〜1（1 を超えてもよい）。a は不透明度
struct FModelMaterialColors
{
	// 光が当たった所の色
	FColor Diffuse = FColor( 1.0f, 1.0f, 1.0f, 1.0f );
	// 環境光での色
	FColor Ambient = FColor( 0.0f, 0.0f, 0.0f, 0.0f );
	// 照り返し（ハイライト）の色と、その鋭さ（大きいほど小さく鋭い）
	FColor Specular = FColor( 0.0f, 0.0f, 0.0f, 0.0f );
	float32 SpecularPower = 0.0f;
	// 光が無くても出る色（自分で光る）
	FColor Emissive = FColor( 0.0f, 0.0f, 0.0f, 0.0f );

	FORCEINLINE bool operator==( const FModelMaterialColors& Other ) const
	{
		return Diffuse == Other.Diffuse && Ambient == Other.Ambient && Specular == Other.Specular && SpecularPower == Other.SpecularPower && Emissive == Other.Emissive;
	}

	FORCEINLINE bool operator!=( const FModelMaterialColors& Other ) const
	{
		return !( *this == Other );
	}
};

// 材質 1 つ
struct FModelMaterialInfo
{
	FString Name;
	// 読み込んだときの色
	FModelMaterialColors Colors;
	// 色に使うテクスチャの番号（無ければ -1）
	int32 DiffuseTexture = -1;
};

// テクスチャ 1 つ
struct FModelTextureInfo
{
	FString Name;
	// 色の画像のファイルのパス（モデルのファイルに書かれたとおり。無ければ空）
	FString ColorFilePath;
	int32 Width = 0;
	int32 Height = 0;
};

// モデルの構造の全体
struct FModelStructure
{
	TArray<FModelFrameInfo> Frames;
	TArray<FModelMeshInfo> Meshes;
	TArray<FModelMaterialInfo> Materials;
	TArray<FModelTextureInfo> Textures;
	// 形状（モーフ。表情や体型の変形）の名前。番号は FModelAppearance::SetShapeRate に渡すもの
	TArray<FString> Shapes;
};

// フレームの行列をどの座標で取るか
enum class EModelFrameSpace : uint8
{
	// 親のフレームから見た行列（アニメーションを含む）
	Local,
	// ワールドの行列（モデルの置き場所・アニメーションを含む。最後に描いたときの状態）
	World,
};
