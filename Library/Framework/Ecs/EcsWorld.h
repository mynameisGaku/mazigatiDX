#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/Utility.h"

class CGameObject;
struct FTransformNode;

namespace NEcs
{
	// 型ごとに一意なキー。システムが「何を読み書きするか」の衝突判定に使う
	template<typename T>
	FORCEINLINE const void* TypeKey()
	{
		static const char Key = 0;
		return &Key;
	}

	inline int32 AllocateTypeId()
	{
		static TAtomic<int32> NextId{ 0 };
		return NextId.FetchAdd( 1, EMemoryOrder::Relaxed );
	}

	// 型ごとの 0 からの通し番号（O(1) の入れ物検索とビットマスク判定用）
	template<typename T>
	FORCEINLINE int32 TypeId()
	{
		static const int32 Id = AllocateTypeId();
		return Id;
	}

	// 最初の 64 個のコンポーネント型は 1 命令のビットマスクで有無を判定できる
	template<typename T>
	FORCEINLINE uint64 TypeBit()
	{
		const int32 Id = TypeId<T>();
		return static_cast<uint32>( Id ) < 64u ? ( 1ull << static_cast<uint32>( Id ) ) : 0ull;
	}

	// システムや ForEach の引数に置くと、「指定した型を持たないエンティティだけ」に絞り込むフィルタ
	//   Scene.ForEach( []( FHealth& Health, NEcs::TWithout<FInvincible> ) { ... } );
	template<typename... TExcluded>
	struct TWithout {};

	// スレッドごとの一覧の数（ワーカー最大 31 + メインスレッド）
	constexpr int32 MaxThreads = 32;
}

//=============================================================================
// IComponentStorage / TComponentStorage
//   データ型 T のコンポーネントを、エンティティ（= ゲームオブジェクト 1 つ）ごとに持つ入れ物（疎集合: Sparse Set）。
//   - Sparse と DenseEntity は型によらないので基底クラスに置き、仮想関数を介さずインラインで引けるようにしてある
//   - 持っているものだけが Dense に隙間なく並ぶので、「T を持つもの全部」を端から端まで速く回せる（キャッシュに優しい）
//   - Add / Emplace / Remove は構造の変更なので、システムの実行中には行わない（システムは、持っているものを読み書きするだけ）
//=============================================================================
class IComponentStorage
{
public:
	explicit IComponentStorage( int32 InTypeId, const void* InTypeKey ) : StorageTypeId( InTypeId ), StorageTypeKey( InTypeKey ) {}
	virtual ~IComponentStorage() = default;

	FORCEINLINE int32 GetTypeId() const { return StorageTypeId; }
	FORCEINLINE const void* GetTypeKey() const { return StorageTypeKey; }
	FORCEINLINE int32 Num() const { return DenseEntity.Num(); }
	// Dense の i 番目を持つエンティティの番号
	FORCEINLINE int32 EntityAt( int32 DenseIndex ) const { return DenseEntity[DenseIndex]; }
	FORCEINLINE const int32* GetEntities() const { return DenseEntity.GetData(); }
	// このエンティティの Dense の位置（持っていなければ INDEX_NONE）
	FORCEINLINE int32 IndexOf( int32 Entity ) const
	{
		return static_cast<uint32>( Entity ) < static_cast<uint32>( Sparse.Num() ) ? Sparse[Entity] : INDEX_NONE;
	}
	FORCEINLINE bool Has( int32 Entity ) const { return IndexOf( Entity ) != INDEX_NONE; }

	virtual void Remove( int32 Entity ) = 0;

protected:
	TArray<int32> Sparse;        // エンティティの番号 -> Dense の位置（持っていなければ INDEX_NONE）
	TArray<int32> DenseEntity;   // Dense の位置 -> エンティティの番号
	int32 StorageTypeId = INDEX_NONE;
	const void* StorageTypeKey = nullptr;
};

template<typename T>
class TComponentStorage final : public IComponentStorage
{
public:
	TComponentStorage() : IComponentStorage( NEcs::TypeId<T>(), NEcs::TypeKey<T>() ) {}

