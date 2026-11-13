// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/ComputeTextureFormat.h"

// カスタムグラフィックスパイプラインが読む二次元画像の場所と成分形式（space 0、register(tN)）。
struct FGraphicsTextureSlot
{
	// シェーダーのt番号（0〜127）。bufferのtスロットと番号を重複させない。
	uint32 Binding = 0;
	// RGBA8／RGBA16FまたはR32Float（単一成分カラー・読み取れる深度）の資源形式。DX12ではR32Floatをfloat型のスカラー・ベクトルから読める。
	EComputeTextureFormat Format = EComputeTextureFormat::RGBA8;
};
