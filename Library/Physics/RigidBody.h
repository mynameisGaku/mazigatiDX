#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Physics/PhysicsTypes.h"

namespace JPH
{
	class BodyInterface;
}

//=============================================================================
// FRigidBodySettings
//   剛体生成時のパラメータ
//=============================================================================
struct FRigidBodySettings
{
	float32 Friction       = 0.2f;
	float32 Restitution    = 0.0f;   // 反発係数 0..1
	float32 LinearDamping  = 0.05f;
	float32 AngularDamping = 0.05f;
	float32 GravityFactor  = 1.0f;
	float32 Mass           = 0.0f;   // 0 なら形状と密度から自動計算（Dynamic のみ有効）
	bool    bIsSensor      = false;  // 衝突応答なしでコールバックのみ（トリガー）
	bool    bAllowSleeping = true;
	bool    bContinuousCollision = false;  // 高速移動体のすり抜け防止 (CCD)
	bool    bActivate      = true;   // 生成直後にアクティブにするか
};

//=============================================================================
// FRigidBody
//   Jolt の Body を指すハンドル。生成・破棄は生成元の CPhysicsSystem が行う。
//   CPhysicsSystem::DestroyBody() 後や、ワールドの Shutdown 後はポインタを使わないこと。
//=============================================================================
class FRigidBody
{
public:
	FRigidBody( JPH::BodyInterface& InBodyInterface, PhysBodyID InBodyID, EBodyType InType, const FVector3& InScale );

	FORCEINLINE PhysBodyID GetBodyID() const { return BodyID; }
	FORCEINLINE EBodyType  GetBodyType() const { return BodyType; }

	//-------------------------------------------------------------------------
	// 姿勢
	//-------------------------------------------------------------------------
	// Scale は生成時の値（形状に焼き込まれており、後から変更不可）
	FTransform GetTransform() const;
	void SetTransform( const FTransform& Transform, bool bActivate = true );

	FVector3 GetPosition() const;
	void SetPosition( const FVector3& Position, bool bActivate = true );

	FQuaternion GetRotation() const;
	void SetRotation( const FQuaternion& Rotation, bool bActivate = true );

	// Kinematic 用: DeltaTime 秒後に目標姿勢へ到達する速度を設定する
	void MoveKinematic( const FVector3& TargetPosition, const FQuaternion& TargetRotation, float32 DeltaTime );

	//-------------------------------------------------------------------------
	// 速度
	//-------------------------------------------------------------------------
	FVector3 GetLinearVelocity() const;
	void SetLinearVelocity( const FVector3& Velocity );

	FVector3 GetAngularVelocity() const;
	void SetAngularVelocity( const FVector3& Velocity );

	//-------------------------------------------------------------------------
	// 力（次の物理ステップで適用される）
	//-------------------------------------------------------------------------
	void AddForce( const FVector3& Force );
	void AddForceAtPosition( const FVector3& Force, const FVector3& WorldPosition );
	void AddTorque( const FVector3& Torque );

	// 撃力（即座に速度が変わる）
	void AddImpulse( const FVector3& Impulse );
	void AddImpulseAtPosition( const FVector3& Impulse, const FVector3& WorldPosition );
	void AddAngularImpulse( const FVector3& AngularImpulse );

	//-------------------------------------------------------------------------
	// 状態・物性
	//-------------------------------------------------------------------------
	void Activate();
	void Deactivate();
	bool IsActive() const;

	void SetFriction( float32 Friction );
	void SetRestitution( float32 Restitution );
	void SetGravityFactor( float32 GravityFactor );

	//-------------------------------------------------------------------------
	// ユーザーデータ（ゲームオブジェクトとの紐付け用）
	//-------------------------------------------------------------------------
	FORCEINLINE void* GetUserData() const { return UserData; }
	FORCEINLINE void SetUserData( void* InUserData ) { UserData = InUserData; }

	template<typename T>
	FORCEINLINE T* GetUserDataAs() const { return static_cast<T*>( UserData ); }

private:
	// 所属するワールドの BodyInterface
	FORCEINLINE JPH::BodyInterface& GetBodyInterface() const { return *BodyInterface; }

	JPH::BodyInterface* BodyInterface;
	PhysBodyID BodyID;
	EBodyType  BodyType;
	FVector3   Scale;
	void*      UserData = nullptr;
};
