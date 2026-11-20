// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Render/RenderTargetPassDesc.h"

// 描画パスの登録値。画像の番号はグラフへ先に取り込む。
struct FRenderGraphPassDesc
{
	FString Name; // 差し替え・挿入・診断に使う一意の名前。
	TArray<FTextureResourceHandle> Reads; // シェーダーなどが読み取る画像。描画先と重複できない。
	TArray<FRenderTargetColorAttachment> Colors; // 1〜8枚の色描画先。初回の書き込みには消去が必要。
	FTextureResourceHandle Depth; // 任意の明示的な深度描画先。
	float32 ClearDepth = 1.0f; // 深度を消去するときに書く値。
	bool bClearDepth = true; // falseなら既存の深度を保持する。
};
