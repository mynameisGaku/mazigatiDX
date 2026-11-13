// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Material/MaterialProgram.h"
#include "Library/Math/Vector3.h"
#include "Library/Math/Vector4.h"

//=============================================================================
// Substrate 風の万能モデルの、陰影の計算
//   層（スラブ）1 つの陰影を、1 か所に決めてある。同じ計算を、CPU（C++）と、シェーダー（HLSL の関数の文字列）の、両方で持つ。
//   バックエンドに依らない（HLSL は、定数バッファやレジスタを使わない、ただの関数。DxLib でも、ほかのバックエンドでも、同じ文字列を使う）。
//
//   フルの評価（FMaterialCapabilities::bFullSubstrate。CPU は ShadeSlab）:
//     - 鏡面: GGX のマイクロファセット（フレネル・分布・遮蔽）。Anisotropy で、接線の向きに伸びた光沢（-1〜1。負は、従接線の向き）
//     - 薄膜（ThinFilm 0〜1）: 見る角度で色の変わる、干渉の色を、鏡面の色に混ぜる
//     - サブサーフェス（Subsurface の色）: 光が回り込む（影の境目がやわらぐ）のと、逆光で透ける分を足す
//     - クリアコート: 上に、もう 1 枚の鏡面（F0 = 0.04）。下の層は、コートが反射した分だけ暗くなる
//     - 拡散は、鏡面が反射した残り（1 - フレネル）。金属（Metallic）は、拡散が無く、BaseColor が鏡面の色
//     - 層を混ぜる（SubstrateMix）ときは、層ごとに陰影を計算してから、重みで足す（値を混ぜてから 1 回計算するのとは違い、
//       金属と非金属を混ぜても、中間の、ありえない材質にならない）。いちどに計算する層は、MaxSlabs まで
//   簡略版（ShadeSlabSimplified）: 拡散・鏡面（Blinn-Phong 風）・発光・クリアコートの近似。サブサーフェス・異方性・薄膜は、無視。
//
//   光は、平行光 1 つ（強さは固定）と、環境の明るさ（拡散は 15%。鏡面は、上が明るく下が暗い、ぼんやりした映り込み。上は +Y）。影は、落ちない。
//
//   // CPU で、マテリアルの見た目を計算する（検査や、ピクセルシェーダーの無いバックエンド用）
//   TArray<FVector4> Values;
//   Material->EvaluateCpu( Input, Values );
//   TArray<FSubstrateSlab> Slabs;
//   NMaterialSubstrate::ReadSlabs( Material->GetProgram(), Values, Slabs );
//   const FVector3 Color = NMaterialSubstrate::Shade( Slabs, Geometry, Material->GetProgram().bFullSubstrate );
//=============================================================================

// 層 1 つの値（SubstrateSlab ノードのピンと同じ）と、混ぜるときの重み
struct FSubstrateSlab
{
	FVector3 BaseColor = FVector3( 0.5f, 0.5f, 0.5f );
	FVector3 F0 = FVector3( 0.04f, 0.04f, 0.04f );
	float32 Roughness = 0.5f;
	float32 Metallic = 0.0f;
	FVector3 Emissive = FVector3( 0.0f, 0.0f, 0.0f );
	float32 Opacity = 1.0f;
	float32 ClearCoat = 0.0f;
	float32 ClearCoatRoughness = 0.1f;
	FVector3 Subsurface = FVector3( 0.0f, 0.0f, 0.0f );
	float32 Anisotropy = 0.0f;
	float32 ThinFilm = 0.0f;
	float32 Weight = 1.0f;
};

// 陰影を計算する点の、向き（どれも、長さは問わない。計算の中で、正規化する）
struct FSubstrateGeometry
{
	FVector3 Normal = FVector3( 0.0f, 0.0f, -1.0f );
	FVector3 Tangent = FVector3( 1.0f, 0.0f, 0.0f );    // 面に沿った向き（UV の U の向き）。異方性の光沢が伸びる向き
	FVector3 ToLight = FVector3( 0.0f, 0.0f, -1.0f );   // 面から、光へ
	FVector3 ToCamera = FVector3( 0.0f, 0.0f, -1.0f );  // 面から、カメラへ
};

namespace NMaterialSubstrate
{
	// いちどに陰影を計算する層の数の上限（SubstrateMix で、これより多くの層を混ぜると、値を混ぜた 1 つの層にまとめる）
	constexpr int32 MaxSlabs = 4;

	// 層 1 つの陰影（フルの評価）。重み（Weight）は、掛けない
	FVector3 ShadeSlab( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry );
	// 層 1 つの陰影（簡略版）
	FVector3 ShadeSlabSimplified( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry );
	// 層を、重みで足した色。OutOpacity（null でもよい）に、不透明度（重みで足したもの）
	FVector3 Shade( const TArray<FSubstrateSlab>& Slabs, const FSubstrateGeometry& Geometry, bool bFull, float32* OutOpacity = nullptr );

	// コンパイルしたマテリアル（Surface・Substrate）と、CPU で評価した値から、層を読む。
	// フルの評価のプログラムは、層ごと（FMaterialProgram::SubstrateSlabs）。簡略版は、値を混ぜた 1 つの層。Substrate でなければ false
	bool ReadSlabs( const FMaterialProgram& Program, const TArray<FVector4>& Values, TArray<FSubstrateSlab>& OutSlabs );

	// HLSL の関数（フルの評価）。ShadeSlab と同じ計算の SubstrateShadeSlab と、画素の接線を作る SubstrateTangent
	const char* GetHlslFunctions();
}
