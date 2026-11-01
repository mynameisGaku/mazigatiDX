// SPDX-License-Identifier: MIT
#pragma once
#include "Library/DebugUI/ImGuiTexture.h"

//=============================================================================
// NDxLibImGuiTexture
//   NDebugUI の画像表示の窓口に差し込む、DxLib（Direct3D 11）の実装。
//   CDxLibBackend が、初期化のときに SetImGuiTextureCallbacks( MakeCallbacks() ) で登録し、終了のときに外す。
//=============================================================================
namespace NDxLibImGuiTexture
{
	NDebugUI::FImGuiTextureCallbacks MakeCallbacks();
}
