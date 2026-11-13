// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/MaterialProgram.h"

//=============================================================================
// NMaterialHlsl: 中間表現から、HLSL（ピクセルシェーダー）の全文を作る
//   DxLib のバックエンド向けの出力（シェーダーモデル 4.0）。ほかのバックエンドは、同じ中間表現から、自分の言語を出す。
//
//   ドメインごとの決まり（グラフから作るもの・HLSL の直指定のもの、どちらにも、システムが先頭に置く）:
//
//   UI / Particle / PostProcess（矩形を ShaderRect で描く。頂点シェーダーは、組み込み）
//     cbuffer cbMaterialParams : register( b4 )   { float4 g_Params[N]; }                          // パラメーター（Params の Index 順）
//     cbuffer cbMaterialFrame  : register( b5 )   { float4 g_Time; float4 g_Rect; float4 g_State; }
//         g_Time  = ( 秒, 前のフレームからの秒, 近い面, 遠い面 )     g_Rect = ( 矩形の幅, 高さ, 0, 0 )（PostProcess は、画面の大きさ）
//         g_State = ( ホバー, 押下, 値, 使える )（UI）
//     Texture2D g_Tex0 : register( t0 ) ...（置き場の Register 順。PostProcess は、t0 がシーンの色、使えば、次が深度）   SamplerState g_Sampler : register( s0 );
//     struct PixelInput { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; };
//
//   Surface（PrimitiveIndexedShader3D。頂点シェーダーは、標準の頂点シェーダー GetSurfaceVertexSource）
//     cbuffer cbScene  : register( b4 ) { row_major float4x4 g_ViewProjection; float4 g_LightDirection; float4 g_CameraPosition; float4 g_Time;
//                                         float4 g_LightColor;（rgb = 平行光の色、w = 点光源の数）  float4 g_SkyColor; float4 g_GroundColor; float4 g_AmbientColor;
//                                         float4 g_Environment;（環境マップの 1 段の幅・高さ・段の数・明るさの倍率）
//                                         float4 g_PointLightPositions[4];（xyz = 位置、w = 半径）  float4 g_PointLightColors[4];
//                                         row_major float4x4 g_ShadowViewProjection; float4 g_ShadowOptions;（影のマップの大きさ・深度のずらし・法線の向きのずらし） }
//     cbuffer cbObject : register( b5 ) { row_major float4x4 g_World; float4 g_Color; float4 g_Options; }
//     cbuffer cbMaterialParams : register( b6 ) { float4 g_Params[N]; }
//     struct VertexOutput { float4 Position : SV_POSITION; float3 Normal : TEXCOORD0; float3 World : TEXCOORD1; float2 Uv : TEXCOORD2; float4 Color : TEXCOORD3; };
//     光を計算するマテリアル（UsesSceneLighting）は、マテリアルの画像のあとに、環境マップ（g_EnvironmentMap）・影のマップ（g_ShadowMap）を、この順で置く
//     （使うものだけ。FMaterialHlslFeatures。レジスタは、GetEnvironmentRegister・GetShadowRegister）。
//
//   HLSL の直指定（FMaterialDefinition::CustomHlsl）は、上の宣言（と、MatNoise などの補助関数）の「あと」に置かれる。PixelInput（Surface は VertexOutput）を受けて、
//   SV_Target を返す関数 main を、書くこと。
//
//   補助関数: float MatNoise( float2 ) / MatHash( float2 ) / MatLinearDepth( float )（PostProcess）、マクロ MatMod( a, b )。
//=============================================================================
// 場面から受け取る画像のうち、使うもの（使う組み合わせごとに、別のシェーダーになる。宣言した画像は、必ず置くこと）
struct FMaterialHlslFeatures
{
	bool bEnvironmentMap = false;   // 環境マップ（FSubstrateEnvironment のアトラス）を読む
	bool bShadowMap = false;        // 影のマップ（平行光から見た深度）を読む
};

namespace NMaterialHlsl
{
	// グラフから作ったプログラムの、ピクセルシェーダーの全文。HLSL の直指定のプログラムなら、CustomBody を、宣言のあとに付ける。
	// Features は、光を計算するマテリアル（UsesSceneLighting）だけが使う（ほかは、無視する）
	FString Generate( const FMaterialProgram& Program, const FString& CustomBody = FString(), const FMaterialHlslFeatures& Features = FMaterialHlslFeatures() );

	// 場面の光（点光源・環境マップ・影）を受けるマテリアルか（Surface で、フルのモデルで光を計算するもの）
	bool UsesSceneLighting( const FMaterialProgram& Program );
	// 環境マップ・影のマップを置く段（t レジスタ）。マテリアルの画像の、すぐあと
	int32 GetEnvironmentRegister( const FMaterialProgram& Program );
	int32 GetShadowRegister( const FMaterialProgram& Program, const FMaterialHlslFeatures& Features );

	// Surface の標準の頂点シェーダー（頂点の位置をワールド行列とビュー射影行列で変え、法線・ワールド位置・UV・頂点の色を渡す）
	FString GetSurfaceVertexSource();
	// 深度だけを書くピクセルシェーダー（影のマップ用。標準の頂点シェーダーと組み合わせて、R に、0〜1 の深度を書く）
	FString GetSurfaceDepthPixelSource();
}
