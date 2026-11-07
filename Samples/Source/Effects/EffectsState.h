// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FEffectsEntry
//   ギャラリーに並べる、エフェクト 1 つ。台座の上で、再生される。
//=============================================================================
struct FEffectsEntry
{
	// 台座に書く名前と、エフェクトのファイル（資産のルートからの相対）
	FString Title;
	FString Path;

	// 説明（画面の下に出す）
	FString Description;

	// 再生中のエフェクト（CEffectsGallery が作る）
	TSharedPtr<IEffectPlayback> Playback;

	// 大きさの倍率（上下キーで変える）
	float32 Scale = 1.0f;

	// 台座の中心の位置（床の上）
	FVector3 Pedestal;
};

//=============================================================================
// FEffectsState
//   Effects のオブジェクトが分け合う状態。並べたエフェクト、選んでいる番号、カメラ、マウスの指す位置（世界の点）。
//=============================================================================
struct FEffectsState
{
	static constexpr int32 ScreenWidth = 1280;
	static constexpr int32 ScreenHeight = 720;

	TArray<FEffectsEntry> Entries;
	int32 Selected = 0;
	FCamera Camera;

	// マウスが指している、舞台（Z = 0 の面）の上の点。雷の終点と、尾を引く帯の先頭に使う
	FVector3 MouseWorld;

	float32 Time = 0.0f;

	FCameraViewport GetViewport() const { return FCameraViewport { ScreenWidth, ScreenHeight }; }
};
