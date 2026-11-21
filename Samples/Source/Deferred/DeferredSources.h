// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/Deferred/DeferredDemoState.h"

class CAssetSubsystem;

//=============================================================================
// NDeferredSources
//   サンプル Deferred の HLSL（頂点・ピクセル）の読み込みとコンパイルと、シェーダーの定数バッファと同じ並びの構造体。
//   Forward と Deferred は、同じ光（Lighting）と同じ見せ方（ShadeView）を使うので、同じ場面が、同じ絵になる。
//   G-buffer の書き出しと読み出し（PackGBuffer / LoadGBuffer）は、ライブラリの NDeferredShaders の HLSL を使う。
//=============================================================================
namespace NDeferredSources
{
	// 場面の定数（b4。頂点・ピクセルの両方に置く）
	struct FSceneConstants
	{
		FMatrix4x4 ViewProjection;
		FVector4 CameraPosition;                                        // xyz: カメラの位置、w: Far
		FVector4 Parameters;                                            // x: 時間、y: 見せるもの（0〜5）、zw: 場面の描画先の大きさ（ピクセル）
		FVector4 TileInfo;                                              // x: タイル 1 枚の大きさ（ピクセル）、y: クラスタ 1 つの横の画素の数、z: タイルの一覧を使うか（1 / 0）
		FVector4 ClusterInfo;                                           // x: 奥行きの層の数、y: タイルの行の数、z: Near、w: Far
		FVector4 LightPositions[FDeferredDemoState::NumLights];        // xyz: 位置、w: 光の届く半径
		FVector4 LightColors[FDeferredDemoState::NumLights];           // rgb: 色 x 明るさ
	};

	// 物 1 つの定数（b5。頂点・ピクセルの両方に置く）
	struct FObjectConstants
	{
		FMatrix4x4 World;
		FVector4 Albedo;
		FVector4 Emission;
	};

	// 画面への合成の定数（b4。ピクセルだけ）
	struct FCompositeConstants
	{
		FVector4 Parameters; // x: 露出、y: トーンマップを掛けるか（仕上がりのときだけ 1。そのほかの見せ方は、値をそのまま見せる）
	};

	// サンプルの HLSL（Samples/Assets/Deferred のファイル。実行時に読む）を、製品の G-buffer の部品と連ねて、すべてコンパイルして、State のシェーダーへ入れる。
	// 1 つでも失敗したら（理由はログに出す）false
	bool CompileAll( FDeferredDemoState& State, CAssetSubsystem& Assets );
}
