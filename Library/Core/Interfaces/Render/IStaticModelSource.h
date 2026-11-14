// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/StaticModelDrawView.h"

// 描画はモデルの読み込み・複製・所有を知らず、準備済みの静的形状だけを借りる。
class IStaticModelSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IStaticModelSource() = default;
	// 無効なモデルはfalseで出力を保つ。借用は登録・更新・解放より前に消費する。
	virtual bool GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out ) = 0;
};
