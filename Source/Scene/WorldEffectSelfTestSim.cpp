#include "WorldEffectSelfTestSupport.h"

using namespace NAssetTest;
using namespace NEffectTest;

//=============================================================================
// 粒子のシミュレーション・バックエンドの一覧・組み込みの丸の画像の確認（DxLib もシーンも使わない）
//   動きの式は、紙の上で出した値と突き合わせる（刻みが 0.125 秒 = 2 のべき乗なので、誤差が出ない）。
//=============================================================================
namespace
{
	// 刻み Dt で K 回進めたあとの、自由落下の高さ: y0 + g * Dt^2 * K * (K + 1) / 2（生まれた Advance の次から数えて K 回）
	float64 FallHeight( float64 Y0, float64 Gravity, float64 Dt, int32 K )
	{
		return Y0 + Gravity * Dt * Dt * static_cast<float64>( K ) * static_cast<float64>( K + 1 ) / 2.0;
	}

	//=========================================================================
	// 動き
	//=========================================================================
	void TestMotion( FChecker& C )
	{
		C.Group = "effect-sim-motion";
		FParticleEmitterDefinition E = QuietEmitter();
		E.Bursts.Add( Burst( 0.0f, 1 ) );
		E.Gravity = FVector3( 0.0f, -10.0f, 0.0f );
		const TSharedPtr<FParticleEffectDefinition> Def = MakeEffect( { E } );

		FParticleSimulation Sim;
		Sim.Transform = FTransform( FVector3( 0.0f, 5.0f, 0.0f ) );
		NParticle::Start( Sim, Def );
		C.Check( Sim.bStarted && Sim.bEmitting && Sim.GetNumParticles() == 0 && !Sim.IsFinished() && Sim.Emitters.Num() == 1, "after Start: emitting, nothing yet, not finished" );
		NParticle::Advance( Sim, 0.125f );
		C.Check( Sim.GetNumParticles() == 1 && Near3( DrawPosition( Sim ), FVector3( 0.0f, 5.0f, 0.0f ) ) && Sim.Emitters[0].Particles[0].Age == 0.0f && Sim.Emitters[0].Particles[0].Velocity.y == 0.0f,
				 "a burst at time 0 is born at the end of the first step: at the effect's position, age 0, at rest" );
		C.Check( Sim.Time == 0.125f, "the effect's time is the sum of the steps" );

		bool bAllOk = true;
		float64 WorstError = 0.0;
		for ( int32 K = 1; K <= 8; ++K )
		{
			NParticle::Advance( Sim, 0.125f );
			const float64 Expected = FallHeight( 5.0, -10.0, 0.125, K );
			const float64 Error = std::fabs( DrawPosition( Sim ).y - Expected );
			WorstError = std::max( WorstError, Error );
			const float64 ExpectedVelocity = -10.0 * 0.125 * K;
			bAllOk = bAllOk && Error < 1.0e-4 && std::fabs( Sim.Emitters[0].Particles[0].Velocity.y - ExpectedVelocity ) < 1.0e-4 && std::fabs( Sim.Emitters[0].Particles[0].Age - 0.125 * K ) < 1.0e-6;
		}
		C.Check( bAllOk, "free fall follows the semi-implicit Euler formula y = y0 + g dt^2 k (k + 1) / 2 for 8 steps (worst error %.6f)", WorstError );
		C.Check( DrawPosition( Sim ).x == 0.0f && DrawPosition( Sim ).z == 0.0f, "and nothing moves sideways" );

		// 空気の抵抗: v = v * (1 - drag dt)（重力のあと）
		FParticleEmitterDefinition D = QuietEmitter();
		D.Bursts.Add( Burst( 0.0f, 1 ) );
		D.Speed = { 10.0f, 10.0f };
		D.Drag = 1.0f;
		FParticleSimulation DragSim;
		NParticle::Start( DragSim, MakeEffect( { D } ) );
		NParticle::Advance( DragSim, 0.125f );
		float64 V = 10.0, Y = 0.0;
		bool bDragOk = DragSim.Emitters[0].Particles[0].Velocity.y == 10.0f;
		for ( int32 K = 1; K <= 6; ++K )
		{
			NParticle::Advance( DragSim, 0.125f );
			V *= ( 1.0 - 1.0 * 0.125 );
			Y += V * 0.125;
			bDragOk = bDragOk && std::fabs( DrawPosition( DragSim ).y - Y ) < 1.0e-4 && std::fabs( DragSim.Emitters[0].Particles[0].Velocity.y - V ) < 1.0e-4;
		}
		C.Check( bDragOk, "drag shrinks the speed by (1 - drag dt) each step (the speed after 6 steps %.4f)", V );

		// 抵抗が大きすぎても、向きは逆にならない（1 - drag dt が負にならない）
		FParticleEmitterDefinition Heavy = QuietEmitter();
		Heavy.Bursts.Add( Burst( 0.0f, 1 ) );
		Heavy.Speed = { 10.0f, 10.0f };
		Heavy.Drag = 100.0f;
		FParticleSimulation HeavySim;
		NParticle::Start( HeavySim, MakeEffect( { Heavy } ) );
		Run( HeavySim, 0.125f, 3 );
		C.Check( HeavySim.GetNumParticles() == 1 && HeavySim.Emitters[0].Particles[0].Velocity.y == 0.0f && DrawPosition( HeavySim ).y == 0.0f, "a drag so large that 1 - drag dt is negative stops the particle (it does not turn it around)" );

		// 回転
		FParticleEmitterDefinition S = QuietEmitter();
		S.Bursts.Add( Burst( 0.0f, 1 ) );
		S.StartAngle = { 90.0f, 90.0f };
		S.Spin = { 180.0f, 180.0f };
		FParticleSimulation SpinSim;
		NParticle::Start( SpinSim, MakeEffect( { S } ) );
		NParticle::Advance( SpinSim, 0.5f );
		const float32 StartAngle = SpinSim.Emitters[0].Particles[0].Angle;
		NParticle::Advance( SpinSim, 0.5f );
		C.Check( std::fabs( StartAngle - 1.5707963f ) < 1.0e-5f && std::fabs( SpinSim.Emitters[0].Particles[0].Angle - 3.1415927f ) < 1.0e-5f, "the start angle is in degrees and the spin in degrees per second (90 degrees, then +180 degrees in 1 s)" );
	}

