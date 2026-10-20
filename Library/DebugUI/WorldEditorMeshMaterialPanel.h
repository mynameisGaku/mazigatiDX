// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITORMESHMATERIALPANEL_H
#define FRAMEWORK_DEBUGUI_WORLDEDITORMESHMATERIALPANEL_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Framework/Render/MeshRender.h"

namespace NWorldEditorUI
{
	// 役割: 材質効果の編集欄と選択meshの再生操作を描く。
	// Effect: 編集対象の保存値。変更がなければそのまま返す。
	// SourceTexture: この材質枠の画像パス。presetで画像も一緒に切り替える。
	// TextureFiles: 選択できる画像の一覧。
	// PreviewRenderer: 選択meshの再生操作先。準備前はnullptr。
	// bInputEnded: 数値入力やpreset操作が一段落した場合にtrueを返す。
	// 戻り値: EffectまたはSourceTextureが変わった場合はtrue。
	bool DrawMeshMaterialEffectPanel( FMeshMaterialEffectDefinition& Effect, FString& SourceTexture, const TArray<FString>& TextureFiles, FMeshRenderer* PreviewRenderer, bool& bInputEnded );
}

#endif
