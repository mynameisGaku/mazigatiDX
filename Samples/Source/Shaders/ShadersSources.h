// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/Shaders/ShadersDemoState.h"

class CAssetManager;

//=============================================================================
// NShadersSources
//   サンプル Shaders で使うシェーダーの HLSL のソースと、シェーダーに渡す定数の構造体。
//   ソースは、実行のときに CShader::Compile でコンパイルする（Windows に入っている d3dcompiler_47.dll を使う）。
//
//   頂点シェーダーの入力は、FShaderVertex3D の並びのとおりに、先頭から飛ばさずに宣言する（DxLib が、宣言した順に詰めて読むため）。
//   定数は cbuffer で受ける（register( b4 ) 〜 register( b13 )）。4x4 の行列は row_major float4x4 で受ける（FMatrix4x4 のメモリの並びがそのまま入る）。
//   頂点シェーダーとピクセルシェーダーで、同じ番号のスロットは別物なので、両方に同じ内容を渡している。
//=============================================================================
namespace NShadersSources
{
	// 場面の定数（スロット 4）。HLSL の cbScene と同じ並び
	struct FSceneConstants
	{
		// ワールド座標 -> クリップ座標（ビュー行列 x 射影行列）
		FMatrix4x4 ViewProjection;
		// 光が進む向き（xyz。長さ 1）
		FVector4 LightDirection;
		// カメラの位置（xyz）
		FVector4 CameraPosition;
		// x: 時間（秒）、y: カメラの Near、z: カメラの Far
		FVector4 Time;
	};

	// 物 1 つの定数（スロット 5）。HLSL の cbObject と同じ並び
	struct FObjectConstants
	{
		// ローカル -> ワールド
		FMatrix4x4 World;
		// 色（rgb）
		FVector4 Color;
		// x: うねりの大きさ（0 ならうねらない）
		FVector4 Options;
	};

	// ポストエフェクトの定数（スロット 4）。HLSL の cbPost と同じ並び
	struct FPostConstants
	{
		// x: 時間（秒）、y: 画像の幅、z: 画像の高さ
		FVector4 Parameters;
	};

	// すべてのシェーダーをコンパイルして、State に入れる。1 つでも失敗したら false（理由は、ログに出す）
	bool CompileAll( FShadersDemoState& State );
}