	//=========================================================================
	// 出す数と時刻
	//=========================================================================
	void TestEmission( FChecker& C )
	{
		C.Group = "effect-sim-emit";
		// 1 秒に 10 個: 刻みに依らず、1 秒で 10 個
		FParticleEmitterDefinition R = QuietEmitter();
		R.Rate = 10.0f;
		int32 Counts[3] = {};
		const float32 Steps[3] = { 0.25f, 0.0625f, 1.0f };
		const int32 Times[3] = { 4, 16, 1 };
		for ( int32 i = 0; i < 3; ++i )
		{
			FParticleSimulation Sim;
			NParticle::Start( Sim, MakeEffect( { R } ) );
			Run( Sim, Steps[i], Times[i] );
			Counts[i] = Sim.GetNumParticles();
		}
		C.Check( Counts[0] == 10 && Counts[1] == 10 && Counts[2] == 10, "10 per second makes 10 in a second, however it is stepped (4 x 0.25 s: %d, 16 x 0.0625 s: %d, 1 x 1 s: %d)", Counts[0], Counts[1], Counts[2] );

		// 端数を覚えている: 1 秒に 2.5 個 x 0.25 秒の刻み = 0.625 個ずつ -> 0, 1, 1, 0, ...
		FParticleEmitterDefinition F = QuietEmitter();
		F.Rate = 2.5f;
		FParticleSimulation Fraction;
		NParticle::Start( Fraction, MakeEffect( { F } ) );
		int32 Seen[4] = {};
		for ( int32 i = 0; i < 4; ++i )
		{
			NParticle::Advance( Fraction, 0.25f );
			Seen[i] = Fraction.GetNumParticles();
		}
		C.Check( Seen[0] == 0 && Seen[1] == 1 && Seen[2] == 1 && Seen[3] == 2, "the fraction is carried over: 0.625 per step gives 0, 1, 1, 2 in total (%d, %d, %d, %d)", Seen[0], Seen[1], Seen[2], Seen[3] );

		// 決まった時刻にまとめて
		FParticleEmitterDefinition B = QuietEmitter();
		B.Bursts.Add( Burst( 0.5f, 7 ) );
		B.Bursts.Add( Burst( 0.5f, 2 ) );
		B.Bursts.Add( Burst( 1.0f, 4 ) );
		FParticleSimulation BurstSim;
		NParticle::Start( BurstSim, MakeEffect( { B } ) );
		int32 BurstCounts[5] = {};
		for ( int32 i = 0; i < 5; ++i )
		{
			NParticle::Advance( BurstSim, 0.25f );
			BurstCounts[i] = BurstSim.GetNumParticles();
		}
		C.Check( BurstCounts[0] == 0 && BurstCounts[1] == 9 && BurstCounts[2] == 9 && BurstCounts[3] == 13 && BurstCounts[4] == 13,
				 "a burst fires in the first step that reaches its time (0.5 s: 7 + 2 together, 1.0 s: 4) (%d, %d, %d, %d, %d)", BurstCounts[0], BurstCounts[1], BurstCounts[2], BurstCounts[3], BurstCounts[4] );

		// 動き出すまでの待ち
		FParticleEmitterDefinition D = QuietEmitter();
		D.Rate = 8.0f;
		D.Delay = 1.0f;
		FParticleSimulation Delayed;
		NParticle::Start( Delayed, MakeEffect( { D } ) );
		Run( Delayed, 0.25f, 4 );
		const int32 BeforeDelay = Delayed.GetNumParticles();
		Run( Delayed, 0.25f, 4 );
		C.Check( BeforeDelay == 0 && Delayed.GetNumParticles() == 8, "an emitter with a delay of 1 s emits nothing for 1 s, then 8 per second (%d, then %d after 2 s)", BeforeDelay, Delayed.GetNumParticles() );
		FParticleEmitterDefinition DB = QuietEmitter();
		DB.Delay = 1.0f;
		DB.Bursts.Add( Burst( 0.5f, 3 ) );
		FParticleSimulation DelayedBurst;
		NParticle::Start( DelayedBurst, MakeEffect( { DB } ) );
		Run( DelayedBurst, 0.25f, 5 );
		const int32 BurstBefore = DelayedBurst.GetNumParticles();
		Run( DelayedBurst, 0.25f, 1 );
		C.Check( BurstBefore == 0 && DelayedBurst.GetNumParticles() == 3, "a burst time counts from the end of the delay (nothing at 1.25 s, 3 at 1.5 s)" );

		// 数の上限
		FParticleEmitterDefinition M = QuietEmitter();
		M.Rate = 1000.0f;
		M.MaxParticles = 50;
		M.Bursts.Add( Burst( 0.0f, 100 ) );
		FParticleSimulation Capped;
		NParticle::Start( Capped, MakeEffect( { M } ) );
		bool bNeverOver = true;
		for ( int32 i = 0; i < 10; ++i )
		{
			NParticle::Advance( Capped, 0.1f );
			bNeverOver = bNeverOver && Capped.GetNumParticles() <= 50;
		}
		C.Check( bNeverOver && Capped.GetNumParticles() == 50, "the maximum is never exceeded and is reached (%d)", Capped.GetNumParticles() );
		FParticleEmitterDefinition Over = QuietEmitter();
		Over.MaxParticles = 1000000;
		Over.Bursts.Add( Burst( 0.0f, 100000 ) );
		FParticleSimulation Huge;
		NParticle::Start( Huge, MakeEffect( { Over } ) );
		NParticle::Advance( Huge, 0.1f );
		C.Check( Huge.GetNumParticles() == FParticleEffectDefinition::MaxParticlesPerEmitter, "a maximum above the hard limit is cut to it (%d)", Huge.GetNumParticles() );

		// 全部のエミッターは別々に数える
		FParticleEmitterDefinition One = QuietEmitter();
		One.Rate = 4.0f;
		FParticleEmitterDefinition Two = QuietEmitter();
		Two.Rate = 12.0f;
		FParticleSimulation Both;
		NParticle::Start( Both, MakeEffect( { One, Two } ) );
		Run( Both, 0.25f, 4 );
		C.Check( Both.Emitters.Num() == 2 && Both.Emitters[0].Particles.Num() == 4 && Both.Emitters[1].Particles.Num() == 12, "each emitter counts for itself (4 and 12 in a second)" );
	}

