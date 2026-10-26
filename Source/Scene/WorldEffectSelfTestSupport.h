#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Effect/ParticleSimulation.h"
#include "WorldMeshSelfTestSupport.h"
#include "WorldSelfTestSupport.h"

//=============================================================================
// エフェクト（パーティクル）の自己確認（WorldEffectSelfTest*.cpp）が共有する道具
//=============================================================================
namespace NEffectTest
{
	inline bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f )
	{
		return NMath::Abs( A.x - B.x ) <= Tolerance && NMath::Abs( A.y - B.y ) <= Tolerance && NMath::Abs( A.z - B.z ) <= Tolerance;
	}

	// 何も起きないエミッター（出さない・動かない・長生き・大きさ 1）。確認したいところだけ変えて使う
	inline FParticleEmitterDefinition QuietEmitter()
	{
		FParticleEmitterDefinition E;
		E.Rate = 0.0f;
		E.Lifetime = { 100.0f, 100.0f };
		E.Speed = { 0.0f, 0.0f };
		E.Gravity = FVector3::Zero;
		E.ConeAngle = 0.0f;
		E.ShapeRadius = 0.0f;
		E.StartSize = { 1.0f, 1.0f };
		E.EndSize = { 1.0f, 1.0f };
		E.MaxParticles = 1000;
		return E;
	}

	inline FParticleBurst Burst( float32 Time, int32 Count )
	{
		FParticleBurst B;
		B.Time = Time;
		B.Count = Count;
		return B;
	}

	inline TSharedPtr<FParticleEffectDefinition> MakeEffect( std::initializer_list<FParticleEmitterDefinition> Emitters, float32 Duration = 0.0f, bool bLoop = true, uint32 Seed = 1 )
	{
		TSharedPtr<FParticleEffectDefinition> Effect = MakeShared<FParticleEffectDefinition>();
		Effect->Duration = Duration;
		Effect->bLoop = bLoop;
		Effect->Seed = Seed;
		for ( const FParticleEmitterDefinition& E : Emitters ) Effect->Emitters.Add( E );
		return Effect;
	}

	// 全部の項目を、既定と違う値にしたエミッター
	inline FParticleEmitterDefinition FullEmitter()
	{
		FParticleEmitterDefinition E;
		E.Name = "Sparks";
		E.Texture = "Textures/Spark.png";
		E.Blend = EParticleBlend::Alpha;
		E.Space = EParticleSpace::Local;
		E.Orient = EParticleOrient::Fixed;
		E.MaxParticles = 77;
		E.Delay = 0.25f;
		E.Rate = 12.5f;
		E.Bursts.Add( Burst( 0.5f, 7 ) );
		E.Bursts.Add( Burst( 1.5f, 3 ) );
		E.Lifetime = { 0.75f, 1.5f };
		E.Speed = { 2.0f, 4.0f };
		E.Shape = EParticleShape::Box;
		E.ShapeRadius = 0.5f;
		E.ConeAngle = 33.0f;
		E.BoxSize = FVector3( 1.0f, 2.0f, 3.0f );
		E.Direction = FVector3( 0.0f, 0.0f, 1.0f );
		E.Gravity = FVector3( 0.5f, -9.0f, 0.25f );
		E.Drag = 0.5f;
		E.StartSize = { 0.5f, 0.75f };
		E.EndSize = { 0.125f, 0.25f };
		E.StartColor = FColor32( 255, 128, 64, 200 );
		E.EndColor = FColor32( 10, 20, 30, 0 );
		E.StartAngle = { 0.0f, 360.0f };
		E.Spin = { -90.0f, 90.0f };
		return E;
	}

	// 同じ刻みで何回か進める
	inline void Run( FParticleSimulation& Sim, float32 Step, int32 Times )
	{
		for ( int32 i = 0; i < Times; ++i ) NParticle::Advance( Sim, Step );
	}

	// i 番目に描く粒子の、ワールドでの位置
	inline FVector3 DrawPosition( const FParticleSimulation& Sim, int32 Index = 0 )
	{
		TArray<NParticle::FDrawParticle> Particles;
		NParticle::CollectDrawParticles( Sim, Particles );
		return Particles.IsValidIndex( Index ) ? Particles[Index].Position : FVector3( 9999.0f, 9999.0f, 9999.0f );
	}

	// 粒子の位置・速度・寿命などが、全部まったく同じか
	inline bool SameParticles( const FParticleSimulation& A, const FParticleSimulation& B )
	{
		if ( A.Emitters.Num() != B.Emitters.Num() ) return false;
		for ( int32 e = 0; e < A.Emitters.Num(); ++e )
		{
			const TArray<FParticle>& PA = A.Emitters[e].Particles;
			const TArray<FParticle>& PB = B.Emitters[e].Particles;
			if ( PA.Num() != PB.Num() ) return false;
			for ( int32 i = 0; i < PA.Num(); ++i )
			{
				if ( PA[i].Position.x != PB[i].Position.x || PA[i].Position.y != PB[i].Position.y || PA[i].Position.z != PB[i].Position.z || PA[i].Velocity.x != PB[i].Velocity.x || PA[i].Velocity.y != PB[i].Velocity.y || PA[i].Velocity.z != PB[i].Velocity.z
					 || PA[i].Lifetime != PB[i].Lifetime || PA[i].StartSize != PB[i].StartSize || PA[i].EndSize != PB[i].EndSize || PA[i].Angle != PB[i].Angle || PA[i].Spin != PB[i].Spin ) return false;
			}
		}
		return true;
	}

	inline int32 CountIssues( const TArray<FParticleIssue>& Issues, bool bError, int32 Emitter = -2 )
	{
		int32 Count = 0;
		for ( const FParticleIssue& Issue : Issues )
		{
			if ( Issue.bError == bError && ( Emitter == -2 || Issue.Emitter == Emitter ) ) ++Count;
		}
		return Count;
	}

	inline bool HasIssueText( const TArray<FParticleIssue>& Issues, const char* Text )
	{
		for ( const FParticleIssue& Issue : Issues )
		{
			if ( Issue.Message.Contains( Text ) ) return true;
		}
		return false;
	}
}
