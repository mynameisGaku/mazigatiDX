// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Material/MaterialProgram.h"
#include "Library/Math/Vector3.h"
#include "Library/Math/Vector4.h"

//=============================================================================
// Substrate 風の万能モデルの、陰影の計算
//   層（スラブ）1 つの陰影を、1 か所に決めてある。同じ計算を、CPU（C++）と、シェーダー（HLSL の関数の文字列）の、両方で持つ。
//   バックエンドに依らない（HLSL は、定数バッファやレジスタを使わない、ただの関数。DxLib でも、ほかのバックエンドでも、同じ文字列を使う）。
//   Surface の DefaultLit も、このモデルの、層 1 つとして計算する（光・影・映り込みが、どのマテリアルでも揃う）。
//
//   フルの評価（FMaterialCapabilities::bFullSubstrate。CPU は ShadeSlab）:
//     - 鏡面: GGX のマイクロファセット（フレネル・分布・遮蔽）。Anisotropy で、接線の向きに伸びた光沢（-1〜1。負は、従接線の向き）
//     - 薄膜（ThinFilm 0〜1）: 見る角度で色の変わる、干渉の色を、鏡面の色に混ぜる
//     - サブサーフェス（Subsurface の色）: 光が回り込む（影の境目がやわらぐ）のと、逆光で透ける分を足す
//     - クリアコート: 上に、もう 1 枚の鏡面（F0 = 0.04）。下の層は、コートが反射した分だけ暗くなる。コートにも、環境が映り込む
//     - 拡散は、鏡面が反射した残り（1 - フレネル）。金属（Metallic）は、拡散が無く、BaseColor が鏡面の色
//     - 法線（Normal）: 層ごとの、ワールドの法線（法線マップ）。長さ 0 なら、面の法線
//     - 層を混ぜる（SubstrateMix。横に並べる）: 層ごとに陰影を計算してから、重みで足す（値を混ぜてから 1 回計算するのとは違い、
//       金属と非金属を混ぜても、中間の、ありえない材質にならない）
//     - 層を重ねる（SubstrateLayer。縦に重ねる）: 下の層は、上の層を通り抜けた分（Throughput = 上の層が反射しなかった分 x 通り抜ける色）だけ見える。
//       上の層の中で散らばる光（拡散・サブサーフェス）は、下へ抜ける分だけ減る（DiffuseScale）
//     - いちどに計算する層は、MaxSlabs まで
//   簡略版（ShadeSlabSimplified）: 拡散・鏡面（Blinn-Phong 風）・発光・クリアコートの近似。平行光 1 つだけ。サブサーフェス・異方性・薄膜は、無視。
//
//   光（FSubstrateLighting）:
//     - 平行光 1 つ（色つき。影の中では Shadow が 0）と、点光源（MaxPointLights まで。半径の外は、届かない）
//     - 環境: 環境マップ（FSubstrateEnvironment。粗さに応じて、ぼかした映り込み）。無ければ、空（上。+Y）と地面（下）の色のぼんやりした映り込みと、一様な明るさ
//
//   // CPU で、マテリアルの見た目を計算する（検査や、ピクセルシェーダーの無いバックエンド用）
//   TArray<FVector4> Values;
//   Material->EvaluateCpu( Input, Values );
//   TArray<FSubstrateSlab> Slabs;
//   NMaterialSubstrate::ReadSlabs( Material->GetProgram(), Values, Slabs );
//   const FVector3 Color = NMaterialSubstrate::Shade( Slabs, Geometry, Material->GetProgram().bFullSubstrate, nullptr, Lighting );
//   const float32 Opacity = NMaterialSubstrate::ReadOpacity( Material->GetProgram(), Values );
//=============================================================================

// 層 1 つの値（SubstrateSlab ノードのピンと同じ）と、混ぜる・重ねるときの重み
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
	FVector3 Normal = FVector3( 0.0f, 0.0f, 0.0f );          // この層の法線（ワールド。法線マップ）。長さ 0 なら、面の法線
	float32 Weight = 1.0f;                                    // 見える割合（混ぜるときの重み・上の層が覆う割合）
	FVector3 Throughput = FVector3( 1.0f, 1.0f, 1.0f );       // 上に重なった層を通り抜けて、この層が見える割合
	FVector3 DiffuseScale = FVector3( 1.0f, 1.0f, 1.0f );     // この層の中で散らばる光の割合（下の層へ抜ける分だけ、減る）
};

// 陰影を計算する点の、位置と向き（向きは、長さを問わない。計算の中で、正規化する）
struct FSubstrateGeometry
{
	FVector3 Normal = FVector3( 0.0f, 0.0f, -1.0f );
	FVector3 Tangent = FVector3( 1.0f, 0.0f, 0.0f );    // 面に沿った向き（UV の U の向き）。異方性の光沢が伸びる向き
	FVector3 ToLight = FVector3( 0.0f, 0.0f, -1.0f );   // 面から、平行光へ
	FVector3 ToCamera = FVector3( 0.0f, 0.0f, -1.0f );  // 面から、カメラへ
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );   // ワールドの位置（点光源までの距離に使う）
};

// 点光源 1 つ。光は、Radius で 0 になるように、なめらかに弱まる
struct FSubstratePointLight
{
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );
	FVector3 Color = FVector3( 1.0f, 1.0f, 1.0f );      // 色 x 強さ（光源のすぐそばでの明るさ）
	float32 Radius = 10.0f;
};

