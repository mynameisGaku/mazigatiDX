// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/TextureDrawView.h"

// 画像の所有・読み込みから描画側を切り離す。異なるAPI間でBindingを共有しない。
class ITextureDrawSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~ITextureDrawSource() = default;
	// 有効な登録画像だけ返す。出力は次のフレーム開始まで有効。失敗時は出力を保つ。
	virtual bool GetTextureDrawView( int32 Handle, FTextureDrawView& Out ) = 0;
};
