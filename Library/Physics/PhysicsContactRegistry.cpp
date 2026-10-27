#include "PhysicsContactRegistry.h"

#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Physics/PhysicsContactRules.h"
#include "Library/Physics/PhysicsSystem.h"
#include "Library/Physics/RigidBody.h"

FContactListenerRegistry::FEntry* FContactListenerRegistry::FindLive( const IPhysicsContactListener& Listener ) const
{
	for ( const TUniquePtr<FEntry>& Entry : Entries )
	{
		if ( Entry->Listener == &Listener ) return Entry.Get();
	}
	return nullptr;
}

void FContactListenerRegistry::Add( IPhysicsContactListener& Listener, uint32 LayerMask, bool bPersisted, bool bHasBody, uint32 BodyKey )
{
	FEntry* Entry = FindLive( Listener );
	if ( Entry == nullptr )
	{
		TUniquePtr<FEntry> NewEntry = MakeUnique<FEntry>();
		NewEntry->Listener = &Listener;
		Entry = NewEntry.Get();
		Entries.Add( MoveTemp( NewEntry ) );
		++LiveCount;
	}
	Entry->LayerMask  = LayerMask;
	Entry->bPersisted = bPersisted;
	Entry->bHasBody   = bHasBody;
	Entry->BodyKey    = bHasBody ? BodyKey : 0;
}

void FContactListenerRegistry::Remove( IPhysicsContactListener& Listener )
{
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		FEntry& Entry = *Entries[Index];
		if ( Entry.Listener != &Listener ) continue;

		--LiveCount;
		if ( DispatchDepth > 0 )
		{
			// 配っている最中は、配列の並びを変えない（印を付けて、配り終えてから片付ける）
			Entry.Listener = nullptr;
			Entry.Pairs.Reset();
			bNeedsCompact = true;
		}
		else
		{
			Entries.RemoveAt( Index );
		}
		return;
	}
}

int32 FContactListenerRegistry::NumPairs() const
{
	int32 Total = 0;
	for ( const TUniquePtr<FEntry>& Entry : Entries ) Total += Entry->Pairs.Num();
	return Total;
}

void FContactListenerRegistry::ResetPairs()
{
	for ( TUniquePtr<FEntry>& Entry : Entries ) Entry->Pairs.Reset();
}

void FContactListenerRegistry::ForgetPair( uint32 KeyA, uint32 KeyB )
{
	const uint64 PairKey = NContactRules::MakePairKey( KeyA, KeyB );
	for ( TUniquePtr<FEntry>& Entry : Entries ) Entry->Pairs.Remove( PairKey );
}

void FContactListenerRegistry::BeginDispatch()
{
	if ( DispatchDepth++ == 0 ) DispatchLimit = Entries.Num();
}

void FContactListenerRegistry::EndDispatch()
{
	if ( --DispatchDepth > 0 ) return;

	DispatchLimit = 0;
	if ( bNeedsCompact )
	{
		Entries.RemoveAll( []( const TUniquePtr<FEntry>& Entry ) { return Entry->Listener == nullptr; } );
		bNeedsCompact = false;
	}
}

void FContactListenerRegistry::DeliverContact( const CPhysicsSystem& System, EContactSignal Signal, uint32 KeyA, uint32 KeyB, const FContactInfo& Info )
{
	FRigidBody* BodyA = nullptr;
	FRigidBody* BodyB = nullptr;
	bool bStale = true;  // 通知の中で剛体が破棄されうるので、通知を呼んだ後は引き直す

	for ( int32 Index = 0; Index < DispatchLimit; ++Index )
	{
		FEntry& Entry = *Entries[Index];
		if ( Entry.Listener == nullptr ) continue;
		if ( Signal == EContactSignal::Persisted && !Entry.bPersisted ) continue;

		if ( bStale )
		{
			BodyA = System.FindBody( PhysBodyID( KeyA ) );
			BodyB = System.FindBody( PhysBodyID( KeyB ) );
			if ( BodyA == nullptr || BodyB == nullptr ) return;
			bStale = false;
		}
		if ( !NContactRules::Accepts( Entry.bHasBody, Entry.BodyKey, Entry.LayerMask, KeyA, BodyA->GetCollisionLayer(), KeyB, BodyB->GetCollisionLayer() ) ) continue;

		switch ( Signal )
		{
		case EContactSignal::Added:     Entry.Listener->OnContactAdded( BodyA, BodyB, Info );     break;
		case EContactSignal::Persisted: Entry.Listener->OnContactPersisted( BodyA, BodyB, Info ); break;
		case EContactSignal::Removed:   Entry.Listener->OnContactRemoved( BodyA, BodyB );         break;
		}
		bStale = true;
	}
}

