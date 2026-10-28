#pragma once
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Component/Component.h"
#include "Library/Framework/Ecs/EcsWorld.h"
#include "Library/Framework/Object/Object.h"

class CScene;
class CTransformComponent;
class FDrawContext;

DECLARE_LOG_CATEGORY_EXTERN( LogGameObject, Log )

//=============================================================================
// CGameObject
//   シーンに所属し、毎フレーム Tick / Draw されるオブジェクトの基底。
//   生成は必ず CScene::SpawnObject で行う（シーンが TSharedPtr で所有する）。
//
//   class CEnemy : public CGameObject
//   {
//   public:
//       explicit CEnemy( int32 InHp ) : Hp( InHp ) {}
//       void OnSpawn() override { AddComponent<CTransformComponent>(); }
//       void Tick( float32 DeltaTime ) override { if ( Hp <= 0 ) Destroy(); }
//       void Draw( FDrawContext& CommandList ) override { CommandList.Circle( X, Y, 8, FColor32::Red ); }
//   };
//
//   TSharedPtr<CEnemy> Enemy = Scene.SpawnObject<CEnemy>( 100 );
//   TWeakPtr<CEnemy>   Target = Enemy;   // 他のオブジェクトから参照するときは弱参照で持つ
//
//   描画は、DX12 のコマンドリストと同じ方式: Draw( FDrawContext& CommandList ) は、シーンが用意したコマンドリストに「何を描くか」を記録するだけ。
//   DxLib を呼ばず、実行先（IDrawExecutor）も知らなくてよい。シーンが、オブジェクトごとにコマンドリストを空にして渡し（Reset）、記録されたものを
//   DrawOrder の順に実行する（ExecuteCommandLists）ので、ブレンドモードなどの状態は、次のオブジェクトに漏れない。
//
//   - SpawnObject 中に OnSpawn が呼ばれる。Tick 中に生成した場合、Tick は次のフレームから
//   - Destroy() は予約のみ。そのフレームの Tick 後に OnDestroy が呼ばれシーンから外れる
//
//   親子関係（所有はしない。親子ともシーンが所有する）:
//   Child->AttachToParent( Parent.Get() );
//   - 親を Destroy すると子もすべて Destroy される
//   - 子の CTransformComponent は親の CTransformComponent からの相対になる
//=============================================================================
class CGameObject : public CObject
{
	friend class CScene;
	friend class CTransformComponent;

public:
	//-------------------------------------------------------------------------
	// 派生クラスで実装するイベント
	//-------------------------------------------------------------------------
	virtual void OnSpawn();
	virtual void Tick( float32 DeltaTime );
	// 描画コマンドを記録する（CommandList に足すだけ。DxLib は呼ばない）。呼ばれるのは、見えている（IsVisible）間、毎フレーム 1 回。DrawOrder の順
	virtual void Draw( FDrawContext& CommandList );
	virtual void OnDestroy();

	//-------------------------------------------------------------------------
	// 寿命
	//-------------------------------------------------------------------------
	// 破棄を予約する（子も破棄予約される。何度呼んでもよい）
	void Destroy();
	FORCEINLINE bool IsPendingDestroy() const { return bPendingDestroy; }
	// シーンに所属していて破棄予約もされていない
	FORCEINLINE bool IsAlive() const { return Scene != nullptr && !bPendingDestroy; }

	// ログ用の名前: "CPhysicsBall 'Name' #12"（SetName していなければ名前は付かない）
	FString GetDebugName() const;

	// 所属シーン（シーンから外れた後は nullptr）
	FORCEINLINE CScene* GetScene() const { return Scene; }
	// シーン内で一意な ID（生成順に 1 から振られる。0 は未登録）
	FORCEINLINE uint64 GetId() const { return Id; }

	// 内部用: このオブジェクトの CTransformComponent のデータの番号（シーンの FTransformWorld 内。持っていなければ INDEX_NONE）
	FORCEINLINE int32 GetTransformSlot() const { return TransformSlot; }
	// 内部用: エンティティの番号（シーンの FEcsWorld 内。シーンに入っていなければ INDEX_NONE）
	FORCEINLINE int32 GetEntitySlot() const { return EntitySlot; }