	// 追加する（すでに持っていれば上書き）。戻り値は、次に構造を変えるまで有効
	T* Add( int32 Entity, const T& Value )
	{
		if ( Entity >= Sparse.Num() ) Sparse.SetNum( static_cast<size_t>( Entity ) + 1, INDEX_NONE );
		int32& DenseIndex = Sparse[Entity];
		if ( DenseIndex != INDEX_NONE )
		{
			Dense[DenseIndex] = Value;
			return &Dense[DenseIndex];
		}
		DenseIndex = Dense.Num();
		Dense.Add( Value );
		DenseEntity.Add( Entity );
		return &Dense.Last();
	}

	// コンストラクタ引数を渡してその場で構築する（すでに持っていれば上書き）
	template<typename... TArgs>
	T* Emplace( int32 Entity, TArgs&&... Args )
	{
		if ( Entity >= Sparse.Num() ) Sparse.SetNum( static_cast<size_t>( Entity ) + 1, INDEX_NONE );
		int32& DenseIndex = Sparse[Entity];
		if ( DenseIndex != INDEX_NONE )
		{
			Dense[DenseIndex] = T( Forward<TArgs>( Args )... );
			return &Dense[DenseIndex];
		}
		DenseIndex = Dense.Num();
		Dense.Emplace( Forward<TArgs>( Args )... );
		DenseEntity.Add( Entity );
		return &Dense.Last();
	}

	// すでに持っていればそれを返し、無ければ新しく作って返す
	template<typename... TArgs>
	T& GetOrAdd( int32 Entity, TArgs&&... Args )
	{
		if ( Entity >= Sparse.Num() ) Sparse.SetNum( static_cast<size_t>( Entity ) + 1, INDEX_NONE );
		int32& DenseIndex = Sparse[Entity];
		if ( DenseIndex != INDEX_NONE ) return Dense[DenseIndex];
		DenseIndex = Dense.Num();
		Dense.Emplace( Forward<TArgs>( Args )... );
		DenseEntity.Add( Entity );
		return Dense.Last();
	}

	FORCEINLINE T* Find( int32 Entity )
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Sparse.Num() ) ) return nullptr;
		const int32 DenseIndex = Sparse[Entity];
		return DenseIndex != INDEX_NONE ? &Dense[DenseIndex] : nullptr;
	}

	FORCEINLINE const T* Find( int32 Entity ) const
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Sparse.Num() ) ) return nullptr;
		const int32 DenseIndex = Sparse[Entity];
		return DenseIndex != INDEX_NONE ? &Dense[DenseIndex] : nullptr;
	}

	// Dense の i 番目を直接取る（Driver の走査では Sparse を引かずに連続アクセスできる）
	FORCEINLINE T& GetDenseAt( int32 DenseIndex ) { return Dense[DenseIndex]; }
	FORCEINLINE const T& GetDenseAt( int32 DenseIndex ) const { return Dense[DenseIndex]; }
	FORCEINLINE T* GetDenseData() { return Dense.GetData(); }
	FORCEINLINE const T* GetDenseData() const { return Dense.GetData(); }

	// Has( Entity ) や ComponentMask で持っていることが分かっているときの高速アクセス
	FORCEINLINE T& GetUnchecked( int32 Entity ) { return Dense[Sparse[Entity]]; }
	FORCEINLINE const T& GetUnchecked( int32 Entity ) const { return Dense[Sparse[Entity]]; }

	void Remove( int32 Entity ) override
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Sparse.Num() ) ) return;
		const int32 DenseIndex = Sparse[Entity];
		if ( DenseIndex == INDEX_NONE ) return;

		// 最後の要素を空いたところへ移す（Dense に隙間を作らない）
		const int32 LastIndex = Dense.Num() - 1;
		if ( DenseIndex != LastIndex )
		{
			Dense[DenseIndex] = MoveTemp( Dense[LastIndex] );
			DenseEntity[DenseIndex] = DenseEntity[LastIndex];
			Sparse[DenseEntity[DenseIndex]] = DenseIndex;
		}
		Dense.Pop();
		DenseEntity.Pop();
		Sparse[Entity] = INDEX_NONE;
	}

