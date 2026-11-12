// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Math/Color.h"

// オフスクリーン描画先へ描くパスの設定値。
struct FRenderTargetPassDesc
{
	// 書き込み先の色描画先（CreateRenderTargetRGBA8で作った有効な番号）。
	FTextureResourceHandle ColorTarget;
	// 書き込み先の深度（任意。有効な場合はCreateDepthTextureD32で作った同寸法の番号）。
	FTextureResourceHandle DepthTarget;
	// 2D描画の拡大率。論理座標に掛けると描画先の画素座標になる。
	float32 Scale = 1.0f;
	// bClearColorがtrueのとき、描画前に色描画先を塗る色。
	FColor32 ClearColor = FColor32( 0, 0, 0, 255 );
	// bClearDepthがtrueのとき、描画前に深度を初期化する値（0.0〜1.0）。
	float32 ClearDepth = 1.0f;
	// パス開始時に色描画先をClearColorで塗るか。falseなら既存内容を保つ。
	bool bClearColor = true;
	// パス開始時に深度をClearDepthで初期化するか。falseなら既存深度を保つ。
	// 深度番号を省略した初回パスでは、内部深度を1で初期化してから描く。
	bool bClearDepth = true;
};
