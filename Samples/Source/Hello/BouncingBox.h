// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CBouncingBox
//   四角形が、一定の速さで動き、範囲の端で跳ね返る。
//   Tick の DeltaTime（前のフレームからの秒）を使うと、フレームの速さに関係なく、同じ速さで動く見本。
//=============================================================================
class CBouncingBox : public CGameObject
{
public:
	// InPosition は最初の中心、InVelocity は 1 秒に進む距離（画面の座標。ピクセル）、InHalfSize は四角形の半分の大きさ、
	// InAreaMin / InAreaMax は動ける範囲（四角形の中心でなく、四角形全体が収まる範囲）
	CBouncingBox( const FVector2& InPosition, const FVector2& InVelocity, int32 InHalfSize, const FColor32& InColor, const FVector2& InAreaMin, const FVector2& InAreaMax );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	// 今の中心（画面の座標）
	FORCEINLINE const FVector2& GetPosition() const { return Position; }

private:
	// 中心（画面の座標。ピクセル）
	FVector2 Position;

	// 1 秒に進む距離（ピクセル）。端で跳ね返ると、その向きが反対になる
	FVector2 Velocity;

	// 四角形の半分の大きさ（ピクセル）
	int32 HalfSize;

	// 四角形の色
	FColor32 Color;

	// 動ける範囲の左上と右下（ピクセル）
	FVector2 AreaMin;
	FVector2 AreaMax;
};