	//=========================================================================
	// 寿命・繰り返し・止める
	//=========================================================================
	void TestLifeAndLoop( FChecker& C )
	{
		C.Group = "effect-sim-life";
		FParticleEmitterDefinition L = QuietEmitter();
		L.Bursts.Add( Burst( 0.0f, 1 ) );
		L.Lifetime = { 0.5f, 0.5f };
		FParticleSimulation Sim;
		NParticle::Start( Sim, MakeEffect( { L } ) );
		int32 Alive[6] = {};
		for ( int32 i = 0; i < 6; ++i )
		{
			NParticle::Advance( Sim, 0.125f );
			Alive[i] = Sim.GetNumParticles();
		}
		C.Check( Alive[0] == 1 && Alive[1] == 1 && Alive[2] == 1 && Alive[3] == 1 && Alive[4] == 0 && Alive[5] == 0, "a particle born in step 1 with a life of 0.5 s lives through step 4 and is gone in step 5 (age >= lifetime) (%d %d %d %d %d %d)", Alive[0], Alive[1], Alive[2], Alive[3], Alive[4], Alive[5] );

		// 繰り返し: 時間は Duration の中に巻き戻り、爆発がもう一度出る
		FParticleEmitterDefinition P = QuietEmitter();
		P.Bursts.Add( Burst( 0.0f, 1 ) );
		FParticleSimulation Loop;
		NParticle::Start( Loop, MakeEffect( { P }, 1.0f, true ) );
		int32 LoopCounts[3] = {};
		float32 TimeAfterWrap = -1.0f;
		for ( int32 i = 1; i <= 9; ++i )
		{
			NParticle::Advance( Loop, 0.25f );
			if ( i == 4 ) TimeAfterWrap = Loop.Time;
			if ( i == 4 ) LoopCounts[0] = Loop.GetNumParticles();
			if ( i == 8 ) LoopCounts[1] = Loop.GetNumParticles();
			if ( i == 9 ) LoopCounts[2] = Loop.GetNumParticles();
		}
		C.Check( LoopCounts[0] == 1 && LoopCounts[1] == 2 && LoopCounts[2] == 3 && TimeAfterWrap == 0.0f && Loop.bEmitting && !Loop.IsFinished(), "with Duration 1 and Loop, time wraps to 0 at the end and the burst fires again (1, 2, 3 after 4, 8, 9 steps; time after the wrap %.2f)", TimeAfterWrap );

		// 1 回の Advance が、何周も進む
		FParticleSimulation Long;
		NParticle::Start( Long, MakeEffect( { P }, 1.0f, true ) );
		NParticle::Advance( Long, 2.5f );
		C.Check( Long.GetNumParticles() == 3 && Long.Time == 0.5f, "one long step of 2.5 s over a Duration of 1 s goes round twice and a half: 3 bursts, time 0.5 (%d, %.2f)", Long.GetNumParticles(), Long.Time );
		// Duration が極端に短くても、止まらない（1 回の Advance で進む周回の数に上限がある）
		FParticleSimulation Tiny;
		NParticle::Start( Tiny, MakeEffect( { P }, 0.000001f, true ) );
		NParticle::Advance( Tiny, 1.0f );
		C.Check( Tiny.GetNumParticles() == 8, "a tiny Duration does not hang the step: it goes round at most 8 times in one step, one burst each (%d bursts)", Tiny.GetNumParticles() );

		// 繰り返さない: Duration のあと出さなくなり、粒子が消えたら終わり
		FParticleEmitterDefinition Q = QuietEmitter();
		Q.Bursts.Add( Burst( 0.0f, 1 ) );
		Q.Lifetime = { 0.25f, 0.25f };
		FParticleSimulation Once;
		NParticle::Start( Once, MakeEffect( { Q }, 1.0f, false ) );
		Run( Once, 0.25f, 3 );
		const bool bEmittingEarly = Once.bEmitting;
		const bool bFinishedEarly = Once.IsFinished();
		NParticle::Advance( Once, 0.25f );
		C.Check( bEmittingEarly && !bFinishedEarly && !Once.bEmitting && Once.GetNumParticles() == 0 && Once.IsFinished(), "without Loop, emitting stops at the end of Duration and the effect is finished when the particles are gone" );
		FParticleSimulation Waiting;
		FParticleEmitterDefinition Slow = QuietEmitter();
		Slow.Bursts.Add( Burst( 0.0f, 1 ) );
		Slow.Lifetime = { 2.0f, 2.0f };
		NParticle::Start( Waiting, MakeEffect( { Slow }, 0.5f, false ) );
		Run( Waiting, 0.25f, 4 );
		C.Check( !Waiting.bEmitting && Waiting.GetNumParticles() == 1 && !Waiting.IsFinished(), "after the end it is not finished while a particle is still alive" );
		Run( Waiting, 0.25f, 8 );
		C.Check( Waiting.GetNumParticles() == 0 && Waiting.IsFinished(), "and it is finished when that one dies" );

		// 繰り返しの指定を、再生する側が変えられる
		FParticleSimulation ForcedOff, ForcedOn;
		NParticle::Start( ForcedOff, MakeEffect( { P }, 1.0f, true ), 0, 1.0f, 1.0f, 0 );
		NParticle::Start( ForcedOn, MakeEffect( { P }, 1.0f, false ), 0, 1.0f, 1.0f, 1 );
		Run( ForcedOff, 0.25f, 8 );
		Run( ForcedOn, 0.25f, 8 );
		C.Check( !ForcedOff.bLoop && ForcedOff.GetNumParticles() == 1 && !ForcedOff.bEmitting && ForcedOn.bLoop && ForcedOn.GetNumParticles() == 2 && ForcedOn.bEmitting, "the player can force Loop off or on over the effect's own setting" );

		// 止める
		FParticleEmitterDefinition S = QuietEmitter();
		S.Rate = 8.0f;
		S.Lifetime = { 0.5f, 0.5f };
		FParticleSimulation Stopped;
		NParticle::Start( Stopped, MakeEffect( { S } ) );
		Run( Stopped, 0.125f, 8 );
		const int32 Before = Stopped.GetNumParticles();
		NParticle::Stop( Stopped, false );
		const bool bStillThere = Stopped.GetNumParticles() == Before;
		NParticle::Advance( Stopped, 0.125f );
		const int32 Fewer = Stopped.GetNumParticles();
		Run( Stopped, 0.125f, 4 );
		C.Check( Before > 0 && bStillThere && Fewer < Before && Stopped.GetNumParticles() == 0 && Stopped.IsFinished(), "Stop keeps the particles that are alive, makes no new ones, and finishes when they are gone (%d -> %d -> %d)", Before, Fewer, Stopped.GetNumParticles() );
		FParticleSimulation Cut;
		NParticle::Start( Cut, MakeEffect( { S } ) );
		Run( Cut, 0.125f, 8 );
		NParticle::Stop( Cut, true );
		C.Check( Cut.GetNumParticles() == 0 && Cut.IsFinished(), "Stop immediately removes the particles too" );

		// 速さの倍率
		FParticleEmitterDefinition T = QuietEmitter();
		T.Rate = 10.0f;
		FParticleSimulation Normal, Double, Frozen;
		NParticle::Start( Normal, MakeEffect( { T } ), 0, 1.0f, 1.0f );
		NParticle::Start( Double, MakeEffect( { T } ), 0, 1.0f, 2.0f );
		NParticle::Start( Frozen, MakeEffect( { T } ), 0, 1.0f, 0.0f );
		Run( Normal, 0.125f, 8 );
		Run( Double, 0.125f, 8 );
		Run( Frozen, 0.125f, 8 );
		C.Check( Normal.GetNumParticles() == 10 && Double.GetNumParticles() == 20 && Double.Time == 2.0f && Frozen.GetNumParticles() == 0 && Frozen.Time == 0.0f && !Frozen.IsFinished(), "the play speed scales time (x1: 10 in 1 s, x2: 20, x0: frozen and not finished)" );
		FParticleSimulation Negative;
		NParticle::Start( Negative, MakeEffect( { T } ), 0, 1.0f, -3.0f );
		Run( Negative, 0.125f, 8 );
		C.Check( Negative.GetNumParticles() == 0 && Negative.Time == 0.0f, "a negative play speed does not run time backwards" );

		// エミッターが無い
		FParticleSimulation None;
		NParticle::Start( None, MakeEffect( {} ) );
		C.Check( None.IsFinished() && None.bStarted && !None.bEmitting && None.Emitters.IsEmpty(), "an effect without emitters is finished at once" );
		FParticleSimulation NoDefinition;
		NParticle::Start( NoDefinition, TSharedPtr<const FParticleEffectDefinition>() );
		NParticle::Advance( NoDefinition, 0.1f );
		TArray<NParticle::FDrawParticle> Nothing;
		NParticle::CollectDrawParticles( NoDefinition, Nothing );
		C.Check( NoDefinition.IsFinished() && Nothing.IsEmpty(), "starting with no definition is harmless" );
		FParticleSimulation NotStarted;
		NParticle::Advance( NotStarted, 0.1f );
		C.Check( !NotStarted.IsFinished() && NotStarted.GetNumParticles() == 0, "a simulation that was never started is not finished, and does not move" );
	}

