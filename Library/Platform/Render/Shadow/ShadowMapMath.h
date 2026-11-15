// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Math/Matrix4x4.h"

//=============================================================================
// NShadowMapMath
//   シャドウマップ（影の絵）を描くときの、光から見た行列を作る。描画 API を知らない。
//   影は、光が進む向きに沿って平行に落ちる（平行投影）。影を計算する範囲（ワールドの箱）は、命令に指定があればそれ、無ければ、
//   始めるときのカメラの視錐台を囲む箱。その箱の 8 つの角が、光から見て、ちょうど収まる平行投影を作る（箱の外には影ができない）。
//   行列は、ワールドの点 → 光のクリップ座標（x, y が -1〜1、奥行きが 0〜1）。FMatrix4x4 と同じ、行ベクトルの流儀。
//=============================================================================
namespace NShadowMapMath
{
	// 命令の値から、光から見たビュー x 射影の行列を作る。カメラは、範囲を自動で決めるとき（HasArea が 0）だけ使う。
	// 値が使えない（向きの長さ 0・範囲が逆・有限でない・箱の幅が 0）ときは false（Out を変えない）。
	bool ComputeLightViewProjection( const FDrawBeginShadowMapData& Data, const FCamera& Camera, const FCameraViewport& Viewport, FMatrix4x4& Out );
}
