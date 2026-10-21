// SPDX-License-Identifier: NOASSERTION
#include "ParticleSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Library/Math/MathConstants.h"

namespace
{
	constexpr float32 MinLifetime = 1.0e-3f;
	constexpr int32 MaxWrapsPerAdvance = 8;   // 1 回の Advance で、ループを何周まで進めるか（Duration が極端に短いときの歯止め）

	// 非有限値を安全な既定値へ置き換える。
	float32 Sanitize( float32 Value, float32 Fallback = 0.0f ) { return std::isfinite( Value ) ? Value : Fallback; }

	// ベクトルの三成分が有限か調べる。
	bool IsFiniteVector( const FVector3& Value )
	{
		return std::isfinite( Value.x ) && std::isfinite( Value.y ) && std::isfinite( Value.z );
	}

	// 加算がfloat32の範囲に収まるときだけ結果を返す。
	bool TryAddFinite( const FVector3& A, const FVector3& B, FVector3& Out )
	{
		// 各成分をfloat32へ狭める前の和。
		const double X = static_cast<double>( A.x ) + B.x;
		const double Y = static_cast<double>( A.y ) + B.y;
		const double Z = static_cast<double>( A.z ) + B.z;
		// float32で表せる絶対値の上限。
		const double Limit = std::numeric_limits<float32>::max();
		if ( !std::isfinite( X ) || !std::isfinite( Y ) || !std::isfinite( Z ) || std::fabs( X ) > Limit || std::fabs( Y ) > Limit || std::fabs( Z ) > Limit ) return false;
		Out = FVector3( static_cast<float32>( X ), static_cast<float32>( Y ), static_cast<float32>( Z ) );
		return true;
	}

	// xorshift32。状態は 0 にならない
	struct FRandom
	{
		uint32& State;

