// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// 並べ替え可能な描画の分類。合成・深度・切り抜きの設定自体は変更しない。
enum class ESceneDrawClass : uint32
{
	Opaque, // 不透明。記録順を保って先に描く。
	Masked, // 切り抜き。不透明の後に記録順で描く。
	Translucent, // 半透明。指定した奥行きの大きい順に描く。
};
