// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTFLIPBOOKFRAMES_H
#define FRAMEWORK_EFFECT_EFFECTFLIPBOOKFRAMES_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Effect/EffectFlipbookDefinition.h"

class CAssetManager;

// 再生開始時に固定した時間軸と、クリップ順の派生画像を所有する値。
struct FEffectFlipbookFrames
{
	// 元クリップの再生順を保持する時間軸。
	FAnimTimeline Timeline;
	// Timeline.StepSource が参照する元クリップ順のコマ画像。
	TArray<TSharedPtr<CTexture>> Frames;
};

namespace NEffectFlipbook
{
	// シートの検証済みスナップショットを派生画像付きで組み立て、失敗理由を返す。
	bool BuildFrames( const FEffectFlipbookDefinition& Definition, CAssetManager& Assets, FEffectFlipbookFrames& Out, FString* OutError = nullptr );
	// シート、選択クリップ、矩形、時間軸を調べる。派生GPU画像は作らない。
	TArray<FString> ValidateAssets( const FEffectFlipbookDefinition& Definition, CAssetManager& Assets );
}

#endif
