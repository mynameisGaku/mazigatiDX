// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/ImageTypes.h"

//=============================================================================
// IImage
//   描画の窓口（FDrawContext）に渡せる画像。描画の側は、画像の持ち主（CTexture・将来の描画先など）の具体的な型を知らずに、このインターフェースだけを見る。
//
//   TSharedPtr<CTexture> Hero = Assets->LoadTexture( "Hero.png" );     // CTexture は IImage を実装している
//   Draw.Image( 100.0f, 50.0f, Hero );                                 // Hero が nullptr・無効なら、何も描かない
//
//   実装する側の約束:
//   - GetImageHandle() は const で、描画システムのワーカーのスレッドからも呼ばれる。状態を変えず、ロックを取らない
//   - 画像が作られていない・読み込めなかったときは、無効なハンドル（FImageHandle()）を返す
//=============================================================================
class IImage
{
public:
	virtual ~IImage() = default;

	// この画像を指すハンドル。描画の窓口は、記録のときにこれを読む（描かれるときまで、画像は手放さないこと）
	virtual FImageHandle GetImageHandle() const = 0;
};
