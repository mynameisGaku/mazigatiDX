// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITORMODELPREVIEWKEY_H
#define FRAMEWORK_DEBUGUI_WORLDEDITORMODELPREVIEWKEY_H

#include "Library/Core/Collection/Array.h"

// ルートworldまたは入れ子prefab内で、model個体を識別する完全なobject経路。
struct FWorldEditorModelPreviewKey
{
	// rootからmodel objectまでのworld内Id。空は有効な鍵として使わない。
	TArray<int32> ObjectIds;

	// 要素数と全Idが一致するときだけ同じ個体として扱う。
	bool operator==( const FWorldEditorModelPreviewKey& Other ) const;
};

// TMap/TSet向けに経路全体を混ぜたhashを返す。
uint32 GetTypeHash( const FWorldEditorModelPreviewKey& Key );

#endif
