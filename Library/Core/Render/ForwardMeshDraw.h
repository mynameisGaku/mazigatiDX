// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawVertex3D.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/StaticModelDrawView.h"

//=============================================================================
// FForwardMeshDraw
//   GPU が変換と光の計算をする、Forward の 1 回の描画（共有のメッシュの、材質 1 つぶんの範囲）。
//   メッシュ（頂点と添字）は、MeshKey で GPU に 1 度だけ置かれ、個体（配置・光・不透明度）ごとに、行列と定数だけを替えて使い回す。
//   頂点の中身が変わる（アニメーション・骨の上書き）ときは、MeshVersion を替える（実行先は、置き直す）。
//   値は、描画 API に依存しない（行列は、FMatrix4x4 と同じ行ベクトルの並び: 行 0〜3 の順に 16 個）。借用は、呼び出しの間だけ。
//=============================================================================
struct FForwardMeshDraw
{
	// 光の定数の float の数（先頭の 7 つの float4 + ライト 6 つぶんの 7 つの float4。並びは、画素ごとの光の定数 b6 と同じ）。
	static constexpr int32 NumLightFloats = ( 7 + 6 * 7 ) * 4;

	bool bTransient = false; // trueならメッシュを登録せずフレームの転送領域から描く。
	bool bDepthOnly = false; // 影の絵（R32Float）へ、光から見た深度だけを書く。ClipMatrix は、ローカルから光のクリップ座標へ。光・フォグ・影・画像は使わない。
	uint64 MeshKey = 0;                      // 登録する共有メッシュの識別（0以外）。bTransientなら使わない。
	uint64 MeshVersion = 0;                  // 頂点の中身の版。変わったら、GPU の頂点を置き直す。
	const FDrawVertex3D* Vertices = nullptr; // メッシュの頂点（変換前）。
	int32 NumVertices = 0;                   // 頂点の数（登録するメッシュは 1〜NGraphicsPipelineLimits::MaxRegisteredVertices、登録しない描画は 1〜MaxTransientVertices = 65536）。
	const uint32* Indices = nullptr;         // メッシュ全体の添字。
	int32 NumIndices = 0;                    // 添字の数（3 の倍数）。
	int32 FirstIndex = 0;                    // 描く範囲の先頭（3 の倍数）。
	int32 IndexCount = 0;                    // 描く範囲の添字の数（3 の倍数。登録するメッシュは 1〜NGraphicsPipelineLimits::MaxRegisteredIndices、登録しない描画は 1〜MaxTransientIndices = 196608）。
	// GPU スキニング（SkinVertices があるとき）。Vertices は、骨の変形の前の頂点で、頂点シェーダーが、骨の行列で変形する。登録するメッシュ（bTransient でない）だけ。
	const FStaticModelSkinVertex* SkinVertices = nullptr; // Vertices と同じ数の、骨と重み。
	const float32* BoneMatrices = nullptr;                // NumBones 個の骨の行列（1 つは、3 つの float4: 行列の 1〜3 列）。
	int32 NumBones = 0;                                   // 骨の行列の数（1 〜 MaxStaticModelSkinBones）。
	int32 Texture = -1;                      // 画像の番号（負なら、なし）。
	float32 ClipMatrix[16] = {};             // ローカルからクリップへ（ワールド * ビュー * 射影 + 画面のずれ）。
	float32 World[16] = {};                  // ローカルからワールドへ。
	float32 NormalMatrix[16] = {};           // 法線をワールドへ（ワールドの逆行列の転置）。
	float32 Lighting[NumLightFloats] = {};   // 光・材質・カメラ（b6 と同じ並び）。
	float32 Bright[3] = { 1.0f, 1.0f, 1.0f }; // 描画の明るさ。
	float32 AlphaScale = 1.0f;               // 頂点のアルファに掛ける割合（不透明度）。
	bool bLit = false;                       // 光を計算するか（false なら、拡散色 + 鏡面色）。
	float32 UvTransform[8] = { 1, 1, 0, 0, 0, 0, 0, 1 }; // 拡大UV・中心UV・移動UV・回転のsin/cos。既定は変換なし。
	bool bPixelLighting = false; // trueなら補間した世界の位置と法線から画素ごとに照明する。
	float32 Fog[20] = {}; // 奥行きの平面、距離の設定と色、高さの設定と色（各float4）。
	FGeometryDrawState State;                // 深度・表裏・合成・画像の外側。
	// 影を受ける（光を計算する描画だけ）。影を受けるライトの分は、影の絵と比べた割合を掛けて足す。距離・高さのフォグは、影を受けるときは掛けない。
	int32 ShadowMap = -1;                    // 影の絵の画像の番号（負なら、影を受けない）。
	int32 ShadowMask = 0;                    // 影を受けるスロットの印（ライトの ShadowMapSlots と重なるライトが、影を受ける）。
	float32 LightViewProjection[16] = {};    // ワールドから、光のクリップ座標へ（FMatrix4x4 と同じ並び）。
	float32 ShadowParams[4] = {};            // 影の絵の幅・高さ・補正の深さ・0。
};