	//-------------------------------------------------------------------------
	// 親子関係
	//-------------------------------------------------------------------------
	// 同じシーンのオブジェクトの子になる。nullptr なら親から外れる。
	// 別シーン・破棄予約済み・循環する場合は失敗して false
	bool AttachToParent( CGameObject* NewParent );
	FORCEINLINE void DetachFromParent() { AttachToParent( nullptr ); }

	FORCEINLINE CGameObject* GetParent() const { return Parent; }
	FORCEINLINE const TArray<CGameObject*>& GetChildren() const { return Children; }
	bool IsAncestorOf( const CGameObject* Object ) const;

	//-------------------------------------------------------------------------
	// コンポーネント
	//-------------------------------------------------------------------------
	template<typename T, typename... Args>
	FORCEINLINE T* AddComponent( Args&&... args )
	{
		static_assert( bIsBaseOf<CComponent, T>, "AddComponent<T>: T must derive from CComponent" );
		TUniquePtr<T> Component = MakeUnique<T>( Forward<Args>( args )... );
		T* Raw = Component.Get();
		AddComponentInternal( TUniquePtr<CComponent>( MoveTemp( Component ) ) );
		return Raw;
	}

	// 最初に見つかった T（クラスでもインターフェースでもよい。取り外し予約済みは除く）
	template<typename T>
	FORCEINLINE T* GetComponent()
	{
		for ( const FComponentEntry& Entry : Components )
		{
			if ( Entry.Component->IsPendingDestroy() ) continue;
			if ( T* Casted = dynamic_cast<T*>( Entry.Component.Get() ) ) return Casted;
		}
		return nullptr;
	}

	template<typename T>
	FORCEINLINE const T* GetComponent() const { return const_cast<CGameObject*>( this )->GetComponent<T>(); }

	FORCEINLINE int32 GetNumComponents() const { return Components.Num(); }
	FORCEINLINE CComponent* GetComponentAt( int32 Index ) const { return Components[Index].Component.Get(); }

	//-------------------------------------------------------------------------
	// データコンポーネント（ECS）
	//   ふつうの構造体 T を、このオブジェクトに付ける。Tick は要らない。
	//   システム（シーンの AddSystem、組み込みの FAngularMotion / FLinearMotion など）が、持っているオブジェクトをまとめて並列に処理する。
	//   シーンに入ってから（OnSpawn 以降）使う。オブジェクトがシーンから外れると、データも消える。
	//
	//   AddData( FAngularMotion{ FVector3::Forward, 1.5f } );
	//   AddData<FHealth>( 100 );
	//   AddDataAll( FAngularMotion{ FVector3::Up, 2.0f }, FLinearMotion{ FVector3::Forward } );
	//   if ( FAngularMotion* Motion = GetData<FAngularMotion>() ) Motion->RadiansPerSecond = 3.0f;
	//
	//   戻り値のポインタは、次にデータを追加・削除する（どのオブジェクトでも）まで有効
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE T* AddData( const T& Value )
	{
		FEcsWorld* World = GetEcsWorldForData( "AddData" );
		return World != nullptr ? World->AddData<T>( EntitySlot, Value ) : nullptr;
	}

	template<typename T, typename... TArgs, typename = TEnableIfT<!( sizeof...( TArgs ) == 1 && ( bIsSame<TDecayT<TArgs>, T> && ... ) )>>
	FORCEINLINE T* AddData( TArgs&&... Args )
	{
		FEcsWorld* World = GetEcsWorldForData( "AddData" );
		return World != nullptr ? World->EmplaceData<T>( EntitySlot, Forward<TArgs>( Args )... ) : nullptr;
	}

	template<typename T, typename... TArgs>
	FORCEINLINE T& GetOrAddData( TArgs&&... Args )
	{
		FEcsWorld* World = GetEcsWorldForData( "GetOrAddData" );
		COLLECTION_CHECK( World != nullptr, "GetOrAddData<T>: object must be in a scene" );
		return World->GetOrAddData<T>( EntitySlot, Forward<TArgs>( Args )... );
	}

