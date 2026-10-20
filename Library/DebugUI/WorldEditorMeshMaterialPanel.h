// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITORMESHMATERIALPANEL_H
#define FRAMEWORK_DEBUGUI_WORLDEDITORMESHMATERIALPANEL_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Framework/Render/MeshRender.h"

namespace NWorldEditorUI
{
	// 材質効果の保存値と選択objectの一時再生操作を描き、変更と入力終了を返す。
	bool DrawMeshMaterialEffectPanel( FMeshMaterialEffectDefinition& Effect, FString& SourceTexture, const TArray<FString>& TextureFiles, FMeshRenderer* PreviewRenderer, bool& bInputEnded );
}

#endif
