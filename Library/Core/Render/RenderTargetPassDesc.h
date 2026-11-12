// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Render/RenderTargetFormat.h"
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Math/Color.h"

// オフスクリーン描画先へ描くパスの設定値。
struct FRenderTargetPassDesc
{
	// 1パスで同時に束縛できる色描画先の最大枚数。
	static constexpr uint32 MaxColorTargets = 8;

	// 書き込み先の色描画先（単一指定用。ColorTargetsが空のときに使用する）。
	FTextureResourceHandle ColorTarget;
	// 書き込み先の深度（任意。有効な場合はCreateDepthTextureD32で作った同寸法の番号）。
	FTextureResourceHandle DepthTarget;
	// 2D描画の拡大率。論理座標に掛けると描画先の画素座標になる。
	float32 Scale = 1.0f;
	// bClearColorがtrueのとき、描画前にColorTargetを塗る色。
	FColor32 ClearColor = FColor32( 0, 0, 0, 255 );
	// bClearDepthがtrueのとき、描画前に深度を初期化する値（0.0〜1.0）。
	float32 ClearDepth = 1.0f;
	// パス開始時にColorTargetをClearColorで塗るか。falseなら既存内容を保つ。
	bool bClearColor = true;
	// パス開始時に深度をClearDepthで初期化するか。falseなら既存深度を保つ。
	// 深度番号を省略した初回パスでは、内部深度を1で初期化してから描く。
	bool bClearDepth = true;
	// 複数描画先（MRT、1〜8枚）を明示指定する列。非空時はColorTargetを同時に指定しない。
	TArrayView<const FRenderTargetColorAttachment> ColorTargets;
};