	template<typename... TComps>
	FORCEINLINE void AddDataAll( TComps&&... Comps )
	{
		( AddData<TDecayT<TComps>>( Forward<TComps>( Comps ) ), ... );
	}

	template<typename T>
	FORCEINLINE T* GetData()
	{
		FEcsWorld* World = GetEcsWorldIfAny();
		return World != nullptr ? World->GetData<T>( EntitySlot ) : nullptr;
	}

	template<typename T>
	FORCEINLINE const T* GetData() const
	{
		const FEcsWorld* World = GetEcsWorldIfAny();
		return World != nullptr ? World->GetData<T>( EntitySlot ) : nullptr;
	}

	template<typename T>
	FORCEINLINE bool HasData() const
	{
		const FEcsWorld* World = GetEcsWorldIfAny();
		return World != nullptr && World->HasData<T>( EntitySlot );
	}

	template<typename... Ts>
	FORCEINLINE bool HasAllData() const
	{
		const FEcsWorld* World = GetEcsWorldIfAny();
		return World != nullptr && ( World->HasData<Ts>( EntitySlot ) && ... );
	}

	template<typename... Ts>
	FORCEINLINE bool HasAnyData() const
	{
		const FEcsWorld* World = GetEcsWorldIfAny();
		return World != nullptr && ( World->HasData<Ts>( EntitySlot ) || ... );
	}

	template<typename T>
	FORCEINLINE void RemoveData()
	{
		if ( FEcsWorld* World = GetEcsWorldIfAny() ) World->RemoveData<T>( EntitySlot );
	}

	//-------------------------------------------------------------------------
	// 更新・描画の制御
	//-------------------------------------------------------------------------
	FORCEINLINE bool IsTickEnabled() const { return bTickEnabled; }
	void SetTickEnabled( bool bEnabled );

	FORCEINLINE bool IsVisible() const { return bVisible; }
	void SetVisible( bool bInVisible );

	// 小さい順に描画する（同じ値なら生成順）
	FORCEINLINE int32 GetDrawOrder() const { return DrawOrder; }
	void SetDrawOrder( int32 InDrawOrder );

	//-------------------------------------------------------------------------
	// CObject
	//-------------------------------------------------------------------------
	void InspectProperties( IPropertyInspector& Inspector ) override;

private:
	struct FComponentEntry
	{
		TUniquePtr<CComponent> Component;
		ITickable* Tickable;  // ITickable を継承していなければ nullptr
	};

	void AddComponentInternal( TUniquePtr<CComponent> Component );
	void TickComponents( float32 DeltaTime, ETickGroup Group );

	// プロファイラー用: このオブジェクトの計測区間（記録中にだけ呼ぶ。Category は "Object.Tick" など）
	FProfileScope MakeProfileScope( const char* Category ) const;
	uint32 GetProfilerLabelId( IProfiler& Recorder ) const;

	// CScene から呼ばれる
	void TickObject( float32 DeltaTime );
	void DrawObject( FDrawContext& CommandList );
	void RemoveDestroyedComponents();
	// シーンから外れるとき（OnDestroy の後）
	void DetachAll();
	// シーンに入ったとき、すでに付いているコンポーネントへ知らせる
	void NotifySceneAssigned();
	FEcsWorld* GetEcsWorldIfAny() const;
	// CTransformComponent のデータの番号を変える（システム用の記録にも写す）
	void SetTransformSlotInternal( int32 Slot );
	// シーンに入っていなければ警告して nullptr
	FEcsWorld* GetEcsWorldForData( const char* What ) const;

private:
	CScene* Scene = nullptr;
	uint64 Id = 0;
	int32 TransformSlot = -1;   // INDEX_NONE
	int32 EntitySlot = -1;      // INDEX_NONE
	int32 DrawOrder = 0;
	bool bPendingDestroy = false;
	bool bDestroyNotified = false;
	bool bTickEnabled = true;
	bool bVisible = true;

	// プロファイラーの表示名（SetName された名前の ID）のキャッシュ
	mutable uint32 ProfilerLabelId = 0;
	mutable uint32 ProfilerLabelRevision = 0xFFFFFFFFu;

	CGameObject* Parent = nullptr;
	TArray<CGameObject*> Children;
	TArray<FComponentEntry> Components;
};
