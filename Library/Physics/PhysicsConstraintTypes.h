// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/MathConstants.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// EConstraintType
//   2 つの剛体のつなぎ方（関節の種類）。
//=============================================================================
enum class EConstraintType : uint8
{
	Fixed,     // 溶接したように、相対的な位置と向きを固定する
	Point,     // 1 点でつなぐ（球関節。その点まわりの回転は自由）
	Distance,  // 2 点の距離を保つ、または範囲に収める（棒・ロープ・ばね）
	Hinge,     // 1 つの軸のまわりにだけ回る（扉・車輪）
	Slider,    // 1 つの軸に沿ってだけ動く（引き出し・ピストン）
	Cone,      // 1 点でつなぎ、ひねり軸の向きを円錐の中に収める（肩のような関節）
};

//=============================================================================
// EConstraintMotorMode
//   Hinge / Slider の駆動（モーター）の状態。
//=============================================================================
enum class EConstraintMotorMode : uint8
{
	Off,       // 駆動なし（自由に動く）
	Velocity,  // 目標の速さへ向けて動かす
	Position,  // 目標の位置（角度）へ向けて、ばねで引き寄せる
};

//=============================================================================
// FConstraintSpring
//   範囲の端や距離を「硬く止める」代わりに、ばねで引き戻すための設定。物理エンジン（Jolt）の型を使わない。
//=============================================================================
struct FConstraintSpring
{
	// ばねの固さ（振動の周波数、Hz）。0 ならばねにしない（硬く止める）。0 以上
	float32 Frequency = 0.0f;

	// 減衰比。0 で減衰なし、1 で臨界減衰（行き過ぎずに収まる）。0 以上
	float32 Damping = 1.0f;
};

//=============================================================================
// FConstraintSettings
//   関節の作り方。物理エンジン（Jolt）の型を使わないので、これを使う側は Jolt に依存しない。
//   位置と向きはワールド座標で指定する。作った瞬間の 2 つの剛体の姿勢が「ずれ 0」で、そこからのずれを抑える。
//   長さは m、角度はラジアン。Hinge の角度・Slider の位置・モーターの向きは、BodyA が BodyB（無ければ世界）に対して動いた量
//   （正の向きは、Hinge が Axis まわりの右手の向き、Slider が Axis の向き）。
//
//   FConstraintSettings::Hinge( DoorPosition, FVector3::Up, 0.0f, NMath::HALF_PI );   // 扉。0..90 度だけ開く
//   FConstraintSettings::Distance( Hook, Weight, 1.0f, 1.0f );                       // 長さ 1 の棒
//   FConstraintSettings Rope = FConstraintSettings::Distance( Hook, Weight, 0.0f, 3.0f );
//   Rope.bCollideConnected = true;                                                    // つないだ相手とも衝突させる
//=============================================================================
struct FConstraintSettings
{
	EConstraintType Type = EConstraintType::Fixed;

	// 取り付ける点（ワールド）。Distance では A 側の点
	FVector3 Anchor = FVector3::Zero;

	// Distance だけ: B 側の点（ワールド）
	FVector3 AnchorB = FVector3::Zero;

	// 軸（ワールド、長さは問わない。零ベクトル・NaN は不可）。
	// Hinge は回る軸、Slider は動く軸、Cone はひねり軸（円錐の中心）。ほかの種類では使わない
	FVector3 Axis = FVector3::Up;

	// 動ける範囲。作った瞬間を 0 とした値で、既定は範囲なし（NMath::MaxFloat の符号違い。無限大も範囲なし）。
	//   Hinge: 角度。LimitMin は -PI..0、LimitMax は 0..PI（超えた値は範囲なしの端として扱う）
	//   Slider: 動いた距離。LimitMin <= 0 <= LimitMax
	//   Distance: 2 点の距離。0 以上で LimitMin <= LimitMax。どちらかが負なら、その側は作った瞬間の距離にする
	//             （両方が負なら、作った瞬間の距離で固定する棒。FConstraintSettings::Distance の既定がこれ）
	// Hinge / Slider で LimitMin == LimitMax かつ LimitSpring が硬いときは、動けないので作れない（Fixed を使う）
	float32 LimitMin = -NMath::MaxFloat;
	float32 LimitMax = NMath::MaxFloat;

	// 範囲の端のばね。Frequency が 0 より大きいと、範囲を超えたとき硬く止めずにばねで引き戻す。
	// Distance では、LimitMin == LimitMax にして、その距離へ引き寄せるばねにも使える
	FConstraintSpring LimitSpring;

	// モーターを使っていないときの摩擦の上限。Hinge は N·m、Slider は N。0 で摩擦なし。Hinge / Slider だけで使う
	float32 MaxFriction = 0.0f;

	// Cone だけ: 円錐の半角（0..PI）。2 つの剛体のひねり軸の間の角度が、これを超えないようにする
	float32 HalfConeAngle = 0.0f;

