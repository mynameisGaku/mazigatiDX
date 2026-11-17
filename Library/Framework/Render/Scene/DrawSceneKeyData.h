// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Render/Scene/SceneDrawClass.h"

// 直後の独立した3D描画だけに付ける分類と奥行き。有限な奥行きと有効な分類が必要。
struct FDrawSceneKeyData
{
	ESceneDrawClass Class = ESceneDrawClass::Opaque; // 材質の描画順の分類。
	float32 Depth = 0.0f; // 同じビューで比較できる奥行き。大きいほど奥。
};

static_assert( sizeof( FDrawSceneKeyData ) == 8, "scene draw keys must use exactly two words" );