private:
	TArray<T> Dense;
};

//=============================================================================
// FEcsWorld
//   シーンごとに 1 つ。エンティティ（= シーンに入っているゲームオブジェクト 1 つ）の番号と、データ型ごとの入れ物を持つ。
//   ゲームオブジェクトは、シーンに入るときにエンティティになり、シーンから外れるときにデータごと消える。
//
//   使う側は CGameObject::AddData<T>() / GetData<T>() や CScene::ForEach() を使う。
//   システム（FSystemScheduler）が、型の組み合わせで「持っているもの全部」を並列に回す。
//=============================================================================
class FEcsWorld
{
public:
	// エンティティ 1 つの記録。連続配列（Records）に置き、システムが 1 回のメモリアクセスで必要な値をすべて読めるようにしてある
	struct FEntityRecord
	{
		CGameObject* Object = nullptr;
		FTransformNode* TransformNode = nullptr;   // CTransformComponent のノード（プールの要素は動かないので直接指す）
		uint64 ObjectId = 0;
		uint64 ComponentMask = 0;                  // 持っているデータコンポーネントのビットマスク（TypeId 0..63）
		int32 TransformSlot = INDEX_NONE;          // CTransformComponent のデータの番号
		bool bTickEnabled = true;
		bool bVisible = true;
		bool bPendingDestroy = false;
		bool bAlive = false;
	};

	int32 CreateEntity( CGameObject* Object, uint64 ObjectId, bool bTickEnabled, bool bVisible );
	// 持っているデータをすべて外して、番号を返す
	void DestroyEntity( int32 Entity );

	FORCEINLINE bool IsAlive( int32 Entity ) const
	{
		return static_cast<uint32>( Entity ) < static_cast<uint32>( Records.Num() ) && Records[Entity].bAlive;
	}
	FORCEINLINE CGameObject* GetEntityObject( int32 Entity ) const { return Records[Entity].Object; }
	FORCEINLINE const FEntityRecord& GetRecord( int32 Entity ) const { return Records[Entity]; }
	FORCEINLINE FEntityRecord& GetRecordMutable( int32 Entity ) { return Records[Entity]; }
	FORCEINLINE const FEntityRecord* GetRecords() const { return Records.GetData(); }
	// 使われたことのある番号の上限（エンティティを端から端まで回すときの範囲）
	FORCEINLINE int32 GetNumEntitySlots() const { return Records.Num(); }
	FORCEINLINE int32 GetNumEntities() const { return NumAlive; }

	// T の入れ物（O(1) で引く。無ければ作る。メインスレッドから）
	template<typename T>
	FORCEINLINE TComponentStorage<T>& GetStorage()
	{
		const int32 Id = NEcs::TypeId<T>();
		if ( static_cast<uint32>( Id ) < static_cast<uint32>( StorageById.Num() ) )
		{
			if ( IComponentStorage* Found = StorageById[Id] )
			{
				return *static_cast<TComponentStorage<T>*>( Found );
			}
		}
		return GetOrCreateStorageSlow<T>( Id );
	}

	// T の入れ物（まだ 1 つも作られていなければ nullptr）
	template<typename T>
	FORCEINLINE TComponentStorage<T>* FindStorage()
	{
		const int32 Id = NEcs::TypeId<T>();
		return static_cast<uint32>( Id ) < static_cast<uint32>( StorageById.Num() )
			? static_cast<TComponentStorage<T>*>( StorageById[Id] )
			: nullptr;
	}

	template<typename T>
	FORCEINLINE const TComponentStorage<T>* FindStorage() const
	{
		const int32 Id = NEcs::TypeId<T>();
		return static_cast<uint32>( Id ) < static_cast<uint32>( StorageById.Num() )
			? static_cast<const TComponentStorage<T>*>( StorageById[Id] )
			: nullptr;
	}

	//-------------------------------------------------------------------------
	// エンティティへのデータの付け外し（ComponentMask も同時に更新する）
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE T* AddData( int32 Entity, const T& Value )
	{
		Records[Entity].ComponentMask |= NEcs::TypeBit<T>();
		return GetStorage<T>().Add( Entity, Value );
	}

