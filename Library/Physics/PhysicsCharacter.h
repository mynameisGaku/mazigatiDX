// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsCharacterTypes.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

namespace JPH
{
	class CharacterVirtual;
}

class CPhysicsSystem;
class FRigidBody;

//=============================================================================
// FPhysicsCharacter
//   操作するキャラクター 1 人。作るのは ICharacterControllers::CreateCharacter（CPhysicsSystem）で、壊すのも同じ窓口。
//   このヘッダーは物理エンジン（Jolt）の型を含まない。
//
//   FPhysicsCharacter* Player = Characters->CreateCharacter( Spawn, FCharacterSettings() );
//   Player->SetMoveVelocity( FVector3( Input.x, 0.0f, Input.y ) * 4.0f );   // 毎フレーム、歩きたい向きと速さ（m/s）
//   if ( JumpPressed ) Player->Jump( 5.0f );
//   FVector3 Feet = Player->GetPosition();                                  // 足元の中心。描画はこの位置に置く
//
//   - 動かすのは物理の Update（固定ステップごと）。重力・ジャンプ・坂（登れない角度）・段差・床への吸いつき・
//     動く床に乗る・動く剛体を押す・他のキャラクターの剛体とぶつかる、までを含む
//   - 歩く向きは SetMoveVelocity の水平成分だけで決まる（Y 成分は無視）。上下の動きは重力とジャンプで決まる
//   - DestroyCharacter か、ワールドの Shutdown まで有効。ワールドの DestroyAllBodies で外れる（IsAttached が false。
//     以後の設定はすべて false を返して何もしない。GetPosition は外れる直前の位置）が、入れ物は残る
//   - 設定の変更は、物理の Update と同時には呼ばない
//   - 設定が不正（NaN・範囲の矛盾）なら、何も変えずに false を返す
//=============================================================================
class FPhysicsCharacter
{
public:
	// 物理のワールドにつながっているか。DestroyAllBodies で false になる
	FORCEINLINE bool IsAttached() const { return Character != nullptr; }

	// 作ったときの設定（変更した分は反映されている）
	FORCEINLINE const FCharacterSettings& GetSettings() const { return Settings; }

	// キャラクターの形をした剛体（運動学）。bCreateBody が false か、外れていれば nullptr。
	// 動かすのは SetPosition で、この剛体を直接動かさない。DestroyBody では壊せない
	FORCEINLINE FRigidBody* GetBody() const { return Body; }

	//-------------------------------------------------------------------------
	// 位置と動き
	//-------------------------------------------------------------------------
	// 足元の中心（ワールド）。外れていれば、外れる直前の位置
	FVector3 GetPosition() const;

	// 瞬間移動する（足元の中心）。他の物体にめり込む位置は避ける。NaN・無限大は false
	bool SetPosition( const FVector3& Position );

	// 歩きたい水平の速さ（ワールド、m/s）。Y 成分は無視する。次に変えるまで続く（止まるなら 0 を渡す）。NaN・無限大は false
	bool SetMoveVelocity( const FVector3& Velocity );
	FORCEINLINE FVector3 GetMoveVelocity() const { return MoveVelocity; }

	// 実際の速度（動く床の速度・重力・ジャンプを含む、直前のステップのもの）。外れていれば 0
	FVector3 GetVelocity() const;

	// 次のステップで、上向きの初速 Speed（m/s、0 より大きい）で跳ぶ。地面にいなければ false（何も起きない）
	bool Jump( float32 Speed );

	//-------------------------------------------------------------------------
	// 足元の状態
	//-------------------------------------------------------------------------
	ECharacterGroundState GetGroundState() const;

	// 地面の上にいて、自由に歩けるか（OnGround）。急な坂の上（OnSteepGround）は含まない
	FORCEINLINE bool IsOnGround() const { return GetGroundState() == ECharacterGroundState::OnGround; }

	// 足元の面の法線（ワールド）。何にも触れていなければ 0
	FVector3 GetGroundNormal() const;

	// 足元の面の速度（動く床の速さ。ワールド）。何にも触れていなければ 0
	FVector3 GetGroundVelocity() const;

	// 足元の剛体。何にも触れていないか、CreateBody で作った剛体でなければ nullptr
	FRigidBody* GetGroundBody() const;

	//-------------------------------------------------------------------------
	// 体と動き方の設定（後から変える）
	//-------------------------------------------------------------------------
	// 高さを変える（かがむ・立つ）。変えた先が他の物体にめり込むときは false で何も変えない。Height は Radius の 2 倍より大きい
	bool SetHeight( float32 Height );

	bool SetMaxSlopeAngle( float32 Angle );          // 0 より大きく PI/2 より小さい
	bool SetStepHeight( float32 Height );            // 0 以上
	bool SetStickToFloorDistance( float32 Distance );  // 0 以上
	bool SetMass( float32 Mass );                    // 0 以上
	bool SetMaxPushForce( float32 Force );           // 0 以上
	bool SetGravityFactor( float32 Factor );         // 有限

	// 衝突レイヤー。剛体があれば、その剛体のレイヤーも変わる（FRigidBody::SetCollisionLayer と同じ）。範囲外は false
	bool SetCollisionLayer( uint32 Layer );

	//-------------------------------------------------------------------------
	// ユーザーデータ（ゲームオブジェクトとの紐付け用）
	//-------------------------------------------------------------------------
	FORCEINLINE void* GetUserData() const { return UserData; }
	FORCEINLINE void SetUserData( void* InUserData ) { UserData = InUserData; }

	template<typename T>
	FORCEINLINE T* GetUserDataAs() const { return static_cast<T*>( UserData ); }

private:
	friend class CPhysicsSystem;

	FPhysicsCharacter( CPhysicsSystem& InOwner, const FCharacterSettings& InSettings, JPH::CharacterVirtual* InCharacter, FRigidBody* InBody );

	CPhysicsSystem*         Owner;
	FCharacterSettings      Settings;
	JPH::CharacterVirtual*  Character;  // 外れると nullptr。参照を 1 つ持つので、外すのは CPhysicsSystem が行う
	FRigidBody*             Body;
	FVector3                MoveVelocity = FVector3::Zero;  // SetMoveVelocity の値（Y は 0）
	bool                    bJumpRequested = false;         // 次のステップで跳ぶか
	float32                 JumpSpeed      = 0.0f;
	FVector3                LastPosition   = FVector3::Zero;  // 外れる直前の位置
	void*                   UserData       = nullptr;
};