		float32 Next()
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			return static_cast<float32>( State >> 8 ) * ( 1.0f / 16777216.0f );   // 0 以上 1 未満
		}
		float32 Range( const FParticleRange& Range )
		{
			const float32 Low = Sanitize( std::min( Range.Min, Range.Max ) ), High = Sanitize( std::max( Range.Min, Range.Max ) );
			return Low + ( High - Low ) * Next();
		}
	};

	uint32 MakeSeed( uint32 Seed, int32 EmitterIndex )
	{
		uint32 State = Seed * 2654435761u + static_cast<uint32>( EmitterIndex + 1 ) * 40503u + 0x9E3779B9u;
		if ( State == 0 ) State = 1;
		FRandom Warm { State };
		for ( int32 i = 0; i < 4; ++i ) Warm.Next();   // 似たシードの最初の値が、近くならないように
		return State;
	}

	// 軸に垂直な、互いに直交する 2 つの単位ベクトル
	void MakeBasis( const FVector3& Axis, FVector3& OutU, FVector3& OutV )
	{
		const FVector3 Helper = std::fabs( Axis.y ) < 0.99f ? FVector3::Up : FVector3::Right;
		OutU = FVector3::Cross( Helper, Axis ).Normalized();
		OutV = FVector3::Cross( Axis, OutU );
	}

	// 向きに使う軸（長さ 0・非有限なら、上）
	FVector3 GetAxis( const FVector3& Direction )
	{
		if ( !std::isfinite( Direction.x ) || !std::isfinite( Direction.y ) || !std::isfinite( Direction.z ) || Direction.LengthSquared() < 1.0e-12f ) return FVector3::Up;
		return Direction.Normalized();
	}

	// 粒子 1 つを、エミッターに足す（最大の数を超えるなら、足さない）。LocalGravity 等は使わない（生まれるだけ）
	void Spawn( FParticleSimulation& Simulation, int32 EmitterIndex )
	{
		const FParticleEmitterDefinition& Def = Simulation.Definition->Emitters[EmitterIndex];
		FParticleEmitterState& State = Simulation.Emitters[EmitterIndex];
		const int32 MaxParticles = std::clamp( Def.MaxParticles, 0, FParticleEffectDefinition::MaxParticlesPerEmitter );
		if ( State.Particles.Num() >= MaxParticles ) return;

		FRandom Random { State.Random };
		const float32 Scale = NParticle::GetEffectScale( Simulation );
		FParticle P;
		P.Lifetime = std::max( Random.Range( Def.Lifetime ), MinLifetime );
		const float32 Speed = std::max( Random.Range( Def.Speed ), 0.0f ) * Scale;
		P.Angle = Random.Range( Def.StartAngle ) * NMath::DEG2RAD;
		P.Spin = Random.Range( Def.Spin ) * NMath::DEG2RAD;
		P.StartSize = std::max( Random.Range( Def.StartSize ), 0.0f ) * Scale;
		P.EndSize = std::max( Random.Range( Def.EndSize ), 0.0f ) * Scale;

		// エフェクトの軸に沿った位置と向き
		const FVector3 Axis = GetAxis( Def.Direction );
		FVector3 U, V;
		MakeBasis( Axis, U, V );
		FVector3 LocalPosition = FVector3::Zero;
		FVector3 LocalDirection = Axis;
		const float32 Radius = std::max( Sanitize( Def.ShapeRadius ), 0.0f );
		switch ( Def.Shape )
		{
		case EParticleShape::Cone:
		{
			const float32 Distance = Radius * std::sqrt( Random.Next() );
			const float32 Theta = Random.Next() * NMath::TWO_PI;
			LocalPosition = ( U * std::cos( Theta ) + V * std::sin( Theta ) ) * Distance;
			const float32 CosLimit = std::cos( std::clamp( Sanitize( Def.ConeAngle ), 0.0f, 180.0f ) * NMath::DEG2RAD );
			const float32 CosPolar = 1.0f - Random.Next() * ( 1.0f - CosLimit );   // 立体角に対して、一様
			const float32 SinPolar = std::sqrt( std::max( 1.0f - CosPolar * CosPolar, 0.0f ) );
			const float32 Phi = Random.Next() * NMath::TWO_PI;
			LocalDirection = Axis * CosPolar + ( U * std::cos( Phi ) + V * std::sin( Phi ) ) * SinPolar;
			break;
		}
		case EParticleShape::Sphere:
		{
			const float32 Z = Random.Next() * 2.0f - 1.0f;
			const float32 Phi = Random.Next() * NMath::TWO_PI;
			const float32 Ring = std::sqrt( std::max( 1.0f - Z * Z, 0.0f ) );
			const FVector3 Unit( Ring * std::cos( Phi ), Z, Ring * std::sin( Phi ) );
			LocalPosition = Unit * ( Radius * std::cbrt( Random.Next() ) );
			LocalDirection = Unit;
			break;
		}
		case EParticleShape::Box:
		{
			const FVector3 Size( std::max( Sanitize( Def.BoxSize.x ), 0.0f ), std::max( Sanitize( Def.BoxSize.y ), 0.0f ), std::max( Sanitize( Def.BoxSize.z ), 0.0f ) );
			LocalPosition = FVector3( ( Random.Next() - 0.5f ) * Size.x, ( Random.Next() - 0.5f ) * Size.y, ( Random.Next() - 0.5f ) * Size.z );
			break;
		}
		}
		LocalPosition = LocalPosition * Scale;
		const FVector3 LocalVelocity = LocalDirection * Speed;

		if ( Def.Space == EParticleSpace::Local )
		{
			P.Position = LocalPosition;
			P.Velocity = LocalVelocity;
		}
		else
		{
			P.Position = Simulation.Transform.Position + Simulation.Transform.Rotation.Rotate( LocalPosition );
			P.Velocity = Simulation.Transform.Rotation.Rotate( LocalVelocity );
		}
		P.Serial = State.NextSerial++;
		State.Particles.Add( P );
	}

	// 粒子を Step 秒進める（年を取らせ、寿命が尽きたものを消し、動かす）
	void UpdateParticles( FParticleSimulation& Simulation, int32 EmitterIndex, float32 Step )
	{
		const FParticleEmitterDefinition& Def = Simulation.Definition->Emitters[EmitterIndex];
		FParticleEmitterState& State = Simulation.Emitters[EmitterIndex];
		if ( State.Particles.IsEmpty() ) return;
		// 定義に保存されたWorld重力を有限値へ整える。
		const FVector3 WorldGravity( Sanitize( Def.Gravity.x ), Sanitize( Def.Gravity.y ), Sanitize( Def.Gravity.z ) );
		// 粒子空間で加える既存の重力。
		FVector3 Gravity = WorldGravity;
		if ( Def.Space == EParticleSpace::Local ) Gravity = Simulation.Transform.Rotation.UnrotateVector( Gravity );   // 重力は、ワールドの向き
		// 既存の抵抗を速度へ掛ける倍率。
		const float32 DragFactor = std::max( 1.0f - std::max( Sanitize( Def.Drag ), 0.0f ) * Step, 0.0f );
		// 抵抗が定義されているか。
		const bool bDrag = Def.Drag > 0.0f;
		// 空配列なら既存の速度加算式をそのまま使う。
		const bool bHasForceFields = !Def.ForceFields.IsEmpty();
		// 保存上限を超えた定義も保持し、実行だけ先頭の上限内にする。
		const int32 NumForceFields = std::min( Def.ForceFields.Num(), FEffectForceFieldDefinition::MaxFields );

		int32 Kept = 0;
		for ( int32 i = 0; i < State.Particles.Num(); ++i )
		{
			FParticle P = State.Particles[i];
			P.Age += Step;
			if ( P.Age >= P.Lifetime ) continue;
			if ( bHasForceFields )
			{
				// Local粒子は描画と同じ移動・回転でWorld位置へ戻す。
				const FVector3 WorldPosition = Def.Space == EParticleSpace::Local ? Simulation.Transform.Position + Simulation.Transform.Rotation.Rotate( P.Position ) : P.Position;
				// 適用中の場をWorld空間で合成する。
				FVector3 WorldFieldAcceleration = FVector3::Zero;
				// 上限内のfieldを保存順に評価する。
				for ( int32 FieldIndex = 0; FieldIndex < NumForceFields; ++FieldIndex )
				{
					// この粒子位置でのfield加速度。
					const FVector3 FieldAcceleration = Def.ForceFields[FieldIndex].EvaluateAcceleration( WorldPosition );
					// overflowしない場合の累積値。
					FVector3 Sum;
					if ( TryAddFinite( WorldFieldAcceleration, FieldAcceleration, Sum ) ) WorldFieldAcceleration = Sum;
				}
				// Local粒子では加速度だけを粒子空間へ戻す。
				FVector3 LocalFieldAcceleration = Def.Space == EParticleSpace::Local ? Simulation.Transform.Rotation.UnrotateVector( WorldFieldAcceleration ) : WorldFieldAcceleration;
				if ( !IsFiniteVector( LocalFieldAcceleration ) ) LocalFieldAcceleration = FVector3::Zero;
				// 重力へ場の加速度を加えた候補。
				FVector3 Acceleration = Gravity;
				if ( !TryAddFinite( Gravity, LocalFieldAcceleration, Acceleration ) ) Acceleration = Gravity;
				// 場を加えた速度候補。
				const FVector3 UpdatedVelocity = P.Velocity + Acceleration * Step;
				// 抵抗後の位置まで有限なときだけfieldの寄与を採用する。
				const FVector3 DraggedUpdatedVelocity = bDrag ? UpdatedVelocity * DragFactor : UpdatedVelocity;
				const FVector3 UpdatedPosition = P.Position + DraggedUpdatedVelocity * Step;
				P.Velocity = IsFiniteVector( UpdatedVelocity ) && IsFiniteVector( UpdatedPosition ) ? UpdatedVelocity : P.Velocity + Gravity * Step;
			}
			else P.Velocity = P.Velocity + Gravity * Step;
			if ( bDrag ) P.Velocity = P.Velocity * DragFactor;
			P.Position = P.Position + P.Velocity * Step;
			P.Angle += P.Spin * Step;
			State.Particles[Kept++] = P;
		}
		State.Particles.SetNum( Kept );
	}

	// 粒子を進めてから、出す（エフェクトの時間を Step 進める）
	void StepOnce( FParticleSimulation& Simulation, float32 Step )
	{
		const FParticleEffectDefinition& Effect = *Simulation.Definition;
		// Start が上限で切った後は、実際に作った状態の数だけ進める。
		for ( int32 i = 0; i < Simulation.Emitters.Num(); ++i )
		{
			UpdateParticles( Simulation, i, Step );
			if ( !Simulation.bEmitting ) continue;

			const FParticleEmitterDefinition& Def = Effect.Emitters[i];
			FParticleEmitterState& State = Simulation.Emitters[i];
			const float32 Delay = std::max( Sanitize( Def.Delay ), 0.0f );
			const float32 LocalPrevious = Simulation.Time - Delay;
			const float32 LocalNow = Simulation.Time + Step - Delay;
			if ( LocalNow < 0.0f ) continue;   // まだ、動き出していない

			// 捨てる粒子の数は整数へ変換せず、残っている空きの数だけ作る。
			// この定義で同時に生存できる粒子数。
			const int32 Capacity = std::clamp( Def.MaxParticles, 0, FParticleEffectDefinition::MaxParticlesPerEmitter );
			// この刻みで新たに埋められる空き数。
			const int32 Available = std::max( Capacity - State.Particles.Num(), 0 );
			const float32 Rate = std::max( Sanitize( Def.Rate ), 0.0f );
			State.Carry += static_cast<float64>( Rate ) * static_cast<float64>( LocalNow - std::max( LocalPrevious, 0.0f ) );
			// 空きに収まらない分も含めた整数個数。巨大な値でも整数型へ変換しない。
			const float64 FromRate = std::floor( State.Carry + 1.0e-9 );
			// 実際に作る数だけを整数型で保持する。
			int32 NumToEmit = static_cast<int32>( std::min( FromRate, static_cast<float64>( Available ) ) );
			State.Carry -= FromRate;
			while ( State.BurstsDone < Def.Bursts.Num() && Def.Bursts[State.BurstsDone].Time <= LocalNow )
			{
				NumToEmit += std::min( std::max( Def.Bursts[State.BurstsDone].Count, 0 ), Available - NumToEmit );
				++State.BurstsDone;
			}
			for ( int32 n = 0; n < NumToEmit; ++n ) Spawn( Simulation, i );
		}
		Simulation.Time += Step;
	}

	float32 Lerp( float32 A, float32 B, float32 T ) { return A + ( B - A ) * T; }

	uint8 ToByte( float32 Value ) { return static_cast<uint8>( std::clamp( static_cast<int32>( std::lround( Value ) ), 0, 255 ) ); }
}

