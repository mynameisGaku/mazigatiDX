// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Brush/UIBrush.h"

//=============================================================================
// 状態ごとのブラシの切り替えを、時間をかけて混ぜる（フェード）
//   部品（ボタン・チェックボックス・スライダー・入力欄）が、スタイルの TransitionDuration（秒）が 0 より大きいときに使う。
//   時間は、CUIWidget::AdvanceTime（CUIContext::Update が、毎フレーム呼ぶ）で進む。
//=============================================================================

// 状態（番号）の切り替え。前の状態のブラシの上に、いまの状態のブラシを、だんだん濃く重ねる
struct FUIStateFade
{
	// 時間を進める。Target は、いまの状態の番号（0 以上。意味は、部品が決める）。Duration が 0 以下か、最初の 1 回は、すぐ切り替わる。
	// 切り替えの途中で、前の状態へ戻るときは、進んだ分だけ戻る（ホバーの出入りを繰り返しても、飛ばない）
	void Advance( int32 Target, float32 DeltaTime, float32 Duration );

	// 1 回でも Advance したか（していなければ、部品は、いまの状態のブラシを、そのまま描く）
	bool IsStarted() const { return Current >= 0; }
	// 切り替えの途中か
	bool IsFading() const { return Previous >= 0 && Progress < 1.0f; }

	int32 Current = -1;       // いまの状態
	int32 Previous = -1;      // 切り替える前の状態（途中でなければ -1）
	float32 Progress = 1.0f;  // 進み具合（0〜1）
};

// 重ねるブラシ（フォーカスの枠・ホバーの重ねなど）の出入り
struct FUIOverlayFade
{
	// 時間を進める。bShown なら 1 へ、違えば 0 へ、Duration 秒かけて近づく。Duration が 0 以下か、最初の 1 回は、すぐその値になる
	void Advance( bool bShown, float32 DeltaTime, float32 Duration );

	bool bStarted = false;
	float32 Amount = 0.0f;    // 濃さ（0〜1）
};

namespace NUIBrush
{
	// 切り替えの途中の 2 つのブラシを描く: 前のブラシの上に、いまのブラシを、Progress（0〜1）の濃さで重ねる。
	// いまのブラシが、何も描かないものなら、前のブラシを、だんだん薄くする
	void DrawFade( FDrawContext& CommandList, const FUIBrush& Previous, const FUIBrush& Current, float32 Progress, const FUIRect& Rect, float32 Opacity = 1.0f );
}
