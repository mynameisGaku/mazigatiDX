#pragma once

//=============================================================================
// DxLib.h をフレームワーク共通の設定で読み込む。
// DxLib の型（VECTOR / MATRIX / COLOR_U8 など）との変換が必要なヘッダーはこれを使う。
//=============================================================================

// windows.h の min / max マクロを無効化（std::min / std::max や Jolt と衝突するため）
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <DxLib.h>