//=============================================================================
// FParticleSimulation
//=============================================================================
int32 FParticleSimulation::GetNumParticles() const
{
	int32 Count = 0;
	for ( const FParticleEmitterState& Emitter : Emitters ) Count += Emitter.Particles.Num();
	return Count;
}

bool FParticleSimulation::IsFinished() const
{
	return bStarted && !bEmitting && GetNumParticles() == 0;
}

//=============================================================================
// NParticle
//=============================================================================
float32 NParticle::GetEffectScale( const FParticleSimulation& Simulation )
{
	const FVector3& S = Simulation.Transform.Scale;
	const float32 Average = ( std::fabs( Sanitize( S.x, 1.0f ) ) + std::fabs( Sanitize( S.y, 1.0f ) ) + std::fabs( Sanitize( S.z, 1.0f ) ) ) / 3.0f;
	return Average * std::max( Sanitize( Simulation.Scale, 1.0f ), 0.0f );
}

void NParticle::Start( FParticleSimulation& Simulation, const TSharedPtr<const FParticleEffectDefinition>& Definition, uint32 Seed, float32 Scale, float32 Speed, int32 LoopOverride )
{
	Simulation.Definition = Definition;
	Simulation.Emitters.Reset();
	Simulation.Time = 0.0f;
	Simulation.Scale = Scale;
	Simulation.Speed = Speed;
	Simulation.bStarted = true;
	Simulation.bEmitting = false;
	if ( !Definition.IsValid() ) return;
	Simulation.bLoop = LoopOverride < 0 ? Definition->bLoop : LoopOverride != 0;
	const uint32 UsedSeed = Seed != 0 ? Seed : Definition->Seed;
	const int32 NumEmitters = std::min( Definition->Emitters.Num(), FParticleEffectDefinition::MaxEmitters );
	for ( int32 i = 0; i < NumEmitters; ++i )
	{
		FParticleEmitterState State;
		State.Random = MakeSeed( UsedSeed, i );
		Simulation.Emitters.Add( State );
	}
	Simulation.bEmitting = NumEmitters > 0;
}