//=============================================================================
// FSubstrateEnvironment: 環境マップ（まわりの景色の映り込み）
//   横長の全周の絵（正距円筒図法。横が方位、縦が仰角。上端が真上。絵の中央が +Z の向き）から、粗さごとにぼかした絵を作って、縦に並べて持つ（アトラス）。
//   鏡のような面は、ぼかしていない段を、粗い面は、ぼかした段を読む。いちばんぼかした段は、拡散の光（まわり全体からの光）になる。
//   CPU だけで作れる（GPU を使わない）。GPU へは、GetAtlas の画像を、そのまま渡す（CMaterialEnvironmentMap）。Sample は、シェーダーと同じ計算。
//=============================================================================
class FSubstrateEnvironment
{
public:
	// 1 段の大きさと、段の数（段 0 がぼかしなし、最後の段が、いちばんぼかしたもの）
	static constexpr int32 LevelWidth = 256;
	static constexpr int32 LevelHeight = 128;
	static constexpr int32 NumLevels = 6;

	// 全周の絵から作る。絵が無効なら、何もせず false
	bool Build( const FPixelImage& Panorama );
	bool IsValid() const { return Atlas.IsValid(); }
	// 段を縦に並べた画像（LevelWidth x LevelHeight * NumLevels）
	const FPixelImage& GetAtlas() const { return Atlas; }
	// Direction の向きの色。Blur は、ぼかし（0 = 鏡、1 = まわり全体。粗さを、そのまま渡す）。無効なら、黒
	FVector3 Sample( const FVector3& Direction, float32 Blur ) const;

private:
	FVector3 SampleLevel( const FVector3& Direction, int32 Level ) const;

	FPixelImage Atlas;
};

// 光（平行光は、向きが FSubstrateGeometry::ToLight）
struct FSubstrateLighting
{
	static constexpr int32 MaxPointLights = 4;

	FVector3 LightColor = FVector3( 0.85f, 0.85f, 0.85f );    // 平行光の色 x 強さ
	float32 Shadow = 1.0f;                                    // 平行光が届く割合（1 = 届く、0 = 影の中）
	// 環境マップが無いときの環境: 鏡面には、上（+Y）が空の色、下が地面の色の、ぼんやりした映り込み。粗い面と拡散は、環境の明るさ（AmbientColor）
	FVector3 SkyColor = FVector3( 0.40f, 0.40f, 0.40f );
	FVector3 GroundColor = FVector3( 0.05f, 0.05f, 0.05f );
	FVector3 AmbientColor = FVector3( 0.15f, 0.15f, 0.15f );
	int32 NumPointLights = 0;
	FSubstratePointLight PointLights[MaxPointLights];
	// 環境マップ（null・無効なら、上の 3 つの色を使う）と、その明るさの倍率
	const FSubstrateEnvironment* Environment = nullptr;
	float32 EnvironmentIntensity = 1.0f;
};

namespace NMaterialSubstrate
{
	// いちどに陰影を計算する層の数の上限（これより多くの層を混ぜる・重ねると、値を混ぜた 1 つの層にまとめる）
	constexpr int32 MaxSlabs = 4;

	// 層 1 つの陰影（フルの評価）。重み（Weight）は、掛けない。Throughput と DiffuseScale は、掛かる
	FVector3 ShadeSlab( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry, const FSubstrateLighting& Lighting = FSubstrateLighting() );
	// 層 1 つの陰影（簡略版。平行光の向きだけを使う）
	FVector3 ShadeSlabSimplified( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry );
	// 層を、重みで足した色。OutOpacity（null でもよい）に、不透明度を重みで足したもの（層を重ねたマテリアルの不透明度は、ReadOpacity で読む）
	FVector3 Shade( const TArray<FSubstrateSlab>& Slabs, const FSubstrateGeometry& Geometry, bool bFull, float32* OutOpacity = nullptr, const FSubstrateLighting& Lighting = FSubstrateLighting() );

	// コンパイルしたマテリアル（Surface の DefaultLit・Substrate）と、CPU で評価した値から、層を読む。
	// フルの評価のプログラムは、層ごと（FMaterialProgram::SubstrateSlabs）。簡略版は、値を混ぜた 1 つの層。光を計算しないマテリアルなら false
	bool ReadSlabs( const FMaterialProgram& Program, const TArray<FVector4>& Values, TArray<FSubstrateSlab>& OutSlabs );
	// マテリアルの不透明度（出力の Opacity。無ければ 1）
	float32 ReadOpacity( const FMaterialProgram& Program, const TArray<FVector4>& Values );

	// HLSL の関数（フルの評価）。ShadeSlab と同じ計算の SubstrateShadeSlab（引数は、構造体 SubstrateSlab・SubstrateGeometry・SubstrateLighting）と、
	// 画素の接線を作る SubstrateTangent・SubstrateBitangent。
	// 環境マップを使うバックエンドは、この文字列より前で、#define SUBSTRATE_ENVIRONMENT_MAP <Texture2D の名前> を書く（FSubstrateEnvironment のアトラスを、Load で読む。サンプラーは使わない）
	const char* GetHlslFunctions();
}
