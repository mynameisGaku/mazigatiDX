// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelEffectTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"

//=============================================================================
// NPixelBackdrop
//   プレビュー用の背景画像を生成する補助関数
//=============================================================================
namespace NPixelBackdrop
{
	// Width x Height の背景画像を作る（GroundY が負のときは Height を地面の位置とする）
	FPixelImage MakeBackdrop( EPixelBackdrop Kind, int32 Width, int32 Height, int32 GroundY = -1 );
}
