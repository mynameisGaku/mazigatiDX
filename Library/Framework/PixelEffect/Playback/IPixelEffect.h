// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Canvas/IPixelSurface.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectTypes.h"

//=============================================================================
// IPixelEffect
//   ピクセルアートのエフェクト 1 種の窓口。始める → 時間を進める → 描く、の 1 つの流れだけ。
//   時刻だけで絵が決まるもの（輪が広がる・文字が跳ねる）も、粒子を動かすもの（爆発の火花）も、同じ形で扱う。
//   描くのは IPixelSurface へ（色と光の 2 枚・CPU の画像・検査の偽物のどれでもよい）。描き先の具体的な型は知らない。
//   決まった種と同じ時間の進め方なら、毎回同じ絵になること。
//=============================================================================
class IPixelEffect
{
public:
	virtual ~IPixelEffect() = default;

	virtual const FPixelEffectMeta& GetMeta() const = 0;

	// 初めから再生し直す（時間を 0 に戻し、Placement の種で乱数を決め直す）
	virtual void Start( const FPixelPlacement& Placement ) = 0;

	// 時間を DeltaTime 秒進める（0 以下は何もしない）
	virtual void Update( float32 DeltaTime ) = 0;

	// いまの絵の Layer の層を、Surface へ描く（状態は変えない）
	virtual void Draw( IPixelSurface& Surface, EPixelLayer Layer ) const = 0;

	// 一度きりのエフェクトが、終わったか（繰り返すものは、いつも false）
	virtual bool IsFinished() const = 0;
};
