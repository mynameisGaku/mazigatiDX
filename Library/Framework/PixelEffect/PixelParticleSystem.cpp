// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelParticleSystem.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelPainter.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 火花の尾の長さ（画素）。速さ 1 画素 / 秒あたり
	constexpr float32 StreakLengthPerSpeed = 0.045f;
	constexpr float32 MaxStreakLength = 6.0f;
}

void CPixelParticleSystem::Add( const FPixelParticle& Particle )
{
	if ( Particle.Life > 0.0f )
	{
		Particles.Add( Particle );
	}
}

void CPixelParticleSystem::Clear()
{
	Particles.Reset();
}

void CPixelParticleSystem::Step( float32 StepTime )
{
	if ( !( StepTime > 0.0f ) )
	{
		return;
	}
	int32 Kept = 0;
	for ( int32 Index = 0; Index < Particles.Num(); ++Index )
	{
		FPixelParticle Particle = Particles[Index];
		Particle.Age += StepTime;
		if ( Particle.Age >= Particle.Life )
		{
			continue;
		}
		const float32 Keep = NMath::Max( 0.0f, 1.0f - Particle.Drag * StepTime );
		Particle.Velocity = FVector2( Particle.Velocity.x * Keep, Particle.Velocity.y * Keep + Particle.Gravity * StepTime );
		Particle.Position = Particle.Position + FVector2( Particle.Velocity.x * StepTime, Particle.Velocity.y * StepTime );
		Particles[Kept] = Particle;
		++Kept;
	}
	Particles.SetNum( Kept );
}

void CPixelParticleSystem::RemoveIf( const TCallable<bool( const FPixelParticle& )>& ShouldRemove )
{
	int32 Kept = 0;
	for ( int32 Index = 0; Index < Particles.Num(); ++Index )
	{
		if ( ShouldRemove( Particles[Index] ) )
		{
			continue;
		}
		Particles[Kept] = Particles[Index];
		++Kept;
	}
	Particles.SetNum( Kept );
}

void CPixelParticleSystem::Draw( const CPixelPainter& Painter, const TArray<FColor32>& Palette, EPixelLayer Layer ) const
{
	for ( const FPixelParticle& Particle : Particles )
	{
		if ( Particle.Layer != Layer )
		{
			continue;
		}
		const float32 Remaining = 1.0f - NMath::Clamp( Particle.Age / Particle.Life, 0.0f, 1.0f );
		const float32 Size = Particle.Size * ( 1.0f + Particle.Growth * ( 1.0f - Remaining ) );
		const FColor32 Color = NPixelColor::SampleStep( Palette, Particle.Heat * Remaining );
		FPixelStroke Stroke;
		// 寿命の最後の 4 割で、ディザで薄れる
		Stroke.Alpha = Particle.bFade ? NMath::Min( 1.0f, Remaining / 0.4f ) : 1.0f;
		Stroke.Glow = Particle.Glow * Remaining;
		switch ( Particle.Shape )
		{
		case EPixelParticleShape::Dot:
			Painter.Dot( Particle.Position, Color, Stroke );
			break;
		case EPixelParticleShape::Disc:
			Painter.Disc( Particle.Position, NMath::Max( 0.5f, Size ), Color, Stroke );
			break;
		case EPixelParticleShape::Streak:
		{
			const float32 Speed = NMath::Sqrt( Particle.Velocity.x * Particle.Velocity.x + Particle.Velocity.y * Particle.Velocity.y );
			const float32 Length = NMath::Min( MaxStreakLength, Speed * StreakLengthPerSpeed );
			const FVector2 Tail = Speed > 0.0f
				? Particle.Position - FVector2( Particle.Velocity.x / Speed * Length, Particle.Velocity.y / Speed * Length )
				: Particle.Position;
			Painter.Line( Tail, Particle.Position, Color, Stroke );
			break;
		}
		case EPixelParticleShape::Cross:
		{
			const float32 Arm = NMath::Max( 1.0f, Size );
			Painter.Line( Particle.Position - FVector2( Arm, 0.0f ), Particle.Position + FVector2( Arm, 0.0f ), Color, Stroke );
			Painter.Line( Particle.Position - FVector2( 0.0f, Arm ), Particle.Position + FVector2( 0.0f, Arm ), Color, Stroke );
			break;
		}
		}
	}
}
