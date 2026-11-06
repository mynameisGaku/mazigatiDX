// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/Lighting.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// NIslandScenery
//   サンプル Island の舞台を記録する関数と、時刻で変わる空・太陽の決め方。
//     地形      頂点バッファ + 添字バッファ（PrimitiveIndexedBuffer3D）。影を落とし、受ける
//     海        半透明の大きな四角形（Primitive3D。頂点のアルファで透ける）
//     木・灯台・探検家  3D 図形（Cone3D / Cube3D / Capsule3D / Sphere3D）。影を落とす
//     結晶      光の計算を切った八面体と、加算で重ねる光のにじみ（Billboard）
//     灯台の光   回るスポットライト（影を受けない）。夕方ほど強い
//     空の明かり  真上からの弱い光（影を受けない）。影の中も、真っ黒にしない
//   太陽は一日の時刻で空を動き、空・霧・光の色も変わる。
//=============================================================================
namespace NIslandScenery
{
	// 太陽の光が進む向き（長さ 1。下向き）
	FVector3 GetSunDirection( const FIslandState& State );

	// 空の上と地平線の色（時刻で変わる。地平線の色は、霧の色にも使う）
	FColor32 GetSkyTopColor( const FIslandState& State );
	FColor32 GetHorizonColor( const FIslandState& State );

	// 夕方らしさ（0: 昼、1: 日の出・日の入り）
	float32 GetDuskAmount( const FIslandState& State );

	// 結晶が、いまどこに見えるか（上下に揺れる）
	FVector3 GetCrystalPosition( const FIslandState& State, int32 Index );

	// 影の絵を作る（メインスレッド）。作れなければ false
	bool MakeShadowMap( FIslandState& State );

	// 舞台を記録する（カメラは、呼ぶ側が先に PushCamera しておく）。影の絵を描き、ライトと霧を決め、地形・海・木・灯台・探検家・結晶を描く
	void RecordStage( FDrawContext& CommandList, const FIslandState& State );
}
