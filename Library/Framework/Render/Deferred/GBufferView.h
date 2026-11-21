// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FGBufferView
//   G-buffer の深度（カメラの前向きの奥行き / Far）と画面の位置（UV）から、世界の位置を求めるための、カメラの光線の基準。
//   Far の面（奥行き 1）の画面の端の位置を、射影行列と同じ規則で（逆行列を使わずに）求めて、「カメラから Far の面の点までのベクトル」を UV の一次式で表す。
//   位置は、このベクトルに深度を掛けてカメラの位置へ足すだけで求まる（正規化はしない。レンズのずらし・横の拡大も、この 3 点に含まれる）。
//   奥行きは、カメラの前向きに測るので、視錐台の中なら 0 以上 1 以下。Far の面の隅でも、距離のように 1 を超えない（1 は、何も描かれていない印に使える）。
//   正射影は、光線の始点が画面の位置で変わるので、扱わない。
//   ピクセルシェーダーの定数バッファへ、そのまま置ける（4 要素 x 4 = 64 バイト）。HLSL 側は EngineContent/Shaders/Deferred/GBufferRead.hlsli の cbGBufferView。
//   CPU にも同じ計算（ReconstructWorldPosition）を持ち、シェーダーの計算の基準として、誤差を検査する。
//
//   FGBufferView View;
//   if ( FGBufferView::Make( Camera, FCameraViewport { Width, Height }, View ) )
//   {
//       CommandList.ShaderConstants( EShaderStage::Pixel, CGBuffer::ViewConstantSlot, View );
//   }
//=============================================================================
struct FGBufferView
{
	// xyz: カメラの位置、w: Far（深度は、奥行きをこの値で割って保存する）
	FVector4 CameraPosition = FVector4( 0.0f, 0.0f, 0.0f, 1.0f );
	// xyz: 画面の左上（UV = 0, 0）の、カメラから Far の面の点までのベクトル
	FVector4 RayOrigin;
	// xyz: UV の x が 0 から 1 へ変わるときの、ベクトルの増分
	FVector4 RayU;
	// xyz: UV の y が 0 から 1 へ変わるときの、ベクトルの増分
	FVector4 RayV;

	// 透視射影のカメラと、映す先の大きさから作る。カメラや大きさが使えないとき、正射影のときは、何もせず false
	static bool Make( const FCamera& Camera, const FCameraViewport& Viewport, FGBufferView& OutView );

	// 世界の点 World の深度（カメラの前向きの奥行き / Far）。ジオメトリのパスのシェーダーが書く値と同じ（カメラが使えなければ 0）
	static float32 ToNormalizedDepth( const FCamera& Camera, const FVector3& World );

	// 画面の位置 Uv（左上が 0, 0、右下が 1, 1）を通る、カメラから Far の面の点までのベクトル
	FVector3 GetRayToFarPlane( const FVector2& Uv ) const;

	// 画面の位置 Uv に見える、深度 NormalizedDepth（カメラの前向きの奥行き / Far）の点の、世界の位置
	FVector3 ReconstructWorldPosition( const FVector2& Uv, float32 NormalizedDepth ) const;
};

static_assert( sizeof( FGBufferView ) == 64, "FGBufferView is placed into a shader constant buffer as it is" );
