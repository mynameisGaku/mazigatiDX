// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IAIAnimationControl
//   AI（ステートマシン・ビヘイビアツリー）が、オブジェクトのアニメーションを操作する窓口。
//   フレームワークはゲームのアニメーター（スプライト・モデルなど）を知らないので、ゲーム側がこれを実装して
//   CStateMachineComponent などへ渡す（SetAnimationControl）。
//
//   - クリップは名前で指す。ステートの Clip 指定は、入ったときに PlayClip で再生される
//   - アニメーションの出来事（"Hit" など）は、1 回だけ読める（ConsumeClipEvent で読むと、消える）
//   - すべての関数はメインスレッドから呼ばれる
//=============================================================================
class IAIAnimationControl
{
public:
	virtual ~IAIAnimationControl() = default;

	// クリップを再生する。bRestart が false で、すでに同じクリップを再生中なら、そのまま続ける。再生できなければ false
	virtual bool PlayClip( const FString& Clip, bool bRestart ) = 0;
	// 今のクリップが最後まで再生し終わったか（繰り返すクリップは、決して true にならない）
	virtual bool IsClipFinished() const = 0;
	// 出来事が起きていれば true を返し、消す（1 回だけ読める）
	virtual bool ConsumeClipEvent( const FString& Event ) = 0;
	// クリップが存在するか（検証・警告用。分からなければ true）
	virtual bool HasClip( const FString& /*Clip*/ ) const { return true; }
};
