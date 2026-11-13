// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"
#include "Library/Math/Vector4.h"

//=============================================================================
// マテリアルの基本の型
//   マテリアルは、「見た目の計算」を、ノードのグラフ（または HLSL）で書いたもの。UI・描画・エフェクトのどれにも属さない、汎用の仕組み。
//   使われ方（どこに・どの入力で適用されるか）は、ドメインで決まる。
//
//   ドメイン       適用先                               入力の例                                  出力
//   UI             部品のブラシ（矩形）                  UV・部品の大きさ・ホバー・押下・値・時間      Color（RGB）・Opacity
//   Surface        メッシュの表面（球・立方体・地面など）  UV・ワールド位置・法線・カメラの向き・時間    Unlit: Emissive・Opacity / DefaultLit: BaseColor ほか / Substrate: Surface
//   Particle       エフェクトの粒子の矩形               UV・頂点の色（粒子の色）・時間                Color（RGB）・Opacity
//   PostProcess    画面全体（シーンの色・深度を読む）     画面の UV・シーンの色・深度・画面の大きさ・時間  Color（RGB）
//=============================================================================
enum class EMaterialDomain : uint8
{
	UI,
	Surface,
	Particle,
	PostProcess
};

// Surface の陰影の付け方
enum class EMaterialShading : uint8
{
	Unlit,        // 光を計算しない（出力は発光だけ）
	DefaultLit,   // 拡散 + 鏡面（金属・粗さ）の標準の光
	Substrate     // Substrate 風の万能モデル（層・クリアコート・サブサーフェス ほか）。バックエンドの能力に応じて、簡略版へ落とす
};

// 値の型。Slab は、Substrate の層（マテリアルの 1 つの層の、すべての性質の束）
enum class EMaterialValueType : uint8
{
	Float,
	Float2,
	Float3,
	Float4,
	Texture,
	Slab
};

enum class EMaterialSeverity : uint8
{
	Info,      // 知らせ（簡略化された、など。動く）
	Warning,   // 注意（値を切り詰めた、など。動く）
	Error      // 失敗（このマテリアルは、使えない）
};

// コンパイルや検証の結果の 1 件。NodeId は、問題のあるノード（無ければ -1）
struct FMaterialDiagnostic
{
	EMaterialSeverity Severity = EMaterialSeverity::Info;
	FString Message;
	int32 NodeId = -1;
};

//=============================================================================
// FMaterialCapabilities
//   バックエンドが、何を評価できるかの宣言。足りない機能は、簡略版へ落とすか、診断（FMaterialDiagnostic）で伝える。例外やクラッシュにはしない。
//   既定（NMaterial::DefaultCapabilities）は、DxLib のバックエンド相当: 固定のパイプラインとピクセルシェーダー（HLSL）。
//=============================================================================
struct FMaterialCapabilities
{
	bool bPixelShaders = true;        // ピクセルシェーダー（HLSL）を実行できる（false なら、マテリアルは描けない。コンパイルと CPU での評価はできる）
	bool bSceneDepthRead = true;      // ポストプロセスで、シーンの深度を読める
	bool bFullSubstrate = true;       // Substrate をフルに評価できる（MaterialSubstrate.h。false なら、簡略版: 拡散・鏡面・発光・クリアコートの近似。サブサーフェス・異方性・薄膜は、無視）
	int32 MaxTextures = 8;            // 1 つのマテリアルが使える画像の数
	int32 MaxParameterVectors = 64;   // パラメーター（4 つ組）の数の上限
};

namespace NMaterial
{
	FMaterialCapabilities DefaultCapabilities();

	// 値の型の、成分の数（Float = 1 ... Float4 = 4。Texture・Slab は 0）
	int32 NumComponents( EMaterialValueType Type );
	// 成分の数から、型（1〜4。範囲の外は Float）
	EMaterialValueType TypeFromComponents( int32 NumComponents );

	// 名前（JSON・エディタ・診断で使う）
	const char* ToString( EMaterialDomain Domain );
	const char* ToString( EMaterialShading Shading );
	const char* ToString( EMaterialValueType Type );
	const char* ToString( EMaterialSeverity Severity );
	bool FromString( const FString& Text, EMaterialDomain& OutDomain );
	bool FromString( const FString& Text, EMaterialShading& OutShading );
	bool FromString( const FString& Text, EMaterialValueType& OutType );

	// 診断に、エラーが含まれるか
	bool HasError( const TArray<FMaterialDiagnostic>& Diagnostics );
}
