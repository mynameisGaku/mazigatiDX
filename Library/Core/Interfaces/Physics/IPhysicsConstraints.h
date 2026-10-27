// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsConstraintTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

class FPhysicsConstraint;
class FRigidBody;

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
};
