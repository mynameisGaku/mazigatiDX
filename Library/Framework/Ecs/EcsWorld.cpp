#include "EcsWorld.h"

#include <intrin.h>

#include "Library/Framework/Object/GameObject.h"

int32 FEcsWorld::CreateEntity( CGameObject* Object, uint64 ObjectId, bool bTickEnabled, bool bVisible )
{
	int32 Entity;
	if ( !FreeSlots.IsEmpty() )
	{
		Entity = FreeSlots.Last();
		FreeSlots.Pop();
	}
	else
	{
		Entity = Records.Num();
		Records.Emplace();
	}
	FEntityRecord& Record = Records[Entity];
	Record.Object = Object;
	Record.TransformNode = nullptr;
	Record.ObjectId = ObjectId;
	Record.ComponentMask = 0;
	Record.TransformSlot = INDEX_NONE;
	Record.bTickEnabled = bTickEnabled;
	Record.bVisible = bVisible;
	Record.bPendingDestroy = false;
	Record.bAlive = true;
	++NumAlive;
	return Entity;
}

void FEcsWorld::DestroyEntity( int32 Entity )
{
	if ( !IsAlive( Entity ) ) return;
	FEntityRecord& Record = Records[Entity];

	// 持っているデータコンポーネントの入れ物だけを外す（全入れ物を毎回走査しない）
	uint64 Mask = Record.ComponentMask;
	while ( Mask != 0ull )
	{
		unsigned long BitIndex = 0;
		_BitScanForward64( &BitIndex, Mask );
		Mask &= ( Mask - 1ull );
		if ( static_cast<int32>( BitIndex ) < StorageById.Num() && StorageById[static_cast<int32>( BitIndex )] != nullptr )
		{
			StorageById[static_cast<int32>( BitIndex )]->Remove( Entity );
		}
	}
	for ( IComponentStorage* Overflow : OverflowStorages ) Overflow->Remove( Entity );

	Record = FEntityRecord{};
	--NumAlive;
	FreeSlots.Add( Entity );
}

void FEcsWorld::FlushDeferred()
{
	TArray<int32> Merged;
	for ( TArray<int32>& PerThread : DeferredDestroy )
	{
		Merged.Append( PerThread );
		PerThread.Reset();
	}
	if ( Merged.IsEmpty() ) return;

	// どのスレッドが予約したかに関わらず、同じ順序・同じ内容で実行する
	Merged.Sort();
	int32 NumUnique = 0;
	for ( int32 i = 0; i < Merged.Num(); ++i )
	{
		if ( i == 0 || Merged[i] != Merged[i - 1] ) Merged[NumUnique++] = Merged[i];
	}
	Merged.SetNum( NumUnique );
	for ( const int32 Entity : Merged )
	{
		if ( IsAlive( Entity ) && Records[Entity].Object != nullptr ) Records[Entity].Object->Destroy();
	}
}
