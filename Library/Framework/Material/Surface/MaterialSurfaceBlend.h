// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// Surfaceの不透明度の扱い。Manualは呼び出し側が描画状態を指定する。
enum class EMaterialSurfaceBlend : uint8
{
	Manual, // 従来の定義と同じく、呼び出し側が分類する。
	Opaque, // 全画素を上書きし、深度を書く。
	Masked, // しきい値を超えた画素だけ上書きし、深度を書く。
	Translucent, // アルファ合成し、深度は読むだけ。
};
