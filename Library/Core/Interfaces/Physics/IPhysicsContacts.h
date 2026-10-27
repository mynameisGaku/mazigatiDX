// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsContactTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IPhysicsContactListener
//   接触の通知を受け取る側。継承して、必要な関数だけ上書きする（どれも何もしない既定がある）。
//
//   class CGoal : public IPhysicsContactListener
//   {
//       void OnBodyContactBegin( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) override { ... }
//   };
//
//   通知は、物理の Update の中で、固定ステップごとに、メインスレッドから呼ばれる。
//   そのため、中で剛体の生成・破棄・力の追加をしてもよい（破棄した剛体は、以降の通知に出ない）。
//   BodyA / BodyB の順は物理エンジンが決める。FContactInfo::Normal は A から B へ向かう。
//=============================================================================
class IPhysicsContactListener
{
public:
	virtual ~IPhysicsContactListener() = default;

	//-------------------------------------------------------------------------
	// 剛体の組ごとの通知（OnCollisionEnter / Exit に近い。たいていはこちらで足りる）
	//   複合形状やメッシュは、1 組の剛体でも接触する部位が複数ある。ここでは「その組の接触が 1 つもない状態から、1 つ以上ある状態になった」ときに
	//   Begin が 1 回、「すべての接触がなくなった」ときに End が 1 回だけ届く。センサー（トリガー）との接触も同じ。
	//   - Begin と End は、登録した後に始まった接触について、必ず対になって届く（登録する前から続いていた接触は、どちらも届かない。
	//     ただし、登録したとき眠っていた剛体の接触は、物理エンジンが一度終わらせているので、起きて触れたときに新しい接触として Begin が届く）
	//   - 剛体を破棄したとき、その剛体の組に End は届かない
	//   - 剛体が眠ると、物理エンジンは接触の終わり（OnContactRemoved）を知らせ、起きてまだ触れていれば始まり（OnContactAdded）をまた知らせる。
	//     組の通知では、どちらも動いていない（眠っている）ときの終わりは End にしない。眠っている間は触れたままとして扱い、
	//     起きたときにまだ触れていれば続きになり（Begin は来ない）、起きても触れていなければ、起きてから 2 ステップほど後に End が届く
	//   - Info は、その組で最初に見つかった接触
	//-------------------------------------------------------------------------
	virtual void OnBodyContactBegin( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) {}
	virtual void OnBodyContactEnd( FRigidBody* BodyA, FRigidBody* BodyB ) {}

	//-------------------------------------------------------------------------
	// 接触点ごとの通知（物理エンジンが知らせたまま。複合形状やメッシュでは、同じ組で何度も届く）
	//-------------------------------------------------------------------------
	// 接触の始まり
	virtual void OnContactAdded( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) {}

	// 接触が続いている（接触している間、毎ステップ。FPhysicsContactFilter::bPersisted が false なら届かない）
	virtual void OnContactPersisted( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) {}

	// 接触の終わり（どちらかが既に破棄されているときは届かない）
	virtual void OnContactRemoved( FRigidBody* BodyA, FRigidBody* BodyB ) {}
};

//=============================================================================
// IPhysicsContacts
//   物理ワールドの接触（ぶつかった・離れた・センサーに入った）の通知を登録する窓口。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースだけを知ればよく、物理エンジンの型を include しない。
//
//   IPhysicsContacts* Contacts = GetScene()->GetSubsystem<IPhysicsContacts>();   // 物理を使うシーンにだけある（無ければ nullptr）
//   FPhysicsContactFilter Filter;
//   Filter.Body = Player;
//   Contacts->AddContactListener( &MyListener, Filter );
//   ...
//   Contacts->RemoveContactListener( &MyListener );   // MyListener を破棄する前に必ず外す
//
//   - 通知を受け取る側（IPhysicsContactListener）の所有権は呼び出し側。破棄する前に RemoveContactListener で外す
//   - 登録が 1 つもないとき、接触の通知は溜めない（負荷がない）
//   - 同じ受け取り側をもう一度登録すると、絞り込みだけが差し替わる（組ごとの記録は保たれる）
//   - 通知の中で登録・解除してもよい。通知の中で解除したものには、以降の通知は届かない。通知の中で登録したものは、次のステップから届く
//   - 物理の Update と同時には呼ばない（呼ぶのは、同じスレッドの Update の合間か、通知の中）
//=============================================================================
class IPhysicsContacts
{
public:
	virtual ~IPhysicsContacts() = default;

	// 通知の受け取り側を登録する。nullptr、または Filter.Body がこのワールドで作った剛体でないときは、何もせず false
	virtual bool AddContactListener( IPhysicsContactListener* Listener, const FPhysicsContactFilter& Filter = FPhysicsContactFilter() ) = 0;

	// 登録を外す。登録されていなければ何もしない
	virtual void RemoveContactListener( IPhysicsContactListener* Listener ) = 0;

	// 登録されている受け取り側の数
	virtual int32 GetContactListenerCount() const = 0;
};