	template<typename T, typename... TArgs>
	FORCEINLINE T* EmplaceData( int32 Entity, TArgs&&... Args )
	{
		Records[Entity].ComponentMask |= NEcs::TypeBit<T>();
		return GetStorage<T>().Emplace( Entity, Forward<TArgs>( Args )... );
	}

	template<typename T, typename... TArgs>
	FORCEINLINE T& GetOrAddData( int32 Entity, TArgs&&... Args )
	{
		Records[Entity].ComponentMask |= NEcs::TypeBit<T>();
		return GetStorage<T>().GetOrAdd( Entity, Forward<TArgs>( Args )... );
	}

	template<typename T>
	FORCEINLINE T* GetData( int32 Entity )
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Records.Num() ) ) return nullptr;
		const uint64 Bit = NEcs::TypeBit<T>();
		if ( Bit != 0ull && ( Records[Entity].ComponentMask & Bit ) == 0ull ) return nullptr;
		TComponentStorage<T>* Storage = FindStorage<T>();
		return Storage != nullptr ? ( Bit != 0ull ? &Storage->GetUnchecked( Entity ) : Storage->Find( Entity ) ) : nullptr;
	}

	template<typename T>
	FORCEINLINE const T* GetData( int32 Entity ) const
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Records.Num() ) ) return nullptr;
		const uint64 Bit = NEcs::TypeBit<T>();
		if ( Bit != 0ull && ( Records[Entity].ComponentMask & Bit ) == 0ull ) return nullptr;
		const TComponentStorage<T>* Storage = FindStorage<T>();
		return Storage != nullptr ? ( Bit != 0ull ? &Storage->GetUnchecked( Entity ) : Storage->Find( Entity ) ) : nullptr;
	}

	template<typename T>
	FORCEINLINE bool HasData( int32 Entity ) const
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Records.Num() ) ) return false;
		const uint64 Bit = NEcs::TypeBit<T>();
		if ( Bit != 0ull ) return ( Records[Entity].ComponentMask & Bit ) != 0ull;
		const TComponentStorage<T>* Storage = FindStorage<T>();
		return Storage != nullptr && Storage->Has( Entity );
	}

	template<typename T>
	FORCEINLINE void RemoveData( int32 Entity )
	{
		if ( static_cast<uint32>( Entity ) >= static_cast<uint32>( Records.Num() ) ) return;
		Records[Entity].ComponentMask &= ~NEcs::TypeBit<T>();
		if ( TComponentStorage<T>* Storage = FindStorage<T>() ) Storage->Remove( Entity );
	}

	// システムの中から、このエンティティのゲームオブジェクトの破棄を予約する（スレッドごとの一覧に溜める）
	FORCEINLINE void RequestDestroy( int32 Entity ) { DeferredDestroy[IJobSystem::GetCurrentThreadIndex()].Add( Entity ); }
	// 予約された破棄を実行する（メインスレッド、システムの実行が終わったあと。番号順に、重複なく）
	void FlushDeferred();

private:
	template<typename T>
	TComponentStorage<T>& GetOrCreateStorageSlow( int32 Id )
	{
		if ( Id >= StorageById.Num() ) StorageById.SetNum( Id + 1, nullptr );
		auto Created = MakeUnique<TComponentStorage<T>>();
		TComponentStorage<T>* Raw = Created.Get();
		StorageById[Id] = Raw;
		if ( Id >= 64 ) OverflowStorages.Add( Raw );
		Storages.Add( MoveTemp( Created ) );
		return *Raw;
	}

private:
	TArray<FEntityRecord> Records;
	TArray<int32> FreeSlots;
	int32 NumAlive = 0;
	TArray<TUniquePtr<IComponentStorage>> Storages;
	TArray<IComponentStorage*> StorageById;        // TypeId -> 入れ物（O(1) 検索）
	TArray<IComponentStorage*> OverflowStorages;   // TypeId >= 64 の入れ物（通常は空）
	TArray<int32> DeferredDestroy[NEcs::MaxThreads];
};
