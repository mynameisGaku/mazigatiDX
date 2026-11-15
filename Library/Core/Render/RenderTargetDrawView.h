// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Core/Types/TypeDefinition.h"

// 描画先（レンダーターゲット）へ描くときに借りる、色と深度の資源の番号と大きさ。
struct FRenderTargetDrawView
{
	FTextureResourceHandle Color; // 色の描画先。
	FTextureResourceHandle Depth; // 専用の深度（無ければ無効）。
	int32 Width = 0, Height = 0; // 画素の大きさ。
};
