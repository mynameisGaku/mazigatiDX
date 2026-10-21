// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

struct FWorldEditorState;

namespace NWorldEditorUI
{
	// 指定Model配置の保存値、材質情報、準備済み個体のtransportを描く。
	// State: World Editor状態。ObjectId: 最新Documentから引き直す配置Id。
	void DrawModelMaterialEffectPanel( FWorldEditorState& State, int32 ObjectId );
}
