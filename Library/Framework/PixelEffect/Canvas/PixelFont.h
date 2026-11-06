// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectTypes.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NPixelFont
//   ドット文字の寸法計測と画素ごとの走査
//=============================================================================
namespace NPixelFont
{
	// 拡大前の文字幅（ピクセル）と高さ（ピクセル）を返す
	int32 MeasureWidth( const FString& Text, EPixelFont Font = EPixelFont::Mini );
	int32 MeasureHeight( EPixelFont Font = EPixelFont::Mini );

	// 左上を (0, 0) として、インクが乗る各ドット座標 (GlyphX, GlyphY) に対して Callback を呼ぶ
	void Rasterize( const FString& Text, EPixelFont Font, const TCallable<void( int32, int32 )>& Callback );
}
