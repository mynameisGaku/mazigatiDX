// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/PixelEffect/PixelEffectTypes.h"

class CPixelPainter;

//=============================================================================
// CPixelParticleSystem
//   粒子（FPixelParticle）の集まりを、決まった刻みで動かし、色の段で描く。
//   動かし方は、速度 → 抵抗で減らす → 重力を足す → 位置を進める、の順（同じ刻みなら、毎回同じ動き）。
//   色は、粒子の Heat を寿命に合わせて 0 へ冷ましながら、色の段から選ぶ（熱いほど明るい段）。
//=============================================================================
class CPixelParticleSystem
{
public:
	void Add( const FPixelParticle& Particle );
	void Clear();

	// StepTime 秒進め、寿命の尽きた粒子を外す（0 以下は何もしない）
	void Step( float32 StepTime );

	// Layer の粒子を、Palette（暗い色から明るい色へ）の色で描く
	void Draw( const CPixelPainter& Painter, const TArray<FColor32>& Palette, EPixelLayer Layer ) const;

	FORCEINLINE int32 Num() const
	{
		return Particles.Num();
	}

	FORCEINLINE const TArray<FPixelParticle>& GetParticles() const
	{
		return Particles;
	}

private:
	TArray<FPixelParticle> Particles;
};