void NParticle::Advance( FParticleSimulation& Simulation, float32 DeltaTime )
{
	if ( !Simulation.bStarted || !Simulation.Definition.IsValid() || !std::isfinite( DeltaTime ) || DeltaTime <= 0.0f ) return;
	const FParticleEffectDefinition& Effect = *Simulation.Definition;
	if ( Simulation.Emitters.Num() > Effect.Emitters.Num() ) return;   // 定義と食い違っている（Start し直すまで進めない）

	float32 Remaining = DeltaTime * std::max( Sanitize( Simulation.Speed, 1.0f ), 0.0f );
	// 入力が有限でも、再生速度との積や現在時刻との和が範囲を超える場合は進めない。
	if ( !std::isfinite( Remaining ) || !std::isfinite( Simulation.Time + Remaining ) ) return;
	const float32 Duration = std::max( Sanitize( Effect.Duration ), 0.0f );
	for ( int32 Wrap = 0; Remaining > 0.0f && Wrap < MaxWrapsPerAdvance; ++Wrap )
	{
		float32 Step = Remaining;
		bool bWrap = false;
		if ( Simulation.bEmitting && Duration > 0.0f )
		{
			const float32 Left = Duration - Simulation.Time;
			if ( Left <= Step )
			{
				Step = std::max( Left, 0.0f );
				bWrap = true;
			}
		}
		StepOnce( Simulation, Step );
		Remaining -= Step;
		if ( !bWrap ) break;
		if ( Simulation.bLoop )
		{
			Simulation.Time = 0.0f;
			for ( FParticleEmitterState& Emitter : Simulation.Emitters )
			{
				Emitter.Carry = 0.0;
				Emitter.BurstsDone = 0;
			}
		}
		else Simulation.bEmitting = false;
	}
}

