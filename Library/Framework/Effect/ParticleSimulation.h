// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "Library/Math/Transform.h"

//=============================================================================
// 粒子のシミュレーション（純粋な計算。DxLib・シーン・ImGui を使わない）
//   定義（FParticleEffectDefinition）と、状態（FParticleSimulation）と、経過時間を入れると、次の状態が決まる。
//   乱数は、シードから決まる（同じシード・同じ時間の刻み・同じ位置の動きなら、同じ結果）。ワーカーの数や実行の順番に依らない。
//
//   FParticleSimulation Simulation;
//   NParticle::Start( Simulation, Definition, /*Seed*/ 0, /*Scale*/ 1.0f, /*Speed*/ 1.0f );
//   Simulation.Transform = FTransform( Position );          // エフェクトの位置・向き（動くものに付けるときは、毎フレーム更新する）
//   NParticle::Advance( Simulation, DeltaTime );
//   TArray<NParticle::FDrawParticle> Particles;
//   NParticle::CollectDrawParticles( Simulation, Particles );   // 描くもの（位置・大きさ・回転・色）
//
//   約束:
//   - 動きは、1 回の Advance ごとの半陰的オイラー法: 速度 += 重力 * dt、速度 *= (1 - 抵抗 * dt)、位置 += 速度 * dt。
//   - 出す数は、Rate * 時間 の端数を覚えておいて、整数になったぶんだけ出す（時間の刻みに依らず、同じ数）。決まった時刻の Burst は、その時刻を過ぎた最初の Advance で出る。
//   - 出た粒子は、その Advance の終わりの位置・向きから始まる（年齢 0）。
//   - 大きさ・速さ・半径は、Scale（再生の指定）と、エフェクトの拡大（Transform.Scale の平均）が掛かる。
//   - 悪い値（寿命 0・非有限の数・負の数）は、安全な範囲に丸める（落ちない。検証が、問題として報告する）。
//=============================================================================
struct FParticle
{
	FVector3 Position = FVector3::Zero;   // World 空間の粒子はワールドの位置、Local 空間の粒子は、エフェクトの軸に沿った、エフェクトの位置からのずれ
	FVector3 Velocity = FVector3::Zero;   // 同じ空間
	float32 Age = 0.0f;
	float32 Lifetime = 1.0f;
	float32 StartSize = 0.0f;
	float32 EndSize = 0.0f;
	float32 Angle = 0.0f;                 // ラジアン
	float32 Spin = 0.0f;                  // ラジアン / 秒
	uint32 Serial = 0;                   // ほかの粒子が消えても変わらない、発生元ごとの生成番号。
};

struct FParticleEmitterState
{
	float64 Carry = 0.0;                  // まだ出していない、数の端数
	int32 BurstsDone = 0;                 // 出し終えた Burst の数
	uint32 Random = 1;                    // 乱数の状態（0 にならない）
	uint32 NextSerial = 0;                // 次に生まれる粒子の生成番号。再生の最初にだけ戻す。
	TArray<FParticle> Particles;          // 生まれた順
};

struct FParticleSimulation
{
	TSharedPtr<const FParticleEffectDefinition> Definition;
	TArray<FParticleEmitterState> Emitters;   // 定義のエミッターと同じ数・同じ並び
	FTransform Transform;                 // エフェクトの、いまの位置・向き・大きさ
	float32 Time = 0.0f;                  // エフェクトの経過時間（ループのとき、Duration の中に巻き戻る）
	float32 Scale = 1.0f;
	float32 Speed = 1.0f;                 // 時間の進み方（0 で止まる）
	bool bLoop = true;
	bool bEmitting = false;
	bool bStarted = false;
	// 雷の動的終点として使うワールド座標。
	FVector3 TargetPosition = FVector3::Zero;
	// 動的終点が指定されているか。
	bool bHasTarget = false;

	int32 GetNumParticles() const;
	// 出し終えて（止めた・ループなしで Duration を過ぎた・エミッターが無い）、粒子も全部消えた
	bool IsFinished() const;
};

namespace NParticle
{
	// 始める（それまでの状態は捨てる）。Seed が 0 なら、定義のシード。LoopOverride: -1 = 定義のまま / 0 = ループしない / 1 = ループする
	void Start( FParticleSimulation& Simulation, const TSharedPtr<const FParticleEffectDefinition>& Definition, uint32 Seed = 0, float32 Scale = 1.0f, float32 Speed = 1.0f, int32 LoopOverride = -1 );

	// DeltaTime 秒（再生の Speed が掛かる）進める。Transform は、呼ぶ前に、いまの位置に更新しておく
	void Advance( FParticleSimulation& Simulation, float32 DeltaTime );

	// 出すのをやめる。残っている粒子は、寿命まで動く（bImmediate なら、すぐ消す）
	void Stop( FParticleSimulation& Simulation, bool bImmediate );

	// 描く粒子 1 つ
	struct FDrawParticle
	{
		FVector3 Position = FVector3::Zero;   // ワールド
		float32 Size = 0.0f;                  // 四角形の 1 辺（m）
		float32 Angle = 0.0f;                 // ラジアン
		FColor32 Color;                       // 寿命の間に、始めの色から終わりの色へ（アルファも）
		int32 Emitter = 0;                    // 定義のエミッターの番号
		uint32 Serial = 0;                    // 生まれたときの番号。雷の形を粒子の残数から独立させる。
		float32 Age = 0.0f;                   // 生まれてからの秒。エフェクトの周回で巻き戻さない。
		float32 NormalizedAge = 0.0f;         // 寿命に対する経過の割合。曲線などの材質評価へ渡す。
	};

	// 描く粒子を、エミッターの順に、生まれた順に集める（Out は空にしてから）
	void CollectDrawParticles( const FParticleSimulation& Simulation, TArray<FDrawParticle>& Out );

	// 拡大の平均（エフェクトの Transform.Scale の各軸の絶対値の平均 x 再生の Scale）
	float32 GetEffectScale( const FParticleSimulation& Simulation );
}
