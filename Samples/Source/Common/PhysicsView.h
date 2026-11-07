// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"

//=============================================================================
// NPhysicsSampleView
//   物理の世界（メートル。Y が上向き）と、サンプルの画面（ピクセル。Y が下向き。左下が世界の原点）の、変換。
//   横から見た 2D の物理のサンプルで、物体を描くときと、マウスの位置から世界の点を求めるときに使う。
//=============================================================================
namespace NPhysicsSampleView
{
	// 1 メートルのピクセル数（1280 x 720 の画面は、25.6 x 14.4 メートル）
	constexpr float32 PixelsPerMeter = 50.0f;

	int32 ToScreenX( float32 X );
	int32 ToScreenY( float32 Y );

	// 画面の点（ピクセル）に対応する、世界の点（Z は 0）
	FVector3 ToWorld( int32 ScreenX, int32 ScreenY );
}
