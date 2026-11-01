// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/PostEffects/PostEffectsDemoState.h"

//=============================================================================
// NPostEffectsSources
//   サンプル PostEffects で使うシェーダーの HLSL のソースと、シェーダーに渡す定数の構造体。
//   ソースは、実行のときに CShader::Compile でコンパイルする。
//
//   場面のピクセルシェーダーは、出力を 2 つ持つ（SV_Target0: 色、SV_Target1: 深度）。
//   SV_Target0 は、いまの描画先（PushTarget で決めた SceneColor）に、SV_Target1 は、ShaderOutputTarget( 1, SceneDepth ) で決めた描画先に書かれる。
//   色は 16 ビットの浮動小数点の描画先に書くので、光る玉の 4 や 6 といった、1 を超える明るさがそのまま残る。
//=============================================================================
namespace NPostEffectsSources
{
	// 場面の定数（スロット 4）。HLSL の cbScene と同じ並び
	struct FSceneConstants
	{
		// ワールド座標 -> クリップ座標（ビュー行列 x 射影行列）
		FMatrix4x4 ViewProjection;
		// 月の光が進む向き（xyz。長さ 1）
		FVector4 LightDirection;
		// カメラの位置（xyz）
		FVector4 CameraPosition;
		// x: 時間（秒）、y: カメラの Far（深度は、距離をこれで割って 0〜1 にする）
		FVector4 Time;
		// 光る玉の位置（xyz）と色（rgb。明るさを掛けたもの。1 を超える）。玉は、近くを照らす点の光にもなる
		FVector4 OrbPositions[FPostEffectsDemoState::NumOrbs];
		FVector4 OrbColors[FPostEffectsDemoState::NumOrbs];
	};

	// 物 1 つの定数（スロット 5）。HLSL の cbObject と同じ並び
	struct FObjectConstants
	{
		// ローカル -> ワールド
		FMatrix4x4 World;
		// 地の色（rgb。光を受けて見える色）
		FVector4 Albedo;
		// 自分で光る色（rgb。明るさを掛けたもの。1 を超えてよい）
		FVector4 Emission;
	};

	// ポストエフェクトの定数（スロット 4）。HLSL の cbPost と同じ並び
	struct FPostConstants
	{
		// x: 露出、y: 明るい所を抜き出す境目、z: ブルームの強さ、w: 霧の濃さ（距離 1 あたり）
		FVector4 Parameters;
		// xy: 読む画像の 1 画素の大きさ（uv）、zw: ぼかす向き（1, 0 = 横、0, 1 = 縦）x 広がり
		FVector4 Sampling;
		// x: 見せるもの、y: トーンマップ、z: ブルームを足すか（1 / 0）、w: 霧をかけるか（1 / 0）
		FVector4 Modes;
		// 霧の色（rgb）と、カメラの Far（w。深度 0〜1 を距離に直す）
		FVector4 FogColor;
	};

	// すべてのシェーダーをコンパイルして、State に入れる。1 つでも失敗したら false（理由は、ログに出す）
	bool CompileAll( FPostEffectsDemoState& State );
}
