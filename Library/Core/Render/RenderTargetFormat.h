// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Math/Color.h"

// オフスクリーン色描画先で扱う画素形式。Framework層のERenderTargetFormatと区別するためColorFormatと名付ける。
enum class ERenderTargetColorFormat : uint8
{
	RGBA8, // R8G8B8A8_UNORM。通常描画・compute・撮影でそのまま共用できる形式。
	RGBA16F, // R16G16B16A16_FLOAT。1.0超や負値を保持できるHDR描画先・compute入力共用形式。
};

// オフスクリーン描画パス（MRT含む）に束縛する1枚の色描画先と開始時消去設定。
struct FRenderTargetColorAttachment
{
	// 書き込み先の色描画先（CreateRenderTargetRGBA8で作った有効な番号）。
	FTextureResourceHandle Target;
	// bClearがtrueのとき、パス開始時にこの描画先を塗る色。
	FColor32 ClearColor = FColor32( 0, 0, 0, 255 );
	// パス開始時にこの描画先をClearColorで塗るか。falseなら既存内容を保つ。
	bool bClear = true;
};
