// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Physics/IPhysicsContacts.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Core/Types/TypeDefinition.h"

class CPhysicsSystem;

//=============================================================================
// FContactListenerRegistry（Physics の内部用）
//   接触の通知を受け取る側の登録と、通知の配り方。CPhysicsSystem が 1 つ持つ。
//   物理エンジンのヘッダーを含まない（剛体はキー = BodyID::GetIndexAndSequenceNumber で受け取り、配るときに CPhysicsSystem から引く）。
//
//   - 通知の中で登録・解除されてもよい。配っている間に解除されたものには、以降の通知を届けない（片付けは配り終えてから）。
//     配っている間に登録されたものは、その配りが終わるまで届かない
//   - 剛体の組ごとの通知（Begin / End）は、受け取り側ごとに「その組で接触している部位の数」を持って決める。
//     登録の前から続いていた接触は数えないので、End だけが届くことはない
//   - 物理エンジンは、剛体が眠ると接触の終わりを知らせる（起きて、まだ触れていれば、また始まりを知らせる）。
//     組の通知では、どちらも眠っている（または動かない）ときの終わりは、End にせず、途切れただけとして覚えておく。
//     起きたときにまだ触れていれば、そのまま続きにする。起きて何回か配っても触れていなければ、そこで End にする
//   - 通知の中で剛体が破棄されたら、その通知はそこで打ち切る（破棄された剛体のポインターは渡さない）
//=============================================================================
class FContactListenerRegistry
{
public:
	enum class EContactSignal : uint8
	{
		Added,
		Persisted,
		Removed,
	};

	// 受け取り側を登録する。登録済みなら、絞り込みだけを差し替える（組ごとの記録は保つ）
	//   bHasBody: 特定の剛体が絡む通知だけにするか。そのとき BodyKey がその剛体
	void Add( IPhysicsContactListener& Listener, uint32 LayerMask, bool bPersisted, bool bHasBody, uint32 BodyKey );

	// 登録を外す。配っている間は、片付けを配り終えるまで後回しにする
	void Remove( IPhysicsContactListener& Listener );

	// 登録されている数（配っている間に解除されたものは数えない）
	int32 Num() const { return LiveCount; }
	bool IsEmpty() const { return LiveCount == 0; }

	// 受け取り側が数えている、接触中の剛体の組の数（受け取り側ごとの合計。確認・デバッグ用）
	int32 NumPairs() const;

	// 剛体の組の記録を、すべて捨てる（剛体をまとめて壊したとき）
	void ResetPairs();

	// この剛体の組の記録を、すべての受け取り側から捨てる（剛体が破棄されて、終わりの通知が届かないとき）
	void ForgetPair( uint32 KeyA, uint32 KeyB );

	//-------------------------------------------------------------------------
	// 配る（CContactEventQueue::Dispatch から。必ず BeginDispatch と EndDispatch で挟む）
	//-------------------------------------------------------------------------
	void BeginDispatch();
	void EndDispatch();

	// 接触点ごとの通知
	void DeliverContact( const CPhysicsSystem& System, EContactSignal Signal, uint32 KeyA, uint32 KeyB, const FContactInfo& Info );

	// 眠って途切れている組を調べ、起きても触れていなければ End にする（接触の通知を配り終えた後に呼ぶ）。剛体が壊れていれば、通知せずに捨てる
	void DeliverWakeCheck( const CPhysicsSystem& System );

	// 剛体の組ごとの通知。始まりは、同じ配りの中の終わりより先に配る側が呼ぶ（部位が入れ替わっても、途切れさせないため）
	void DeliverBodyBegin( const CPhysicsSystem& System, uint32 KeyA, uint32 KeyB, const FContactInfo& Info );
	void DeliverBodyEnd( const CPhysicsSystem& System, uint32 KeyA, uint32 KeyB );

private:
	// 起きてから、触れていなければ End にするまでに待つ配りの回数（起きた直前のステップでは、まだ接触が見つかっていないことがあるため）
	static constexpr int32 WakeGraceSteps = 2;

	struct FPairState
	{
		int32 Parts      = 0;  // 接触している部位の数。0 は、眠って接触が途切れている状態（End は出していない）
		int32 AwakeSteps = 0;  // 眠って途切れたまま、どちらかが起きている配りの回数
	};

	struct FEntry
	{
		IPhysicsContactListener* Listener = nullptr;  // nullptr は、配っている間に解除されたもの
		uint32                   LayerMask = 0;
		bool                     bPersisted = true;
		bool                     bHasBody = false;
		uint32                   BodyKey = 0;
		TMap<uint64, FPairState> Pairs;                // key: NContactRules::MakePairKey
		bool                     bMaybeSuspended = false;  // 眠って途切れている組があるかもしれない（調べる手間を省くための目印）
	};

	FEntry* FindLive( const IPhysicsContactListener& Listener ) const;

	TArray<TUniquePtr<FEntry>> Entries;
	int32 LiveCount     = 0;
	int32 DispatchDepth = 0;
	int32 DispatchLimit = 0;   // 配っている間に増えた分には届けない
	bool  bNeedsCompact = false;
};
