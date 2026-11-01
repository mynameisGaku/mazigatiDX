// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

//=============================================================================
// 撮影のための描き直し（IDrawExecutor::BeginOffscreenCapture）の設定
//   画面とは別の、大きな描画先へ、同じ描画命令をもう一度実行して撮る（高解像度のスクリーンショット・被写体だけの撮影）。
//   3D は描画先の大きさに合わせて細かくなり、2D（座標が画面の画素のもの）は Scale 倍に拡大して描く。
//=============================================================================
struct FOffscreenCaptureDesc
{
	// 描画先の大きさ（画素。1〜16384）
	int32 Width = 0;
	int32 Height = 0;
	// 2D の描画を何倍に拡大するか（画面の大きさ x Scale が、描画先の大きさになるようにする）
	float32 Scale = 1.0f;
	// 描き始める前に、描画先を塗る色
	FColor32 ClearColor = FColor32( 0, 0, 0, 255 );
};
