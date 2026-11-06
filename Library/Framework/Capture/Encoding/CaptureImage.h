// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Framework/Capture/CaptureTypes.h"
#include "Library/Math/Color.h"

//=============================================================================
// NCaptureImage
//   撮った画面の画素から、矩形を切り出し、縮める（CPU だけ）。
//=============================================================================
namespace NCaptureImage
{
	// Width x Height の Pixels から Region を切り出し（画面の外は切り捨てる）、Downscale x Downscale の画素の平均で縮める（透明度も平均）。
	// 切り出せないとき（矩形が画面の外・画素が足りない）は空で、OutWidth / OutHeight は 0
	TArray<FColor32> Extract( TArrayView<const FColor32> Pixels, int32 Width, int32 Height, const FCaptureRegion& Region, int32 Downscale, int32& OutWidth, int32& OutHeight );
}
