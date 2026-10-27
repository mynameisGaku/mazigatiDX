// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsConstraintTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

class FPhysicsConstraint;
class FRigidBody;

//=============================================================================
// IPhysicsConstraintListener
//   関節が壊れたときの通知を受け取る側。継承して、必要な関数だけ上書きする。
//   通知は、物理の Update の中で、固定ステップごとに、メインスレッドから呼ばれる。中で剛体や関節を作る・壊す、受け取り側を登録・解除してもよい。
//   同じステップで複数の関節が壊れたときは、作った順に 1 つずつ知らせる。通知の中で壊された剛体・関節についての通知は届けない。
//=============================================================================
class IPhysicsConstraintListener
{
public:
	virtual ~IPhysicsConstraintListener() = default;

	// 関節が壊れて外れた。BodyA / BodyB は壊れる前につないでいた剛体（世界に固定した関節では BodyB が nullptr）。
	// Constraint は外れた入れ物で、DestroyConstraint で片付けるまで残る
	virtual void OnConstraintBroken( FPhysicsConstraint* Constraint, FRigidBody* BodyA, FRigidBody* BodyB, const FConstraintBreakInfo& Info ) {}
};

//=============================================================================
// IPhysicsConstraints
//   2 つの剛体をつなぐ関節（拘束）を作る・壊す窓口。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースと
//   Library/Physics/PhysicsConstraint.h（作った関節を操作する FPhysicsConstraint。どちらも Jolt を含まない）だけを知ればよい。
//
//   IPhysicsConstraints* Joints = GetScene()->GetSubsystem<IPhysicsConstraints>();   // 物理を使うシーンにだけある（無ければ nullptr）
//   FPhysicsConstraint* Door = Joints->CreateConstraint( Frame, Leaf, FConstraintSettings::Hinge( HingePosition, FVector3::Up, 0.0f, NMath::HALF_PI ) );
//   Door->SetMotorVelocity( 1.0f, 50.0f );   // 開ける
//
//   - BodyA は必須。BodyB が nullptr なら、BodyA を世界（ワールドの座標）に固定する
//   - 2 つのうち少なくとも 1 つは動的（EBodyType::Dynamic）であること。同じ剛体どうし・別のワールドの剛体・
//     設定が不正（NaN・無限大・零ベクトルの軸・範囲の矛盾）のときは、ログを出して nullptr を返す
//   - 作った関節は CPhysicsSystem が持つ。DestroyConstraint か Shutdown まで、返したポインターは有効
//   - どちらかの剛体を DestroyBody / DestroyAllBodies すると、その剛体につないだ関節は外れる（FPhysicsConstraint::IsAttached が false）。
//     入れ物は残るので、DestroyConstraint で片付ける
//   - 壊れる関節（FConstraintSettings::BreakForce / BreakTorque）は、物理のステップの終わりに外れ、AddConstraintListener で登録した
//     IPhysicsConstraintListener に知らされる。壊れた関節も入れ物は残る（FPhysicsConstraint::IsBroken が true、IsAttached が false）
//   - 物理の Update と同時には呼ばない
//=============================================================================
class IPhysicsConstraints
{
public:
	virtual ~IPhysicsConstraints() = default;

	// 関節を作る。失敗は nullptr
	virtual FPhysicsConstraint* CreateConstraint( FRigidBody* BodyA, FRigidBody* BodyB, const FConstraintSettings& Settings ) = 0;

	// 関節を壊す。このワールドで作ったものでなければ何もしない。nullptr も無視する
	virtual void DestroyConstraint( FPhysicsConstraint* Constraint ) = 0;
	virtual void DestroyAllConstraints() = 0;

	// 持っている関節の数（外れて入れ物だけ残っているものも数える）
	virtual int32 GetConstraintCount() const = 0;

	// 関節が壊れたときの通知の受け取り側を登録する。nullptr は登録できず false。同じものを重ねて登録しても 1 つ。受け取り側の所有権は呼び出し側で、破棄する前に外す
	virtual bool AddConstraintListener( IPhysicsConstraintListener* Listener ) = 0;
	virtual void RemoveConstraintListener( IPhysicsConstraintListener* Listener ) = 0;
};
