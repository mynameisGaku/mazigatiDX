// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// windows.h を、フレームワーク共通の設定で読み込む（Windows の API を直接使う、Platform の実装のファイルだけが使う）。
// DxLib は読まない（DxLib を使うのは Platform/DxLib の中だけ）。
//=============================================================================

// windows.h の min / max マクロを無効化（std::min / std::max や Jolt と衝突するため）
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// windows.h の、フレームワークの関数名とぶつかるマクロ（SetProp -> SetPropA など）を取り消す
#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
#ifdef GetObject
#undef GetObject
#endif