	//=========================================================================
	// 空間と拡大
	//=========================================================================
	void TestSpace( FChecker& C )
	{
		C.Group = "effect-sim-space";
		// 世界の空間: 生まれたあとは、エフェクトが動いてもついて来ない
		FParticleEmitterDefinition W = QuietEmitter();
		W.Bursts.Add( Burst( 0.0f, 1 ) );
		FParticleSimulation World;
		NParticle::Start( World, MakeEffect( { W } ) );
		World.Transform = FTransform( FVector3( 1.0f, 2.0f, 3.0f ) );
		NParticle::Advance( World, 0.125f );
		World.Transform = FTransform( FVector3( 10.0f, 0.0f, 0.0f ) );
		NParticle::Advance( World, 0.125f );
		C.Check( Near3( DrawPosition( World ), FVector3( 1.0f, 2.0f, 3.0f ) ), "a World-space particle stays where it was born when the effect moves on" );

		// ローカルの空間: エフェクトについて来る・向きも
		FParticleEmitterDefinition L = QuietEmitter();
		L.Space = EParticleSpace::Local;
		L.Bursts.Add( Burst( 0.0f, 1 ) );
		L.Speed = { 1.0f, 1.0f };
		L.Direction = FVector3( 1.0f, 0.0f, 0.0f );
		FParticleSimulation Local;
		NParticle::Start( Local, MakeEffect( { L } ) );
		Local.Transform = FTransform( FVector3( 5.0f, 0.0f, 0.0f ) );
		NParticle::Advance( Local, 0.125f );
		NParticle::Advance( Local, 1.0f );
		C.Check( Near3( DrawPosition( Local ), FVector3( 6.0f, 0.0f, 0.0f ) ), "a Local-space particle is placed from the effect: born at (5, 0, 0), then moved 1 along its own axis (6, 0, 0)" );
		Local.Transform = FTransform( FVector3( 10.0f, 0.0f, 0.0f ) );
		C.Check( Near3( DrawPosition( Local ), FVector3( 11.0f, 0.0f, 0.0f ) ), "when the effect moves, a Local particle moves with it (11, 0, 0)" );
		const FQuaternion Turn = FQuaternion::FromAxisAngle( FVector3::Up, 1.0471976f );
		Local.Transform = FTransform( FVector3( 10.0f, 0.0f, 0.0f ), Turn );
		const FVector3 Turned = Local.Transform.Position + Turn.Rotate( FVector3( 1.0f, 0.0f, 0.0f ) );
		C.Check( Near3( DrawPosition( Local ), Turned ) && !Near3( DrawPosition( Local ), FVector3( 11.0f, 0.0f, 0.0f ) ), "and when the effect turns, the particle turns around it" );

		// 世界の空間で生まれる向きは、エフェクトの向きに従う
		FParticleSimulation Aimed;
		FParticleEmitterDefinition A = QuietEmitter();
		A.Bursts.Add( Burst( 0.0f, 1 ) );
		A.Speed = { 2.0f, 2.0f };
		A.Direction = FVector3( 1.0f, 0.0f, 0.0f );
		NParticle::Start( Aimed, MakeEffect( { A } ) );
		Aimed.Transform = FTransform( FVector3::Zero, Turn );
		NParticle::Advance( Aimed, 0.125f );
		C.Check( Near3( Aimed.Emitters[0].Particles[0].Velocity, Turn.Rotate( FVector3( 2.0f, 0.0f, 0.0f ) ) ), "a World particle is born moving along the effect's turned axis" );

		// 重力はワールドの向き: ひっくり返したエフェクトの、ローカルの粒子も下へ落ちる
		FParticleEmitterDefinition G = QuietEmitter();
		G.Space = EParticleSpace::Local;
		G.Bursts.Add( Burst( 0.0f, 1 ) );
		G.Gravity = FVector3( 0.0f, -10.0f, 0.0f );
		FParticleSimulation Flipped;
		NParticle::Start( Flipped, MakeEffect( { G } ) );
		Flipped.Transform = FTransform( FVector3( 0.0f, 5.0f, 0.0f ), FQuaternion::FromAxisAngle( FVector3::Right, 1.5707964f ) );   // 90 度（180 度だと、回転と逆の回転が同じで、向きの取り違えが見えない）
		NParticle::Advance( Flipped, 0.125f );
		bool bFalls = true;
		for ( int32 K = 1; K <= 4; ++K )
		{
			NParticle::Advance( Flipped, 0.125f );
			bFalls = bFalls && std::fabs( DrawPosition( Flipped ).y - FallHeight( 5.0, -10.0, 0.125, K ) ) < 1.0e-3;
		}
		C.Check( bFalls, "gravity keeps the world's direction for a Local particle of an effect turned by 90 degrees (it falls, following the same formula)" );

		// 拡大: 大きさ・速さ・半径に掛かる（各軸の絶対値の平均 x 再生の Scale）
		FParticleEmitterDefinition Z = QuietEmitter();
		Z.Bursts.Add( Burst( 0.0f, 1 ) );
		Z.Speed = { 2.0f, 2.0f };
		Z.StartSize = { 0.2f, 0.2f };
		Z.EndSize = { 0.4f, 0.4f };
		FParticleSimulation Big;
		NParticle::Start( Big, MakeEffect( { Z } ), 0, 1.5f );
		Big.Transform = FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 2.0f, 2.0f, 2.0f ) );
		NParticle::Advance( Big, 0.125f );
		TArray<NParticle::FDrawParticle> Drawn;
		NParticle::CollectDrawParticles( Big, Drawn );
		C.Check( std::fabs( NParticle::GetEffectScale( Big ) - 3.0f ) < 1.0e-5f && Drawn.Num() == 1 && std::fabs( Drawn[0].Size - 0.6f ) < 1.0e-5f && std::fabs( Big.Emitters[0].Particles[0].Velocity.y - 6.0f ) < 1.0e-4f, "scale 2 of the object x 1.5 of the player = 3 on size (0.6) and speed (6)" );
		FParticleSimulation Mixed;
		NParticle::Start( Mixed, MakeEffect( { Z } ) );
		Mixed.Transform = FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 1.0f, -2.0f, 3.0f ) );
		C.Check( std::fabs( NParticle::GetEffectScale( Mixed ) - 2.0f ) < 1.0e-5f, "a non-uniform scale (1, -2, 3) uses the mean of the absolute values (2)" );
		Mixed.Transform = FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( std::numeric_limits<float32>::quiet_NaN(), 1.0f, 1.0f ) );
		C.Check( std::isfinite( NParticle::GetEffectScale( Mixed ) ), "a scale that is not a number does not spoil the size" );
	}

	//=========================================================================
	// 出る形
	//=========================================================================
	void TestShapes( FChecker& C )
	{
		C.Group = "effect-sim-shape";
		const auto Emit = [&]( const FParticleEmitterDefinition& E, FParticleSimulation& Sim, uint32 Seed = 1 )
		{
			NParticle::Start( Sim, MakeEffect( { E }, 0.0f, true, Seed ) );
			NParticle::Advance( Sim, 0.125f );
		};
		FParticleEmitterDefinition Cone = QuietEmitter();
		Cone.Bursts.Add( Burst( 0.0f, 400 ) );
		Cone.Speed = { 1.0f, 1.0f };
		Cone.Direction = FVector3( 3.0f, 0.0f, 0.0f );   // 長さは使わない
		Cone.ConeAngle = 0.0f;
		FParticleSimulation Straight;
		Emit( Cone, Straight );
		bool bAlong = Straight.GetNumParticles() == 400;
		for ( const FParticle& P : Straight.Emitters[0].Particles ) bAlong = bAlong && Near3( P.Velocity, FVector3( 1.0f, 0.0f, 0.0f ), 1.0e-4f ) && Near3( P.Position, FVector3::Zero, 1.0e-4f );
		C.Check( bAlong, "a cone of 0 degrees and radius 0 sends every particle straight along the axis (the axis length does not matter)" );

		Cone.ConeAngle = 30.0f;
		FParticleSimulation Spread;
		Emit( Cone, Spread );
		float32 MaxAngle = 0.0f, SumDot = 0.0f;
		bool bUnit = true;
		for ( const FParticle& P : Spread.Emitters[0].Particles )
		{
			const float32 Dot = std::clamp( P.Velocity.x, -1.0f, 1.0f );
			MaxAngle = std::max( MaxAngle, std::acos( Dot ) );
			SumDot += Dot;
			bUnit = bUnit && std::fabs( P.Velocity.Length() - 1.0f ) < 1.0e-4f;
		}
		const float32 MaxDegrees = MaxAngle * 57.29578f;
		C.Check( bUnit && MaxDegrees <= 30.01f && MaxDegrees > 25.0f && std::fabs( SumDot / 400.0f - 0.9330f ) < 0.012f, "a cone of 30 degrees: every direction within 30 degrees of the axis, some near the edge, speed kept, and spread evenly over the solid angle (mean cosine (1 + cos 30) / 2 = 0.933) (largest %.1f degrees, mean cosine %.3f)", MaxDegrees, SumDot / 400.0f );

		Cone.ConeAngle = 180.0f;
		FParticleSimulation Everywhere;
		Emit( Cone, Everywhere );
		int32 Behind = 0;
		for ( const FParticle& P : Everywhere.Emitters[0].Particles ) Behind += P.Velocity.x < 0.0f ? 1 : 0;
		C.Check( Behind > 120 && Behind < 280, "a cone of 180 degrees goes every way (%d of 400 backwards)", Behind );

		// 底の円: 軸に垂直な円盤の中
		Cone.ConeAngle = 0.0f;
		Cone.ShapeRadius = 2.0f;
		FParticleSimulation Disk;
		Emit( Cone, Disk );
		bool bInDisk = true;
		float32 FarthestDisk = 0.0f, SumSquare = 0.0f;
		for ( const FParticle& P : Disk.Emitters[0].Particles )
		{
			bInDisk = bInDisk && std::fabs( P.Position.x ) < 1.0e-4f;   // 軸は X
			const float32 Distance = FVector3( 0.0f, P.Position.y, P.Position.z ).Length();
			FarthestDisk = std::max( FarthestDisk, Distance );
			SumSquare += Distance * Distance;
		}
		const float32 MeanSquare = SumSquare / 400.0f / 4.0f;   // 半径 2 の 2 乗で割る。面積に対して一様なら 1/2
		C.Check( bInDisk && FarthestDisk <= 2.001f && FarthestDisk > 1.7f && std::fabs( MeanSquare - 0.5f ) < 0.06f, "a cone base of radius 2 puts the particles in a disk across the axis, evenly over its area (farthest %.2f, mean of distance^2 / radius^2 %.3f, 0.5 expected)", FarthestDisk, MeanSquare );

		// 球: 球の中から、中心の外向きへ
		FParticleEmitterDefinition Ball = QuietEmitter();
		Ball.Bursts.Add( Burst( 0.0f, 400 ) );
		Ball.Shape = EParticleShape::Sphere;
		Ball.ShapeRadius = 2.0f;
		Ball.Speed = { 3.0f, 3.0f };
		FParticleSimulation Sphere;
		Emit( Ball, Sphere );
		bool bOutward = true;
		float32 FarthestBall = 0.0f, SumDistance = 0.0f;
		int32 Outside15 = 0;
		for ( const FParticle& P : Sphere.Emitters[0].Particles )
		{
			const float32 Distance = P.Position.Length();
			FarthestBall = std::max( FarthestBall, Distance );
			SumDistance += Distance;
			Outside15 += Distance > 1.5f ? 1 : 0;
			if ( Distance > 0.01f ) bOutward = bOutward && Near3( P.Position / Distance, P.Velocity / 3.0f, 1.0e-3f );
			bOutward = bOutward && std::fabs( P.Velocity.Length() - 3.0f ) < 1.0e-3f;
		}
		const float32 MeanDistance = SumDistance / 400.0f / 2.0f;   // 半径で割る。体積に対して一様なら 3/4
		C.Check( bOutward && FarthestBall <= 2.001f && Outside15 > 180 && std::fabs( MeanDistance - 0.75f ) < 0.04f, "a sphere of radius 2: inside it, evenly over its volume, moving straight away from the centre at the given speed (farthest %.2f, %d beyond 1.5, mean distance / radius %.3f, 0.75 expected)", FarthestBall, Outside15, MeanDistance );

		// 箱: 箱の中から、軸の向きへ
		FParticleEmitterDefinition Box = QuietEmitter();
		Box.Bursts.Add( Burst( 0.0f, 400 ) );
		Box.Shape = EParticleShape::Box;
		Box.BoxSize = FVector3( 2.0f, 4.0f, 6.0f );
		Box.Speed = { 1.0f, 1.0f };
		FParticleSimulation Crate;
		Emit( Box, Crate );
		bool bInBox = true, bUp = true;
		float32 MaxX = 0.0f, MaxY = 0.0f, MaxZ = 0.0f;
		for ( const FParticle& P : Crate.Emitters[0].Particles )
		{
			bInBox = bInBox && std::fabs( P.Position.x ) <= 1.001f && std::fabs( P.Position.y ) <= 2.001f && std::fabs( P.Position.z ) <= 3.001f;
			bUp = bUp && Near3( P.Velocity, FVector3( 0.0f, 1.0f, 0.0f ), 1.0e-4f );
			MaxX = std::max( MaxX, std::fabs( P.Position.x ) );
			MaxY = std::max( MaxY, std::fabs( P.Position.y ) );
			MaxZ = std::max( MaxZ, std::fabs( P.Position.z ) );
		}
		C.Check( bInBox && bUp && MaxX > 0.8f && MaxY > 1.6f && MaxZ > 2.4f, "a box of 2 x 4 x 6: inside it, filling it, moving along the axis (largest %.2f, %.2f, %.2f)", MaxX, MaxY, MaxZ );

		// 軸が上に近いときも、向きの基準が壊れない
		for ( const FVector3& Axis : { FVector3( 0.0f, 1.0f, 0.0f ), FVector3( 0.0f, -1.0f, 0.0f ), FVector3( 0.0f, 0.995f, 0.1f ) } )
		{
			FParticleEmitterDefinition Up = QuietEmitter();
			Up.Bursts.Add( Burst( 0.0f, 50 ) );
			Up.Direction = Axis;
			Up.ConeAngle = 20.0f;
			Up.ShapeRadius = 1.0f;
			Up.Speed = { 1.0f, 1.0f };
			FParticleSimulation Sim;
			Emit( Up, Sim );
			bool bGood = Sim.GetNumParticles() == 50;
			const FVector3 Unit = Axis.Normalized();
			for ( const FParticle& P : Sim.Emitters[0].Particles ) bGood = bGood && std::isfinite( P.Velocity.x + P.Velocity.y + P.Velocity.z ) && std::fabs( P.Velocity.Length() - 1.0f ) < 1.0e-3f && FVector3::Dot( P.Velocity, Unit ) >= 0.93f && std::fabs( FVector3::Dot( P.Position, Unit ) ) < 1.0e-3f;
			C.Check( bGood, "the cone works for an axis near the vertical (%.3f, %.3f, %.3f): finite, unit speed, within 20 degrees, base across the axis", Axis.x, Axis.y, Axis.z );
		}

		// 範囲: 生まれるときの値は範囲の中に散らばる
		FParticleEmitterDefinition Ranges = QuietEmitter();
		Ranges.Bursts.Add( Burst( 0.0f, 400 ) );
		Ranges.Lifetime = { 1.0f, 2.0f };
		Ranges.Speed = { 1.0f, 3.0f };
		Ranges.StartSize = { 0.5f, 1.5f };
		Ranges.EndSize = { 2.0f, 4.0f };
		Ranges.StartAngle = { 0.0f, 90.0f };
		Ranges.Spin = { -10.0f, 10.0f };
		FParticleSimulation Wide;
		Emit( Ranges, Wide );
		struct FStat
		{
			float32 Min = 1.0e9f, Max = -1.0e9f, Sum = 0.0f;
			void Add( float32 V )
			{
				Min = std::min( Min, V );
				Max = std::max( Max, V );
				Sum += V;
			}
		};
		FStat Life, Speed, Size, EndSize, Angle, Spin;
		for ( const FParticle& P : Wide.Emitters[0].Particles )
		{
			Life.Add( P.Lifetime );
			Speed.Add( P.Velocity.Length() );
			Size.Add( P.StartSize );
			EndSize.Add( P.EndSize );
			Angle.Add( P.Angle * 57.29578f );
			Spin.Add( P.Spin * 57.29578f );
		}
		const auto Spreads = [&]( const FStat& S, float32 Low, float32 High ) { return S.Min >= Low - 1.0e-3f && S.Max <= High + 1.0e-3f && S.Min < Low + ( High - Low ) * 0.1f && S.Max > High - ( High - Low ) * 0.1f && std::fabs( S.Sum / 400.0f - ( Low + High ) * 0.5f ) < ( High - Low ) * 0.1f; };
		C.Check( Spreads( Life, 1.0f, 2.0f ) && Spreads( Speed, 1.0f, 3.0f ) && Spreads( Size, 0.5f, 1.5f ) && Spreads( EndSize, 2.0f, 4.0f ) && Spreads( Angle, 0.0f, 90.0f ) && Spreads( Spin, -10.0f, 10.0f ),
				 "lifetime, speed, sizes, angle and spin are spread evenly over their ranges (lifetime %.2f..%.2f, speed %.2f..%.2f, size %.2f..%.2f)", Life.Min, Life.Max, Speed.Min, Speed.Max, Size.Min, Size.Max );

		// 範囲の大小が逆でも、中に収まる
		FParticleEmitterDefinition Swapped = QuietEmitter();
		Swapped.Bursts.Add( Burst( 0.0f, 100 ) );
		Swapped.Speed = { 3.0f, 1.0f };
		FParticleSimulation SwappedSim;
		Emit( Swapped, SwappedSim );
		float32 SwappedMin = 99.0f, SwappedMax = -99.0f;
		for ( const FParticle& P : SwappedSim.Emitters[0].Particles )
		{
			SwappedMin = std::min( SwappedMin, P.Velocity.Length() );
			SwappedMax = std::max( SwappedMax, P.Velocity.Length() );
		}
		C.Check( SwappedMin >= 0.999f && SwappedMax <= 3.001f && SwappedMax > 2.5f, "a range written the wrong way round is used the right way round (%.2f..%.2f)", SwappedMin, SwappedMax );
	}

	//=========================================================================
	// 乱数（決まった結果）
	//=========================================================================
	void TestDeterminism( FChecker& C )
	{
		C.Group = "effect-sim-seed";
		FParticleEmitterDefinition E = FullEmitter();
		E.Space = EParticleSpace::World;
		E.Delay = 0.0f;
		E.Rate = 40.0f;
		const TSharedPtr<FParticleEffectDefinition> Def = MakeEffect( { E, E }, 0.0f, true, 77 );
		FParticleSimulation A, B, Other, ByParam, DefaultSeed;
		NParticle::Start( A, Def );
		NParticle::Start( B, Def );
		NParticle::Start( Other, Def, 78 );
		NParticle::Start( ByParam, Def, 77 );
		TSharedPtr<FParticleEffectDefinition> Seed77 = MakeShared<FParticleEffectDefinition>( *Def );
		NParticle::Start( DefaultSeed, Seed77 );
		Run( A, 0.0625f, 40 );
		Run( B, 0.0625f, 40 );
		Run( Other, 0.0625f, 40 );
		Run( ByParam, 0.0625f, 40 );
		Run( DefaultSeed, 0.0625f, 40 );
		C.Check( A.GetNumParticles() > 10 && SameParticles( A, B ), "the same seed and steps give exactly the same particles (%d)", A.GetNumParticles() );
		C.Check( !SameParticles( A, Other ), "another seed gives different particles" );
		C.Check( SameParticles( A, ByParam ) && SameParticles( A, DefaultSeed ), "a seed of 0 means the effect's own seed (77), and giving 77 is the same" );
		C.Check( !SameParticles( Other, DefaultSeed ), "and the seed given to Start replaces the effect's own" );

		// 2 つのエミッターは、同じ設定でも別の乱数
		bool bSame = A.Emitters[0].Particles.Num() == A.Emitters[1].Particles.Num();
		for ( int32 i = 0; bSame && i < A.Emitters[0].Particles.Num(); ++i ) bSame = A.Emitters[0].Particles[i].Position.x == A.Emitters[1].Particles[i].Position.x;
		C.Check( !bSame, "two emitters with the same settings use different random numbers" );

		// シードが 0 でも、乱数が止まらない
		FParticleSimulation Zero;
		FParticleEmitterDefinition Z = QuietEmitter();
		Z.Bursts.Add( Burst( 0.0f, 50 ) );
		Z.Speed = { 1.0f, 3.0f };
		NParticle::Start( Zero, MakeEffect( { Z }, 0.0f, true, 0 ) );
		NParticle::Advance( Zero, 0.125f );
		float32 Low = 9.0f, High = -9.0f;
		for ( const FParticle& P : Zero.Emitters[0].Particles )
		{
			Low = std::min( Low, P.Velocity.Length() );
			High = std::max( High, P.Velocity.Length() );
		}
		C.Check( High - Low > 0.5f, "an effect whose own seed is 0 still gets varied random numbers (%.2f..%.2f)", Low, High );
	}

	//=========================================================================
	// 描く値
	//=========================================================================
	void TestDrawValues( FChecker& C )
	{
		C.Group = "effect-sim-draw";
		FParticleEmitterDefinition E = QuietEmitter();
		E.Bursts.Add( Burst( 0.0f, 1 ) );
		E.Lifetime = { 2.0f, 2.0f };
		E.StartSize = { 1.0f, 1.0f };
		E.EndSize = { 0.0f, 0.0f };
		E.StartColor = FColor32( 255, 0, 100, 255 );
		E.EndColor = FColor32( 0, 200, 100, 0 );
		E.StartAngle = { 45.0f, 45.0f };
		FParticleSimulation Sim;
		NParticle::Start( Sim, MakeEffect( { E } ) );
		NParticle::Advance( Sim, 0.125f );
		TArray<NParticle::FDrawParticle> Items;
		NParticle::CollectDrawParticles( Sim, Items );
		C.Check( Items.Num() == 1 && Items[0].Size == 1.0f && Items[0].Color.r == 255 && Items[0].Color.g == 0 && Items[0].Color.b == 100 && Items[0].Color.a == 255 && std::fabs( Items[0].Angle - 0.7853982f ) < 1.0e-5f && Items[0].Emitter == 0,
				 "a newborn particle is drawn with its start size and colour and its angle" );
		Sim.Emitters[0].Particles[0].Age = 1.0f;   // 寿命の半分
		NParticle::CollectDrawParticles( Sim, Items );
		C.Check( Items.Num() == 1 && std::fabs( Items[0].Size - 0.5f ) < 1.0e-5f && Items[0].Color.r == 128 && Items[0].Color.g == 100 && Items[0].Color.b == 100 && Items[0].Color.a == 128, "half way through its life, size and colour (with alpha) are half way between (127.5 rounds to 128)" );
		Sim.Emitters[0].Particles[0].Age = 1.5f;
		NParticle::CollectDrawParticles( Sim, Items );
		C.Check( Items.Num() == 1 && std::fabs( Items[0].Size - 0.25f ) < 1.0e-5f && Items[0].Color.r == 64 && Items[0].Color.g == 150 && Items[0].Color.a == 64, "three quarters through, a quarter of the way left" );
		Sim.Emitters[0].Particles[0].Age = 5.0f;   // 寿命を超えていても、値は端で止まる
		NParticle::CollectDrawParticles( Sim, Items );
		C.Check( Items.Num() == 1 && Items[0].Size == 0.0f && Items[0].Color.r == 0 && Items[0].Color.a == 0, "past its life the values stay at the end (never beyond)" );

		// 順番: エミッターの順、生まれた順。出すたびに空にする
		FParticleEmitterDefinition A = QuietEmitter();
		A.Rate = 8.0f;
		A.Direction = FVector3( 1.0f, 0.0f, 0.0f );
		A.Speed = { 1.0f, 1.0f };
		FParticleEmitterDefinition B = QuietEmitter();
		B.Bursts.Add( Burst( 0.0f, 2 ) );
		FParticleSimulation Two;
		NParticle::Start( Two, MakeEffect( { A, B } ) );
		Run( Two, 0.125f, 3 );
		Items.Add( NParticle::FDrawParticle() );   // 前の中身は消える
		NParticle::CollectDrawParticles( Two, Items );
		bool bOrdered = Items.Num() == 5;
		for ( int32 i = 0; bOrdered && i < Items.Num(); ++i ) bOrdered = Items[i].Emitter == ( i < 3 ? 0 : 1 );
		bOrdered = bOrdered && Items[0].Position.x > Items[1].Position.x && Items[1].Position.x > Items[2].Position.x;
		C.Check( bOrdered, "the particles come emitter by emitter, oldest first (the oldest of the first emitter has gone farthest), and the list is emptied first" );
	}

	//=========================================================================
	// おかしな値でも落ちない・止まらない
	//=========================================================================
	void TestRobust( FChecker& C )
	{
		C.Group = "effect-sim-robust";
		const float32 NaN = std::numeric_limits<float32>::quiet_NaN();
		const float32 Inf = std::numeric_limits<float32>::infinity();

		FParticleEmitterDefinition E = QuietEmitter();
		E.Bursts.Add( Burst( 0.0f, 3 ) );
		FParticleSimulation Sim;
		NParticle::Start( Sim, MakeEffect( { E } ) );
		const float32 Bad[] = { NaN, Inf, -Inf, -1.0f, 0.0f };
		for ( const float32 Dt : Bad ) NParticle::Advance( Sim, Dt );
		C.Check( Sim.GetNumParticles() == 0 && Sim.Time == 0.0f, "a step that is not a positive finite number is ignored (NaN, infinity, negative, zero)" );
		NParticle::Advance( Sim, 0.125f );
		C.Check( Sim.GetNumParticles() == 3 && Sim.Time == 0.125f, "and the next good step works" );

		// 悪い定義: 寿命 0・負の速さ・数ではない値
		FParticleEmitterDefinition Wild = QuietEmitter();
		Wild.Bursts.Add( Burst( 0.0f, 20 ) );
		Wild.Lifetime = { 0.0f, 0.0f };
		Wild.Speed = { -5.0f, -1.0f };
		Wild.StartSize = { -1.0f, -0.5f };
		Wild.EndSize = { NaN, NaN };
		Wild.Gravity = FVector3( NaN, Inf, 0.0f );
		Wild.Drag = NaN;
		Wild.ShapeRadius = NaN;
		Wild.ConeAngle = NaN;
		Wild.Direction = FVector3::Zero;
		Wild.Spin = { NaN, NaN };
		Wild.StartAngle = { Inf, Inf };
		FParticleSimulation Wildest;
		NParticle::Start( Wildest, MakeEffect( { Wild } ), 0, NaN, NaN );
		NParticle::Advance( Wildest, 0.125f );
		TArray<NParticle::FDrawParticle> Items;
		NParticle::CollectDrawParticles( Wildest, Items );
		bool bFinite = Items.Num() == 20;
		for ( const NParticle::FDrawParticle& P : Items ) bFinite = bFinite && std::isfinite( P.Position.x + P.Position.y + P.Position.z + P.Size + P.Angle ) && P.Size >= 0.0f && Near3( P.Position, FVector3::Zero );
		C.Check( bFinite, "an effect full of bad values still makes its particles with finite positions and sizes, bad numbers taken as 0 (%d particles)", Items.Num() );
		Run( Wildest, 0.125f, 4 );
		C.Check( Wildest.GetNumParticles() == 0 && Wildest.IsFinished() == false, "and they die at once (the lifetime was raised to a tiny positive one), while the endless effect goes on" );

		FParticleSimulation Short;
		FParticleEmitterDefinition Zero = QuietEmitter();
		Zero.Bursts.Add( Burst( 0.0f, 5 ) );
		Zero.Lifetime = { 0.0f, 0.0f };
		NParticle::Start( Short, MakeEffect( { Zero } ) );
		NParticle::Advance( Short, 0.125f );
		const int32 Born = Short.GetNumParticles();
		NParticle::Advance( Short, 0.125f );
		C.Check( Born == 5 && Short.GetNumParticles() == 0 && Short.Emitters[0].Particles.Num() == 0, "a lifetime of 0 is raised to a tiny positive one: the particles are born and die at the next step" );

		// 定義とシミュレーションが食い違っても、進めない（落ちない）
		FParticleSimulation Mismatch;
		NParticle::Start( Mismatch, MakeEffect( { E } ) );
		Mismatch.Emitters.Add( FParticleEmitterState() );
		NParticle::Advance( Mismatch, 0.125f );
		C.Check( Mismatch.Time == 0.0f && Mismatch.GetNumParticles() == 0, "a simulation with more emitter states than the definition has emitters is left alone" );

		// 多すぎるエミッターは、上限で切る
		TSharedPtr<FParticleEffectDefinition> Many = MakeEffect( {} );
		// 上限までの各状態が動き、上限を超えた定義を参照しないことを確かめる。
		FParticleEmitterDefinition CappedEmitter = QuietEmitter();
		CappedEmitter.Rate = 8.0f;
		CappedEmitter.Bursts.Add( Burst( 0.0f, 1 ) );
		for ( int32 i = 0; i < FParticleEffectDefinition::MaxEmitters + 5; ++i ) Many->Emitters.Add( CappedEmitter );
		FParticleSimulation Cut;
		NParticle::Start( Cut, Many );
		C.Check( Cut.Emitters.Num() == FParticleEffectDefinition::MaxEmitters, "more emitters than the limit are not run (%d)", Cut.Emitters.Num() );
		NParticle::Advance( Cut, 0.125f );
		NParticle::CollectDrawParticles( Cut, Items );
		C.Check( Cut.Time == 0.125f && Cut.GetNumParticles() == FParticleEffectDefinition::MaxEmitters * 2 && Items.Num() == FParticleEffectDefinition::MaxEmitters * 2 && Items.Last().Emitter == FParticleEffectDefinition::MaxEmitters - 1, "上限を超える定義も、開始時に残したエミッターだけを進めて描く" );
		NParticle::Advance( Cut, 0.125f );
		C.Check( Cut.GetNumParticles() == FParticleEffectDefinition::MaxEmitters * 3, "上限までのエミッターは次の刻みでも生成を続ける" );
		NParticle::Stop( Cut, true );
		C.Check( Cut.IsFinished(), "上限を超える定義も即時停止できる" );

		// 有限の最大値や大きな一括生成でも、整数の範囲や空き数を超えて処理しない。
		FParticleEmitterDefinition Flood = QuietEmitter();
		Flood.MaxParticles = 1;
		Flood.Rate = std::numeric_limits<float32>::max();
		Flood.Lifetime = { 0.25f, 0.25f };
		Flood.Bursts.Add( Burst( 0.0f, std::numeric_limits<int32>::max() ) );
		Flood.Bursts.Add( Burst( 0.0f, std::numeric_limits<int32>::max() ) );
		// 空きが一つしかない再生状態。
		FParticleSimulation Flooded;
		NParticle::Start( Flooded, MakeEffect( { Flood } ) );
		NParticle::Advance( Flooded, 0.125f );
		C.Check( Flooded.GetNumParticles() == 1 && Flooded.Emitters[0].BurstsDone == 2 && std::isfinite( Flooded.Emitters[0].Carry ) && Flooded.Emitters[0].Carry == 0.0, "巨大な生成速度と最大数の一括生成でも、一つの空きだけを埋めて余剰を捨てる" );
		NParticle::Advance( Flooded, 0.125f );
		C.Check( Flooded.GetNumParticles() == 1 && Flooded.Emitters[0].Particles[0].Age == 0.125f, "上限に達した後も、捨てる生成数だけ反復せず既存の粒子を進める" );
		NParticle::Advance( Flooded, 0.125f );
		C.Check( Flooded.GetNumParticles() == 1 && Flooded.Emitters[0].Particles[0].Age == 0.0f, "巨大な生成速度でも、寿命が尽きて空いた枠へ新しい粒子を生成できる" );

		// 速度との積が有限でなくなった刻みは、通常の刻みと混ぜない。
		FParticleSimulation OverflowedTime;
		NParticle::Start( OverflowedTime, MakeEffect( { E } ), 0, 1.0f, std::numeric_limits<float32>::max() );
		NParticle::Advance( OverflowedTime, 2.0f );
		C.Check( OverflowedTime.Time == 0.0f && OverflowedTime.GetNumParticles() == 0, "刻みが有限でも再生速度との積が範囲を超えたら進めない" );
		OverflowedTime.Speed = 1.0f;
		OverflowedTime.Time = std::numeric_limits<float32>::max();
		NParticle::Advance( OverflowedTime, std::numeric_limits<float32>::max() );
		C.Check( OverflowedTime.Time == std::numeric_limits<float32>::max() && OverflowedTime.GetNumParticles() == 0, "現在時刻と刻みの和が範囲を超える場合も状態を保持する" );
	}

	//=========================================================================
	// バックエンドの一覧
	//=========================================================================
	class FFakePlayback final : public IEffectPlayback
	{
	public:
		void SetTransform( const FTransform& ) override {}
		void Update( float32 ) override {}
		void Record( FDrawContext& ) const override {}
		void Stop( bool ) override {}
		bool IsFinished() const override { return false; }
	};

	class FFakeBackend final : public IEffectBackend
	{
	public:
		FFakeBackend( const char* InName, const char* InExtension, bool bInFail = false ) : Name( InName ), Extension( InExtension ), bFail( bInFail ) {}
		const char* GetName() const override { return Name; }
		bool CanPlay( const FString& Path ) const override { return Path.EndsWith( Extension, ESearchCase::IgnoreCase ); }
		TSharedPtr<IEffectPlayback> Play( const FString& Path, CAssetManager*, const FEffectPlayParams&, const FTransform& ) override
		{
			++NumPlays;
			LastPath = Path;
			return bFail ? TSharedPtr<IEffectPlayback>() : TSharedPtr<IEffectPlayback>( MakeShared<FFakePlayback>() );
		}

		const char* Name;
		FString Extension;
		bool bFail;
		int32 NumPlays = 0;
		FString LastPath;
	};

	void TestRegistry( FChecker& C )
	{
		C.Group = "effect-backends";
		FEffectBackendRegistry Registry;
		C.Check( Registry.Num() == 0 && Registry.Find( "a.fx" ) == nullptr && !Registry.Play( "a.fx", nullptr, FEffectPlayParams(), FTransform() ).IsValid(), "an empty list handles nothing" );
		TSharedPtr<FFakeBackend> Fx = MakeShared<FFakeBackend>( "Fx", ".fx" );
		TSharedPtr<FFakeBackend> Fy = MakeShared<FFakeBackend>( "Fy", ".fy" );
		Registry.Add( Fx );
		Registry.Add( Fy );
		Registry.Add( TSharedPtr<IEffectBackend>() );
		C.Check( Registry.Num() == 2 && Registry.Find( "Fire.fx" ) == Fx.Get() && Registry.Find( "Fire.FY" ) == Fy.Get() && Registry.Find( "Fire.zz" ) == nullptr && Registry.Find( "" ) == nullptr, "the backend whose CanPlay accepts the file is found (ignoring case); nothing for an unknown kind; a null backend is not added" );

		const TSharedPtr<IEffectPlayback> Played = Registry.Play( "Fire.fx", nullptr, FEffectPlayParams(), FTransform() );
		C.Check( Played.IsValid() && Fx->NumPlays == 1 && Fx->LastPath == "Fire.fx" && Fy->NumPlays == 0, "Play goes to the backend that handles the file, with the path" );

		// 同じ名前は置き換え（順番は最初のまま）
		TSharedPtr<FFakeBackend> Fx2 = MakeShared<FFakeBackend>( "Fx", ".fx" );
		Registry.Add( Fx2 );
		C.Check( Registry.Num() == 2 && Registry.Find( "a.fx" ) == Fx2.Get(), "adding a backend with an existing name replaces it" );

		// 読み込めなかったら、次のバックエンドに聞く
		TSharedPtr<FFakeBackend> Failing = MakeShared<FFakeBackend>( "Failing", ".fx", true );
		TSharedPtr<FFakeBackend> Backup = MakeShared<FFakeBackend>( "Backup", ".fx" );
		FEffectBackendRegistry Chain;
		Chain.Add( Failing );
		Chain.Add( Backup );
		const TSharedPtr<IEffectPlayback> Fallback = Chain.Play( "b.fx", nullptr, FEffectPlayParams(), FTransform() );
		C.Check( Fallback.IsValid() && Failing->NumPlays == 1 && Backup->NumPlays == 1 && Chain.Find( "b.fx" ) == Failing.Get(), "when the first backend cannot load the file, the next one that handles it is asked (Find still names the first)" );
		FEffectBackendRegistry AllFail;
		AllFail.Add( Failing );
		C.Check( !AllFail.Play( "c.fx", nullptr, FEffectPlayParams(), FTransform() ).IsValid() && Failing->NumPlays == 2, "when all of them fail, nothing is played" );

		// 取り除く
		C.Check( Registry.Remove( "Fy" ) && Registry.Num() == 1 && Registry.Find( "a.fy" ) == nullptr && !Registry.Remove( "Fy" ) && !Registry.Remove( "" ), "a backend is removed by name (once)" );

		// 内蔵
		FEffectBackendRegistry Builtin = FEffectBackendRegistry::CreateWithBuiltins();
		IEffectBackend* Particle = Builtin.Find( "Effects/Fire.effect.json" );
		C.Check( Builtin.Num() == 1 && Particle != nullptr && FString( Particle->GetName() ) == "Particle" && Builtin.Find( "Effects/FIRE.EFFECT.JSON" ) == Particle && Builtin.Find( "Effects/Fire.json" ) == nullptr && Builtin.Find( "Effects/Fire.efk" ) == nullptr,
				 "the built-in list has the particle backend, which takes .effect.json (any case) and nothing else" );
		const int32 GlobalBefore = FEffectBackendRegistry::Global().Num();
		Builtin.Add( Fx );
		C.Check( FEffectBackendRegistry::Global().Num() == GlobalBefore && FEffectBackendRegistry::Global().Find( "x.effect.json" ) != nullptr && FEffectBackendRegistry::Global().Find( "x.fx" ) == nullptr, "a list made with CreateWithBuiltins is separate from the application-wide one, which has the particle backend" );
		const TSharedPtr<IEffectPlayback> NoAssets = Builtin.Play( "Effects/Fire.effect.json", nullptr, FEffectPlayParams(), FTransform() );
		const TSharedPtr<IEffectPlayback> OtherKind = Builtin.Play( "Effects/Fire.fx", nullptr, FEffectPlayParams(), FTransform() );
		C.Check( !NoAssets.IsValid() && OtherKind.IsValid(), "the particle backend cannot play without the asset manager, and another kind still reaches its own backend" );
	}

	//=========================================================================
	// 組み込みの丸の画像
	//=========================================================================
	void TestSoftDot( FChecker& C )
	{
		C.Group = "effect-softdot";
		const TArray<uint8> Image = NEffect::MakeSoftDotImage();
		const int32 Size = 64;
		C.Check( Image.Num() == 18 + Size * Size * 4, "the image is a 64 x 64 32-bit TGA: 18 header bytes + 16384 bytes of pixels (%d)", Image.Num() );
		C.Check( Image[2] == 2 && Image[12] == 64 && Image[13] == 0 && Image[14] == 64 && Image[15] == 0 && Image[16] == 32 && Image[17] == ( 8 | 0x20 ), "the header says: uncompressed colour, 64 x 64, 32 bits, 8 alpha bits, top-left origin" );
		const auto Pixel = [&]( int32 X, int32 Y, int32 Channel ) { return static_cast<int32>( Image[18 + ( Y * Size + X ) * 4 + Channel] ); };   // B, G, R, A
		C.Check( Pixel( 31, 31, 3 ) >= 235 && Pixel( 32, 32, 3 ) >= 235 && Pixel( 0, 0, 3 ) == 0 && Pixel( 63, 0, 3 ) == 0 && Pixel( 0, 63, 3 ) == 0 && Pixel( 63, 63, 3 ) == 0 && Pixel( 0, 32, 3 ) < 8,
				 "opaque in the middle and clear at the corners and the edge (%d, %d, %d)", Pixel( 31, 31, 3 ), Pixel( 0, 0, 3 ), Pixel( 0, 32, 3 ) );
		bool bFalls = true, bWhite = true, bSymmetric = true;
		for ( int32 X = 32; X < Size - 1; ++X ) bFalls = bFalls && Pixel( X, 32, 3 ) >= Pixel( X + 1, 32, 3 );
		for ( int32 Y = 0; Y < Size; ++Y )
		{
			for ( int32 X = 0; X < Size; ++X )
			{
				bWhite = bWhite && Pixel( X, Y, 0 ) == 255 && Pixel( X, Y, 1 ) == 255 && Pixel( X, Y, 2 ) == 255;
				bSymmetric = bSymmetric && Pixel( X, Y, 3 ) == Pixel( Size - 1 - X, Y, 3 ) && Pixel( X, Y, 3 ) == Pixel( X, Size - 1 - Y, 3 ) && Pixel( X, Y, 3 ) == Pixel( Y, X, 3 );
			}
		}
		C.Check( bFalls && bWhite && bSymmetric, "the alpha falls off smoothly from the centre; the colour is white everywhere; the image is symmetric" );
		C.Check( NEffect::MakeSoftDotImage() == Image, "making it again gives the same bytes" );
	}
}

void NWorldTest::RunEffectSimTests( FChecker& C )
{
	TestMotion( C );
	TestEmission( C );
	TestLifeAndLoop( C );
	TestSpace( C );
	TestShapes( C );
	TestDeterminism( C );
	TestDrawValues( C );
	TestRobust( C );
	TestRegistry( C );
	TestSoftDot( C );
}
