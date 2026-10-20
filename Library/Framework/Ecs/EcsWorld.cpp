#include "EcsWorld.h"

#include <algorithm>
#include <intrin.h>

#include "Library/Framework/Object/GameObject.h"

int32 FEcsWorld::CreateEntity( CGameObject* Object, uint64 ObjectId, bool bTickEnabled, bool bVisible )
{
	int32 Entity;
	if ( !FreeSlots.empty() )
	{
		Entity = FreeSlots.back();
		FreeSlots.pop_back();
	}
	else
	{
		Entity = static_cast<int32>( Records.size() );
		Records.emplace_back();
	}
	FEntityRecord& Record = Records[static_cast<size_t>( Entity )];
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
	FEntityRecord& Record = Records[static_cast<size_t>( Entity )];

	// 持っているデータコンポーネントの入れ物だけを外す（全入れ物を毎回走査しない）
	uint64 Mask = Record.ComponentMask;
	while ( Mask != 0ull )
	{
		unsigned long BitIndex = 0;
		_BitScanForward64( &BitIndex, Mask );
		Mask &= ( Mask - 1ull );
		if ( BitIndex < StorageById.size() && StorageById[BitIndex] != nullptr )
		{
			StorageById[BitIndex]->Remove( Entity );
		}
	}
	for ( IComponentStorage* Overflow : OverflowStorages ) Overflow->Remove( Entity );

	Record = FEntityRecord{};
	--NumAlive;
	FreeSlots.push_back( Entity );
}

void FEcsWorld::FlushDeferred()
{
	std::vector<int32> Merged;
	for ( std::vector<int32>& PerThread : DeferredDestroy )
	{
		Merged.insert( Merged.end(), PerThread.begin(), PerThread.end() );
		PerThread.clear();
	}
	if ( Merged.empty() ) return;

	// どのスレッドが予約したかに関わらず、同じ順序・同じ内容で実行する
	std::sort( Merged.begin(), Merged.end() );
	Merged.erase( std::unique( Merged.begin(), Merged.end() ), Merged.end() );
	for ( const int32 Entity : Merged )
	{
		if ( IsAlive( Entity ) && Records[static_cast<size_t>( Entity )].Object != nullptr ) Records[static_cast<size_t>( Entity )].Object->Destroy();
	}
}