void NParticle::Stop( FParticleSimulation& Simulation, bool bImmediate )
{
	Simulation.bEmitting = false;
	if ( bImmediate )
	{
		for ( FParticleEmitterState& Emitter : Simulation.Emitters ) Emitter.Particles.Reset();
	}
}

void NParticle::CollectDrawParticles( const FParticleSimulation& Simulation, TArray<FDrawParticle>& Out )
{
	Out.Reset();
	if ( !Simulation.Definition.IsValid() ) return;
	const FParticleEffectDefinition& Effect = *Simulation.Definition;
	for ( int32 i = 0; i < Simulation.Emitters.Num() && i < Effect.Emitters.Num(); ++i )
	{
		const FParticleEmitterDefinition& Def = Effect.Emitters[i];
		const bool bLocal = Def.Space == EParticleSpace::Local;
		for ( const FParticle& P : Simulation.Emitters[i].Particles )
		{
			const float32 T = std::clamp( P.Age / P.Lifetime, 0.0f, 1.0f );
			FDrawParticle Item;
			Item.Position = bLocal ? Simulation.Transform.Position + Simulation.Transform.Rotation.Rotate( P.Position ) : P.Position;
			if ( Def.Appearance.Size.Keys.IsEmpty() ) Item.Size = Lerp( P.StartSize, P.EndSize, T );
			else
			{
				// 有効な大きさ曲線だけを適用し、巨大値や非有限値を描画へ渡さない。
				// 従来の寿命補間による大きさ。
				const float32 BaseSize = Lerp( P.StartSize, P.EndSize, T );
				// 寿命位置の大きさ倍率。
				const float32 SizeScale = Def.Appearance.EvaluateSizeScale( T );
				// 二倍精度での積。
				const double ScaledSize = std::isfinite( BaseSize ) && BaseSize >= 0.0f ? static_cast<double>( BaseSize ) * SizeScale : 0.0;
				Item.Size = static_cast<float32>( std::isfinite( ScaledSize ) ? std::clamp( ScaledSize, 0.0, 1000000.0 ) : 0.0 );
			}
			Item.Angle = P.Angle;
			if ( Def.Appearance.Color.Keys.IsEmpty() ) Item.Color = FColor32( ToByte( Lerp( Def.StartColor.r, Def.EndColor.r, T ) ), ToByte( Lerp( Def.StartColor.g, Def.EndColor.g, T ) ), ToByte( Lerp( Def.StartColor.b, Def.EndColor.b, T ) ), ToByte( Lerp( Def.StartColor.a, Def.EndColor.a, T ) ) );
			else
			{
				// 基準 RGBA を丸める前に各色倍率を掛け、一度だけ byte 化する。
				// 寿命位置の RGBA 倍率。
				const FColor ColorScale = Def.Appearance.EvaluateColorScale( T );
				// 丸め前の赤成分。
				const float32 Red = Lerp( Def.StartColor.r, Def.EndColor.r, T ) * ColorScale.r;
				// 丸め前の緑成分。
				const float32 Green = Lerp( Def.StartColor.g, Def.EndColor.g, T ) * ColorScale.g;
				// 丸め前の青成分。
				const float32 Blue = Lerp( Def.StartColor.b, Def.EndColor.b, T ) * ColorScale.b;
				// 丸め前のアルファ成分。
				const float32 Alpha = Lerp( Def.StartColor.a, Def.EndColor.a, T ) * ColorScale.a;
				Item.Color = FColor32( ToByte( std::isfinite( Red ) ? Red : 0.0f ), ToByte( std::isfinite( Green ) ? Green : 0.0f ), ToByte( std::isfinite( Blue ) ? Blue : 0.0f ), ToByte( std::isfinite( Alpha ) ? Alpha : 0.0f ) );
			}
			Item.Emitter = i;
			Item.Serial = P.Serial;
			Item.Age = P.Age;
			Item.NormalizedAge = T;
			Out.Add( Item );
		}
	}
}