void FContactListenerRegistry::DeliverBodyBegin( const CPhysicsSystem& System, uint32 KeyA, uint32 KeyB, const FContactInfo& Info )
{
	const uint64 PairKey = NContactRules::MakePairKey( KeyA, KeyB );
	FRigidBody* BodyA = nullptr;
	FRigidBody* BodyB = nullptr;
	bool bStale = true;

	for ( int32 Index = 0; Index < DispatchLimit; ++Index )
	{
		FEntry& Entry = *Entries[Index];
		if ( Entry.Listener == nullptr ) continue;

		if ( bStale )
		{
			BodyA = System.FindBody( PhysBodyID( KeyA ) );
			BodyB = System.FindBody( PhysBodyID( KeyB ) );
			if ( BodyA == nullptr || BodyB == nullptr )
			{
				ForgetPair( KeyA, KeyB );  // 通知の中で破棄された。終わりの通知は届かないので、記録も残さない
				return;
			}
			bStale = false;
		}

		// すでに数えている組は、絞り込みが変わっていても数え続ける（Begin と End を対にするため）。
		// 眠って途切れていた組（部位が 0）なら、起きてまた触れたということなので、Begin は出さずに続きにする
		FPairState* State = Entry.Pairs.Find( PairKey );
		if ( State != nullptr )
		{
			++State->Parts;
			State->AwakeSteps = 0;
			continue;
		}
		if ( !NContactRules::Accepts( Entry.bHasBody, Entry.BodyKey, Entry.LayerMask, KeyA, BodyA->GetCollisionLayer(), KeyB, BodyB->GetCollisionLayer() ) ) continue;

		FPairState NewState;
		NewState.Parts     = 1;
		NewState.FirstKey  = KeyA;
		NewState.SecondKey = KeyB;
		Entry.Pairs.Add( PairKey, NewState );
		Entry.Listener->OnBodyContactBegin( BodyA, BodyB, Info );
		bStale = true;
	}
}

void FContactListenerRegistry::DeliverBodyEnd( const CPhysicsSystem& System, uint32 KeyA, uint32 KeyB )
{
	const uint64 PairKey = NContactRules::MakePairKey( KeyA, KeyB );
	FRigidBody* BodyA = nullptr;
	FRigidBody* BodyB = nullptr;
	bool bStale = true;

	for ( int32 Index = 0; Index < DispatchLimit; ++Index )
	{
		FEntry& Entry = *Entries[Index];
		if ( Entry.Listener == nullptr ) continue;

		FPairState* State = Entry.Pairs.Find( PairKey );
		if ( State == nullptr || State->Parts <= 0 ) continue;  // 登録の前から続いていた接触、破棄で記録を捨てた組、眠って途切れている組
		if ( --State->Parts > 0 ) continue;

		if ( bStale )
		{
			BodyA = System.FindBody( PhysBodyID( KeyA ) );
			BodyB = System.FindBody( PhysBodyID( KeyB ) );
			if ( BodyA == nullptr || BodyB == nullptr )
			{
				ForgetPair( KeyA, KeyB );
				return;
			}
			bStale = false;
		}

		// 剛体が眠ると、物理エンジンは接触の終わりを知らせる。どちらも動いていないなら、離れたのではなく眠っただけなので、End にしない
		if ( !BodyA->IsActive() && !BodyB->IsActive() )
		{
			State->AwakeSteps = 0;
			Entry.bMaybeSuspended = true;
			continue;
		}

		Entry.Pairs.Remove( PairKey );
		Entry.Listener->OnBodyContactEnd( BodyA, BodyB );
		bStale = true;
	}
}

void FContactListenerRegistry::DeliverWakeCheck( const CPhysicsSystem& System )
{
	for ( int32 Index = 0; Index < DispatchLimit; ++Index )
	{
		FEntry& Entry = *Entries[Index];
		if ( Entry.Listener == nullptr || !Entry.bMaybeSuspended ) continue;

		// 通知の中で組の表が変わりうるので、先に集めてから配る
		TArray<uint64> Gone;
		TArray<uint64> Ready;
		bool bStillSuspended = false;
		for ( auto& Pair : Entry.Pairs )
		{
			if ( Pair.Value.Parts > 0 ) continue;

			const FRigidBody* BodyA = System.FindBody( PhysBodyID( Pair.Value.FirstKey ) );
			const FRigidBody* BodyB = System.FindBody( PhysBodyID( Pair.Value.SecondKey ) );
			if ( BodyA == nullptr || BodyB == nullptr )
			{
				Gone.Add( Pair.Key );
				continue;
			}
			if ( !BodyA->IsActive() && !BodyB->IsActive() )
			{
				Pair.Value.AwakeSteps = 0;
				bStillSuspended = true;
				continue;
			}
			if ( ++Pair.Value.AwakeSteps >= WakeGraceSteps ) Ready.Add( Pair.Key );
			else bStillSuspended = true;
		}
		Entry.bMaybeSuspended = bStillSuspended;

		for ( const uint64 Key : Gone ) Entry.Pairs.Remove( Key );
		for ( const uint64 Key : Ready )
		{
			const FPairState* State = Entry.Pairs.Find( Key );
			if ( Entry.Listener == nullptr || State == nullptr || State->Parts > 0 ) continue;  // 通知の中で、解除・破棄・接触のやり直しがあった

			const uint32 FirstKey  = State->FirstKey;
			const uint32 SecondKey = State->SecondKey;
			Entry.Pairs.Remove( Key );
			FRigidBody* BodyA = System.FindBody( PhysBodyID( FirstKey ) );
			FRigidBody* BodyB = System.FindBody( PhysBodyID( SecondKey ) );
			if ( BodyA == nullptr || BodyB == nullptr ) continue;
			Entry.Listener->OnBodyContactEnd( BodyA, BodyB );
		}
	}
}
