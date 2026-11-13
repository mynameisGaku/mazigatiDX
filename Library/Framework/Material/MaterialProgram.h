// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Map.h"
#include "Library/Framework/Material/MaterialGraph.h"

//=============================================================================
// マテリアルの中間表現（IR）
//   グラフを、型のついた、1 つずつの計算（命令）の並びにしたもの。バックエンドに依らない。HLSL にする（MaterialHlsl）・CPU で評価する
//   （MaterialEval。テストやエディタのノードのプレビュー用）は、この表現から行う。
//   命令は、前の命令の結果（番号）だけを使う（SSA）。出口（Outputs）は、出力ピンごとの、命令の番号。
//=============================================================================
enum class EMaterialOp : uint8
{
	Const,       // Constant の値
	Param,       // パラメーター（Slot = Params の番号）
	Input,       // ドメインの入力（Sub = EMaterialInput）
	Unary,       // 1 つの値の計算（Sub = EMaterialUnary。A）
	Binary,      // 2 つ（Sub = EMaterialBinary。A, B）
	Ternary,     // 3 つ（Sub = EMaterialTernary。A, B, C）
	Splat,       // Float を、Type の成分の数に広げる（A）
	Append,      // A と B を並べる
	Mask,        // A から、Swizzle の成分を取り出す
	Sample,      // 画像を読む（Slot = Textures の番号、A = UV）。Float4
	SceneDepth,  // シーンの深度（Slot = Textures の番号、A = UV）。カメラからの距離（Float）
	Noise        // 値のノイズ（A = UV）。Float
};

enum class EMaterialInput : uint8
{
	Uv,
	Time,
	Size,
	Hover,
	Press,
	Value,
	Enabled,
	VertexColor,
	WorldPosition,
	WorldNormal,
	CameraVector,
	ParticleLife,
	ParticleAge,
	ParticleRandom,
	ParticleSpeed
};

enum class EMaterialUnary : uint8
{
	Abs,
	Saturate,
	OneMinus,
	Negate,
	Sin,
	Cos,
	Frac,
	Floor,
	Ceil,
	Sqrt,
	Length,
	Normalize
};

enum class EMaterialBinary : uint8
{
	Add,
	Subtract,
	Multiply,
	Divide,
	Min,
	Max,
	Power,
	Mod,
	Step,
	Dot,
	Distance
};

enum class EMaterialTernary : uint8
{
	Lerp,
	Clamp,
	SmoothStep
};

struct FMaterialInstruction
{
	EMaterialOp Op = EMaterialOp::Const;
	EMaterialValueType Type = EMaterialValueType::Float;
	uint8 Sub = 0;                 // Input / Unary / Binary / Ternary の種類
	int32 A = -1;
	int32 B = -1;
	int32 C = -1;
	int32 Slot = -1;
	int32 Node = -1;               // 元のノード（診断・エディタのプレビュー用。展開で増えた命令は、元のノード）
	FVector4 Constant = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	uint8 Swizzle[4] = { 0, 1, 2, 3 };
	int32 NumSwizzle = 0;
};

// パラメーターの置き場所（g_Params[Index]。Float・Float2・Float3 も、4 つ組 1 つを使う）
struct FMaterialParamSlot
{
	FString Name;
	FString ParameterCollection;            // 空でなければ、そのコレクションの値を読む
	EMaterialValueType Type = EMaterialValueType::Float;
	int32 Index = 0;
	FVector4 Default = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	bool bColor = false;
	bool IsFromParameterCollection() const { return !ParameterCollection.IsEmpty(); }
};

enum class EMaterialTextureSource : uint8
{
	Parameter,     // 画像のパラメーター
	SceneColor,    // PostProcess: シーンの色
	SceneDepth     // PostProcess: シーンの深度
};

struct FMaterialTextureSlot
{
	FString Name;
	FString DefaultPath;
	EMaterialTextureSource Source = EMaterialTextureSource::Parameter;
	int32 Register = 0;            // HLSL の t レジスタ（0 から連続）
};

struct FMaterialOutputValue
{
	FString Pin;
	EMaterialValueType Type = EMaterialValueType::Float;
	int32 Value = -1;              // 命令の番号
};

// Substrate の層（SubstrateSlab のピン）の並び。FMaterialSubstrateSlab::Fields の番号
namespace NMaterialSlabField
{
	enum : int32
	{
		BaseColor,
		F0,
		Roughness,
		Metallic,
		Emissive,
		Opacity,
		ClearCoat,
		ClearCoatRoughness,
		Subsurface,
		Anisotropy,
		ThinFilm,
		Num
	};

	// 出力の名前（"BaseColor" など）。範囲の外は、空
	const char* GetName( int32 Field );
}

// 陰影を、別々に計算する層 1 つ（フルの評価）。値は、命令の番号
struct FMaterialSubstrateSlab
{
	int32 Fields[NMaterialSlabField::Num] = {};
	int32 Weight = -1;             // 混ぜるときの重み（Float。全部の層で、足して 1）
};

struct FMaterialProgram
{
	EMaterialDomain Domain = EMaterialDomain::UI;
	EMaterialShading Shading = EMaterialShading::Unlit;
	TArray<FMaterialInstruction> Code;
	TArray<FMaterialOutputValue> Outputs;
	TArray<FMaterialParamSlot> Params;
	TArray<FMaterialTextureSlot> Textures;
	TMap<int32, int32> NodeValues;             // ノードの番号 → そのノードの値の命令の番号（Slab のノードは、無い）
	TArray<FMaterialDiagnostic> Diagnostics;
	bool bValid = false;                       // エラーなく作れた
	bool bCustomHlsl = false;                  // HLSL の直指定（Code は空。パラメーター・画像の置き場だけがある）
	bool bUsesSceneDepth = false;
	// Substrate を、フルに評価する（バックエンドの能力から。false なら、簡略版で、Outputs の 11 の値だけを使う）
	bool bFullSubstrate = false;
	// フルの評価で、陰影を別々に計算する層（1〜NMaterialSubstrate::MaxSlabs。Surface・Substrate のときだけ）
	TArray<FMaterialSubstrateSlab> SubstrateSlabs;

	const FMaterialOutputValue* FindOutput( const FString& Pin ) const
	{
		for ( int32 Index = 0; Index < Outputs.Num(); ++Index )
		{
			if ( Outputs[Index].Pin == Pin )
			{
				return &Outputs[Index];
			}
		}
		return nullptr;
	}

	const FMaterialParamSlot* FindParam( const FString& Name ) const
	{
		for ( int32 Index = 0; Index < Params.Num(); ++Index )
		{
			if ( Params[Index].Name == Name && !Params[Index].IsFromParameterCollection() )
			{
				return &Params[Index];
			}
		}
		return nullptr;
	}
};
