// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/IPixelEffect.h"
#include "Library/Framework/PixelEffect/Canvas/PixelPainter.h"
#include "Library/Framework/PixelEffect/Math/PixelRandom.h"

//=============================================================================
// CPixelTimedEffect
//   IPixelEffect の共通の流れ（時間の数え方・繰り返し・終わり・置き場所・色の段）を受け持つ土台。
//   それぞれのエフェクトは、始めたとき（OnStart）・決まった刻みごと（OnStep）・描くとき（OnDraw）だけを書く。
//   時間は 1/120 秒の決まった刻みでだけ進む（呼ぶ側のフレームの長さが変わっても、同じ絵になる）。
//=============================================================================
class CPixelTimedEffect : public IPixelEffect
{
public:
	// 時間を進める刻み（秒）
	static constexpr float32 StepTime = 1.0f / 120.0f;

	const FPixelEffectMeta& GetMeta() const override;
	void Start( const FPixelPlacement& Placement ) override;
	void Update( float32 DeltaTime ) override;
	void Draw( IPixelSurface& Surface, EPixelLayer Layer ) const override;
	bool IsFinished() const override;

protected:
	explicit CPixelTimedEffect( const FPixelEffectMeta& InMeta );

	// Start で、時間を 0 に戻したあとに呼ぶ（粒子を撒くなど）。Random は、置き場所のシードから作った乱数
	virtual void OnStart( FPixelRandom& Random );

	// 刻み（StepTime 秒）ごとに呼ぶ
	virtual void OnStep( float32 InStepTime );

	// Layer の層を描く。Painter の原点はエフェクトの基準点で、左右反転も済んでいる
	virtual void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const = 0;

	// 始めてからの時間（秒。繰り返すものは、1 周の中の時間）
	float32 GetTime() const;

	// GetTime を、長さで割った進み具合（0〜1）
	float32 GetProgress() const;

	// 始めてから数えた刻みの数（繰り返しても戻らない。ちらつきの切り替えなどに）
	FORCEINLINE int32 GetStepCount() const
	{
		return StepCount;
	}

	FORCEINLINE const FPixelPlacement& GetPlacement() const
	{
		return Placement;
	}

	// 使う色の段（置き場所の指定がなければ、既定のパレット）
	FORCEINLINE const TArray<FColor32>& GetPalette() const
	{
		return Palette;
	}

private:
	FPixelEffectMeta Meta;
	FPixelPlacement Placement;
	TArray<FColor32> Palette;
	int32 StepCount = 0;
	// まだ刻みにならない、余りの時間
	float32 PendingTime = 0.0f;
};
