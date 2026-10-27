// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsConstraintTypes.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

namespace JPH
{
	class TwoBodyConstraint;
}

class CPhysicsSystem;
class FRigidBody;

//=============================================================================
// FPhysicsConstraint
//   2 つの剛体をつなぐ関節 1 つ。作るのは IPhysicsConstraints::CreateConstraint（CPhysicsSystem）で、壊すのも同じ窓口。
//   このヘッダーは物理エンジン（Jolt）の型を含まない。
//
//   FPhysicsConstraint* Door = Joints->CreateConstraint( Frame, Leaf, FConstraintSettings::Hinge( HingePosition, FVector3::Up, 0.0f, NMath::HALF_PI ) );
//   Door->SetMotorVelocity( 1.0f, 50.0f );     // 毎秒 1 ラジアンで開く（最大 50 N·m）
//   float32 Angle = Door->GetCurrentValue();   // 今の角度
//
//   - DestroyConstraint か、ワールドの Shutdown まで有効。どちらかの剛体を破棄すると関節は外れる（IsAttached が false。
//     以後の設定はすべて false を返して何もしない）が、入れ物は残る
//   - 設定を変えると、つないだ剛体を起こす（眠ったままでは、変えた効果が出ないため）
//   - 設定の変更は、物理の Update と同時には呼ばない
//   - 設定が不正（NaN・範囲の矛盾・その種類では使えない設定）なら、何も変えずに false を返す
//=============================================================================
class FPhysicsConstraint
{
public:
	FORCEINLINE EConstraintType GetType() const { return Type; }

	// つないだ剛体。外れたら nullptr。B は、世界に固定した関節でも nullptr
	FORCEINLINE FRigidBody* GetBodyA() const { return BodyA; }
	FORCEINLINE FRigidBody* GetBodyB() const { return BodyB; }

	// 世界に固定した関節か（B が nullptr で作った）。外れた後も、作ったときのまま
	FORCEINLINE bool IsAnchoredToWorld() const { return bAnchoredToWorld; }

	// 剛体につながっているか。つないだ剛体を破棄すると false になる
	FORCEINLINE bool IsAttached() const { return Joint != nullptr; }

	// 効かせるか。外れていれば常に false
	bool IsEnabled() const;
	bool SetEnabled( bool bEnabled );

	// つないだ 2 つの剛体どうしを衝突させるか（作ったときの FConstraintSettings::bCollideConnected）
	FORCEINLINE bool GetCollideConnected() const { return bCollideConnected; }

	//-------------------------------------------------------------------------
	// 今の状態
	//-------------------------------------------------------------------------
	// Hinge: 作った瞬間を 0 とした角度（ラジアン）。Slider: 動いた距離（m）。Distance: 2 点の距離（m）。それ以外と、外れていれば 0
	float32 GetCurrentValue() const;

	//-------------------------------------------------------------------------
	// 壊れる力（FConstraintSettings::BreakForce / BreakTorque）
	//-------------------------------------------------------------------------
	// 壊れて外れたか。壊れた関節は、入れ物だけ残る（IsAttached が false）。つないだ剛体を破棄して外れただけのときは false
	FORCEINLINE bool IsBroken() const { return bBroken; }

	// 壊れる限界。壊れない設定は NMath::MaxFloat
	FORCEINLINE float32 GetBreakForce() const { return BreakForce; }
	FORCEINLINE float32 GetBreakTorque() const { return BreakTorque; }

	// 壊れる限界を変える。0 以上（無限大は壊れない）。NaN・負、外れた関節は、何も変えずに false
	bool SetBreakLimits( float32 Force, float32 Torque );

	// 直前の物理のステップで、点や向きを保つためにかけた力（N）とトルク（N·m）。
	// 外れた関節、効かせていない関節、つないだ剛体がどちらも眠っている関節は 0
	FORCEINLINE float32 GetAppliedForce() const { return AppliedForce; }
	FORCEINLINE float32 GetAppliedTorque() const { return AppliedTorque; }

	//-------------------------------------------------------------------------
	// 範囲・ばね・摩擦・円錐
	//-------------------------------------------------------------------------
	// 動ける範囲を変える（FConstraintSettings::LimitMin / LimitMax と同じ意味）。Hinge / Slider / Distance だけ。
	// Distance は 0 以上の距離を指定する（負の「作った瞬間の距離」は、作るときだけ使える）
	bool SetLimits( float32 Min, float32 Max );

	// 範囲の端のばね。Hinge / Slider / Distance だけ。Frequency は 0 以上、Damping は 0 以上
	bool SetLimitSpring( const FConstraintSpring& Spring );

	// モーターを使っていないときの摩擦の上限（Hinge は N·m、Slider は N）。0 以上。Hinge / Slider だけ
	bool SetMaxFriction( float32 MaxFriction );

	// 円錐の半角（0..PI、ラジアン）。Cone だけ
	bool SetHalfConeAngle( float32 HalfConeAngle );

	//-------------------------------------------------------------------------
	// モーター（Hinge と Slider だけ）
	//   Hinge は角度（ラジアン）、Slider は距離（m）。MaxForce は出せる力の上限で、Hinge は N·m、Slider は N。0 以上
	//   （無限大は上限なし）。目標の位置は、範囲の中に切り詰められる
	//-------------------------------------------------------------------------
	// 目標の速さ（Hinge は rad/s、Slider は m/s）で動かす
	bool SetMotorVelocity( float32 TargetVelocity, float32 MaxForce );

	// 目標の位置へ、ばねで引き寄せる。Spring.Frequency が 0 だと、MaxForce の許す限り一気に目標へ動かす
	bool SetMotorPosition( float32 TargetPosition, const FConstraintSpring& Spring, float32 MaxForce );

	// モーターを止める
	bool DisableMotor();

	EConstraintMotorMode GetMotorMode() const;

	//-------------------------------------------------------------------------
	// ユーザーデータ（ゲームオブジェクトとの紐付け用）
	//-------------------------------------------------------------------------
	FORCEINLINE void* GetUserData() const { return UserData; }
	FORCEINLINE void SetUserData( void* InUserData ) { UserData = InUserData; }

	template<typename T>
	FORCEINLINE T* GetUserDataAs() const { return static_cast<T*>( UserData ); }

private:
	friend class CPhysicsSystem;

	FPhysicsConstraint( EConstraintType InType, FRigidBody* InBodyA, FRigidBody* InBodyB, JPH::TwoBodyConstraint* InJoint, bool bInCollideConnected );

	// つないだ剛体を起こす
	void WakeBodies() const;

	EConstraintType         Type;
	FRigidBody*             BodyA;
	FRigidBody*             BodyB;
	bool                    bAnchoredToWorld;
	bool                    bCollideConnected;
	bool                    bBroken = false;
	float32                 BreakForce  = NMath::MaxFloat;
	float32                 BreakTorque = NMath::MaxFloat;
	float32                 AppliedForce  = 0.0f;
	float32                 AppliedTorque = 0.0f;
	JPH::TwoBodyConstraint* Joint;  // 外れると nullptr。参照を 1 つ持つので、外すのは CPhysicsSystem がまとめて行う
	void*                   UserData = nullptr;
};
