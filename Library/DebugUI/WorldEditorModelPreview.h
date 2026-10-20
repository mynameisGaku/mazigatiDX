// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITORMODELPREVIEW_H
#define FRAMEWORK_DEBUGUI_WORLDEDITORMODELPREVIEW_H

#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/World/WorldDefinition.h"

// エディタだけが所有する一個体のsource、再生状態、設定snapshot。
struct FWorldEditorModelPreview
{
	// 他の個体やAssetInfo共有instanceと分離した再生状態。
	FModelAnimator Animator;
	// Animatorのinstanceと効果resourceが参照する元model。
	TSharedPtr<CModel> Source;
	// 再構築判定に使う直近のworld設定値。
	FWorldModel Definition;
	// capability確認に使った借用executor。
	const IDrawExecutor* Executor = nullptr;
	// 直近更新時のexecutor対応状態。
	bool bExecutorSupported = false;
	// sourceの準備完了状態。非同期load完了時に再構築する。
	bool bSourceReady = false;
	// 失敗も同じ入力では繰り返さないための構築済み印。
	bool bBuilt = false;
	// native fallbackになった理由。成功時は空。
	FString FailureReason;
};

#endif
