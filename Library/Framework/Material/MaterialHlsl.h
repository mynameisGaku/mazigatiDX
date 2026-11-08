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
//     cbuffer cbScene  : register( b4 ) { row_major float4x4 g_ViewProjection; float4 g_LightDirection; float4 g_CameraPosition; float4 g_Time; }
//     cbuffer cbObject : register( b5 ) { row_major float4x4 g_World; float4 g_Color; float4 g_Options; }
//     cbuffer cbMaterialParams : register( b6 ) { float4 g_Params[N]; }
//     struct VertexOutput { float4 Position : SV_POSITION; float3 Normal : TEXCOORD0; float3 World : TEXCOORD1; float2 Uv : TEXCOORD2; float4 Color : TEXCOORD3; };
//
//   HLSL の直指定（FMaterialDefinition::CustomHlsl）は、上の宣言（と、MatNoise などの補助関数）の「あと」に置かれる。PixelInput（Surface は VertexOutput）を受けて、
//   SV_Target を返す関数 main を、書くこと。
//
//   補助関数: float MatNoise( float2 ) / MatHash( float2 ) / MatLinearDepth( float )（PostProcess）、マクロ MatMod( a, b )。
//=============================================================================
namespace NMaterialHlsl
{
	// グラフから作ったプログラムの、ピクセルシェーダーの全文。HLSL の直指定のプログラムなら、CustomBody を、宣言のあとに付ける
	FString Generate( const FMaterialProgram& Program, const FString& CustomBody = FString() );

	// Surface の標準の頂点シェーダー（頂点の位置をワールド行列とビュー射影行列で変え、法線・ワールド位置・UV・頂点の色を渡す）
	FString GetSurfaceVertexSource();
}