	// 壊れる限界。関節が点や向きを保つためにかけている力（BreakForce、N）やトルク（BreakTorque、N·m）が、
	// 物理のステップの終わりにこれを超えていたら、関節は壊れて外れる（FPhysicsConstraint::IsBroken。つないでいた剛体は起こされ、衝突に戻る）。
	// 既定は壊れない（NMath::MaxFloat。無限大も同じ）。0 以上で、NaN・負は作れない。0 は、少しでも力がかかれば壊れる。
	//   力:     つないだ点を保つ力（Distance は棒が引く・押す力）
	//   トルク: 向きを保つ力（Fixed / Hinge / Slider は固めた回転、Cone は円錐の端を押さえる力）。Point / Distance にはトルクがない
	//   モーター・範囲の端（Hinge / Slider）・摩擦の力は含めない。作った直後の数ステップは、姿勢のずれを直すために大きな力が出ることがある
	float32 BreakForce  = NMath::MaxFloat;
	float32 BreakTorque = NMath::MaxFloat;

	// つないだ 2 つの剛体どうしが衝突するか。既定は false（つなぎ目で押し合わない）。
	// 作る前から触れ合っていた組も、次のステップから触れ合わなくなる。関節を無効（SetEnabled）にしても変わらず、壊すと衝突に戻る
	bool bCollideConnected = false;

	// 作った直後に効かせるか
	bool bEnabled = true;

	//-------------------------------------------------------------------------
	// 種類ごとの作り方
	//-------------------------------------------------------------------------
	static FConstraintSettings Fixed( const FVector3& InAnchor )
	{
		FConstraintSettings Settings;
		Settings.Type   = EConstraintType::Fixed;
		Settings.Anchor = InAnchor;
		return Settings;
	}

	static FConstraintSettings Point( const FVector3& InAnchor )
	{
		FConstraintSettings Settings;
		Settings.Type   = EConstraintType::Point;
		Settings.Anchor = InAnchor;
		return Settings;
	}

	// 2 点の距離を InMinDistance..InMaxDistance に収める。負の値は「作った瞬間の距離」（両方負なら固定）
	static FConstraintSettings Distance( const FVector3& InAnchorA, const FVector3& InAnchorB, float32 InMinDistance = -1.0f, float32 InMaxDistance = -1.0f )
	{
		FConstraintSettings Settings;
		Settings.Type     = EConstraintType::Distance;
		Settings.Anchor   = InAnchorA;
		Settings.AnchorB  = InAnchorB;
		Settings.LimitMin = InMinDistance;
		Settings.LimitMax = InMaxDistance;
		return Settings;
	}

	// InAnchor を通る InAxis のまわりに回る。角度の範囲は InLimitMin..InLimitMax（ラジアン。既定は全周）
	static FConstraintSettings Hinge( const FVector3& InAnchor, const FVector3& InAxis, float32 InLimitMin = -NMath::MaxFloat, float32 InLimitMax = NMath::MaxFloat )
	{
		FConstraintSettings Settings;
		Settings.Type     = EConstraintType::Hinge;
		Settings.Anchor   = InAnchor;
		Settings.Axis     = InAxis;
		Settings.LimitMin = InLimitMin;
		Settings.LimitMax = InLimitMax;
		return Settings;
	}

	// InAnchor を通る InAxis に沿って動く。距離の範囲は InLimitMin..InLimitMax（m。既定は範囲なし）
	static FConstraintSettings Slider( const FVector3& InAnchor, const FVector3& InAxis, float32 InLimitMin = -NMath::MaxFloat, float32 InLimitMax = NMath::MaxFloat )
	{
		FConstraintSettings Settings;
		Settings.Type     = EConstraintType::Slider;
		Settings.Anchor   = InAnchor;
		Settings.Axis     = InAxis;
		Settings.LimitMin = InLimitMin;
		Settings.LimitMax = InLimitMax;
		return Settings;
	}

	// InAnchor でつなぎ、InTwistAxis の向きを半角 InHalfConeAngle（ラジアン）の円錐に収める
	static FConstraintSettings Cone( const FVector3& InAnchor, const FVector3& InTwistAxis, float32 InHalfConeAngle )
	{
		FConstraintSettings Settings;
		Settings.Type          = EConstraintType::Cone;
		Settings.Anchor        = InAnchor;
		Settings.Axis          = InTwistAxis;
		Settings.HalfConeAngle = InHalfConeAngle;
		return Settings;
	}
};

//=============================================================================
// FConstraintBreakInfo
//   関節が壊れた瞬間の力（N）とトルク（N·m）。物理のステップの時間で割った値。
//=============================================================================
struct FConstraintBreakInfo
{
	float32 Force  = 0.0f;
	float32 Torque = 0.0f;
};
