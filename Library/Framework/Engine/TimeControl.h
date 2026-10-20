#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FEngineTimeControl
//   ゲームの時間の進め方（スケール・一時停止・コマ送り）。デバッグ用のコンソール（timescale / pause / step）が使う。
//   エンジンのループが、プラットフォームの経過時間を Apply に通してから、サブシステム・ゲームの Tick に渡す。
//
//   何も設定していないとき（スケール 1・一時停止なし）は、Apply は経過時間をそのまま（ビットも変えずに）返す。
//   描画・ImGui・プロファイラーはプラットフォームの時間で動き続ける（止まるのは、Tick に渡す経過時間だけ）。
//
//   Control.SetScale( 0.5f );   // 半分の速さ
//   Control.Pause();            // 経過時間が 0 になる（Tick は呼ばれ続ける）
//   Control.Step( 3 );          // 一時停止中に、3 コマだけ進める（1 コマ = StepDeltaTime 秒）
//=============================================================================
struct FEngineTimeControl
{
	static constexpr float32 MaxScale = 100.0f;

	FORCEINLINE float32 GetScale() const { return Scale; }
	// 0 以上 MaxScale 以下に収める
	void SetScale( float32 InScale );

	FORCEINLINE bool IsPaused() const { return bPaused; }
	void Pause() { bPaused = true; }
	// 再開する（コマ送りの残りは捨てる）
	void Resume()
	{
		bPaused = false;
		StepsRemaining = 0;
	}
	// 一時停止にして、Frames コマだけ進める（すでに予約があれば足す）
	void Step( int32 Frames = 1 );
	FORCEINLINE int32 GetStepsRemaining() const { return StepsRemaining; }

	// コマ送りの 1 コマの長さ（秒）
	float32 StepDeltaTime = 1.0f / 60.0f;

	// プラットフォームの経過時間 -> Tick に渡す経過時間
	float32 Apply( float32 PlatformDeltaTime );

private:
	float32 Scale = 1.0f;
	bool bPaused = false;
	int32 StepsRemaining = 0;
};
