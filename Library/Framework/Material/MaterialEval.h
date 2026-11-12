// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Material/MaterialProgram.h"

//=============================================================================
// NMaterialEval: 中間表現を、CPU で評価する
//   GPU を使わずに、マテリアルの値を計算する。テストと、エディタのノードのプレビュー（小さな画像）に使う。GPU に出す HLSL（MaterialHlsl）と、同じ計算になるよう
//   作ってある（ノイズなど、浮動小数点の細かな違いは、わずかに出ることがある）。
//
//   FMaterialEvalContext Context = NMaterialEval::MakeContext( Program );   // パラメーターは、既定の値
//   Context.Uv = FVector2( 0.25f, 0.5f );
//   Context.Time = 1.0f;
//   TArray<FVector4> Values;
//   NMaterialEval::Evaluate( Program, Context, Values );
//   const FVector4 Color = NMaterialEval::GetOutput( Program, Values, "Color" );
//=============================================================================
struct FMaterialEvalContext
{
	FVector2 Uv = FVector2( 0.0f, 0.0f );
	float32 Time = 0.0f;
	FVector2 Size = FVector2( 1.0f, 1.0f );
	float32 Hover = 0.0f;
	float32 Press = 0.0f;
	float32 Value = 0.0f;
	float32 Enabled = 1.0f;
	FVector4 VertexColor = FVector4( 1.0f, 1.0f, 1.0f, 1.0f );
	FVector3 WorldPosition = FVector3( 0.0f, 0.0f, 0.0f );
	FVector3 WorldNormal = FVector3( 0.0f, 1.0f, 0.0f );
	FVector3 CameraVector = FVector3( 0.0f, 0.0f, 1.0f );
	float32 ParticleLife = 0.0f;     // Particle: 寿命の位置（0〜1）
	float32 ParticleAge = 0.0f;      // Particle: 生まれてからの秒
	float32 ParticleRandom = 0.0f;   // Particle: 粒子ごとの 0〜1 の数
	float32 ParticleSpeed = 0.0f;    // Particle: 速さ（ワールドの長さ / 秒）

	// パラメーターの値（Params の Index ごと。足りない分は、既定の値）
	TArray<FVector4> ParamValues;

	// 画像を読む関数（置き場の番号・UV → 色）。無ければ、白。シーンの色（置き場 0）も、これで読む
	TCallable<FVector4( int32 Slot, const FVector2& Uv )> SampleTexture;
	// シーンの深度（カメラからの距離）。無ければ 1
	TCallable<float32( const FVector2& Uv )> SceneDepth;
};

namespace NMaterialEval
{
	// 値のノイズ（0〜1）。HLSL の MatNoise と同じ計算
	float32 Noise( float32 X, float32 Y );

	// プログラムの既定のパラメーターの値が入った文脈
	FMaterialEvalContext MakeContext( const FMaterialProgram& Program );

	// すべての命令を評価する。OutValues[i] が、命令 i の値（使う成分は、型による）。プログラムが無効（bValid が false）・HLSL の直指定なら false
	bool Evaluate( const FMaterialProgram& Program, const FMaterialEvalContext& Context, TArray<FVector4>& OutValues );

	// 出力ピンの値（見つからなければ、0。bOutFound に結果）
	FVector4 GetOutput( const FMaterialProgram& Program, const TArray<FVector4>& Values, const FString& Pin, bool* bOutFound = nullptr );
	// ノードの値（そのノードが、コンパイルされていなければ、0）
	FVector4 GetNodeValue( const FMaterialProgram& Program, const TArray<FVector4>& Values, int32 NodeId, bool* bOutFound = nullptr );
}
