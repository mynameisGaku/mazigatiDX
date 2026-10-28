#pragma once
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SmartPointer.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Ecs/EcsWorld.h"
#include "Library/Framework/Ecs/System.h"
#include "Library/Framework/Ecs/TransformWorld.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Object/Object.h"
#include "Library/Framework/Scene/ISceneManager.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

class CGameInstance;

//=============================================================================
// CScene
//   ゲームオブジェクトの入れ物。タイトル・ゲーム本編・ポーズメニューなどの単位。
//   CSceneManager に登録して使う（生成・破棄・切り替えはマネージャーが行う）。
//   シーンごとに CSceneSubsystem が生成される（GetSubsystem<T>() で取得）。
//
//   class CGameScene : public CScene
//   {
//   protected:
//       void OnEnter() override { SpawnObject<CPlayer>(); }
//       void OnTick( float32 DeltaTime ) override
//       {
//           if ( bCleared ) GetSceneManager()->ChangeScene( "Result" );
//       }
//   };
//
//   開始: サブシステム生成 -> OnEnter      終了: OnExit -> 全オブジェクト破棄 -> サブシステム破棄
//   Tick: サブシステム (PreUpdate) -> OnTick -> 各オブジェクトの Tick -> サブシステム (PostUpdate)
//         -> 破棄予約されたオブジェクトの OnDestroy
//   Draw: 描画システムの記録（並列）-> 各オブジェクトの Draw（DrawOrder 順。オブジェクトごとにコマンドリストを空にして渡し、記録されたものを実行する。
//         その直後に、そのオブジェクトの描画システムの出力）-> OnDraw（UI など手前に出すもの）
//=============================================================================
class CScene : public CObject
{
	friend class CSceneManager;
	friend class CGameObject;

public:
	CScene();
	virtual ~CScene();

	//-------------------------------------------------------------------------
	// オブジェクト
	//-------------------------------------------------------------------------
	// T を生成してシーンに追加する。戻り値を保持し続けるなら TWeakPtr にする
	template<typename T, typename... Args>
	FORCEINLINE TSharedPtr<T> SpawnObject( Args&&... args )
	{
		static_assert( bIsBaseOf<CGameObject, T>, "SpawnObject<T>: T must derive from CGameObject" );
		TSharedPtr<T> Object = MakeShared<T>( Forward<Args>( args )... );
		AddObject( Object );
		return Object;
	}

	// 派生クラスを作らずに、CTransformComponent と任意個数のデータコンポーネント（ECS）を付けたオブジェクトを 1 行で生成する
	//   SpawnEntity( FAngularMotion{ FVector3::Up, 2.0f }, FHealth{ 100 } );
	template<typename... TComps>
	FORCEINLINE TSharedPtr<CGameObject> SpawnEntity( TComps&&... Comps )
	{
		TSharedPtr<CGameObject> Object = SpawnObject<CGameObject>();
		Object->AddComponent<CTransformComponent>();
		Object->AddDataAll( Forward<TComps>( Comps )... );
		return Object;
	}

	// 初期位置を指定して、CTransformComponent とデータコンポーネント付きのオブジェクトを 1 行で生成する
	template<typename... TComps>
	FORCEINLINE TSharedPtr<CGameObject> SpawnEntityAt( const FVector3& Position, TComps&&... Comps )
	{
		TSharedPtr<CGameObject> Object = SpawnObject<CGameObject>();
		Object->AddComponent<CTransformComponent>()->SetLocalPosition( Position );
		Object->AddDataAll( Forward<TComps>( Comps )... );
		return Object;
	}

	template<typename... TComps>
	FORCEINLINE TSharedPtr<CGameObject> SpawnEntityTransform( const FTransform& LocalTransform, TComps&&... Comps )
	{
		TSharedPtr<CGameObject> Object = SpawnObject<CGameObject>();
		Object->AddComponent<CTransformComponent>()->SetLocalTransform( LocalTransform );
		Object->AddDataAll( Forward<TComps>( Comps )... );
		return Object;
	}

	// 最初に見つかった T（無ければ空）
	template<typename T>
	FORCEINLINE TSharedPtr<T> FindObject() const
	{
		for ( const TSharedPtr<CGameObject>& Object : Objects )
		{
			if ( Object->IsAlive() && Object->IsA<T>() ) return StaticCastSharedPtr<T>( Object );
		}
		return nullptr;
	}

	template<typename T>
	FORCEINLINE TArray<TSharedPtr<T>> FindObjects() const
	{
		TArray<TSharedPtr<T>> Result;
		for ( const TSharedPtr<CGameObject>& Object : Objects )
		{
			if ( Object->IsAlive() && Object->IsA<T>() ) Result.Add( StaticCastSharedPtr<T>( Object ) );
		}
		return Result;
	}

	TSharedPtr<CGameObject> FindObjectByName( const FString& Name ) const;
	// 親を持たないオブジェクト（階層表示の起点）
	TArray<CGameObject*> GetRootObjects() const;
	TSharedPtr<CGameObject> FindObjectById( uint64 Id ) const;

	// 所属中のオブジェクト（生成直後でまだ Tick されていないものも含む）
	FORCEINLINE const TArray<TSharedPtr<CGameObject>>& GetObjects() const { return Objects; }
	FORCEINLINE int32 GetNumObjects() const { return Objects.Num(); }

	// 全オブジェクトを即座に破棄する（Tick / Draw の最中には呼ばない）
	void DestroyAllObjects();

	//-------------------------------------------------------------------------
	// シーン
	//-------------------------------------------------------------------------
	FORCEINLINE ISceneManager* GetSceneManager() const { return SceneManager; }
	// プロファイラー用: このシーン名の ID。TickScene が記録中に毎フレーム更新する（記録していないときは 0）
	FORCEINLINE uint32 GetProfilerOwnerId() const { return ProfilerOwnerId; }

	// シーンマネージャーの所有者（単体で動かしているなら nullptr）
	FORCEINLINE CGameInstance* GetGameInstance() const { return SceneManager ? SceneManager->GetGameInstance() : nullptr; }

	// このシーンの CTransformComponent のデータ（ECS）。通常は CTransformComponent 経由で使う
	FORCEINLINE FTransformWorld& GetTransformWorld() { return TransformWorld; }
	FORCEINLINE const FTransformWorld& GetTransformWorld() const { return TransformWorld; }

	//-------------------------------------------------------------------------
	// ECS（データコンポーネントとシステム・即時クエリ）
	//   オブジェクトに CGameObject::AddData<T>() や SpawnEntity(...) でデータを付け、システムを登録するか ForEach で走査する。
	//   組み込みのシステム（FAngularMotion / FLinearMotion）は、すべてのシーンに最初から入っている。
	//
	//   // 毎フレーム自動で並列実行されるシステム（フェーズ省略時は PreUpdate）
	//   AddSystem( "Health.Regen", []( float32 DeltaTime, FHealth& Health ) { Health.Value += DeltaTime; } );
	//
	//   // その場で条件に合うオブジェクトを走査する即時クエリ（T* で任意コンポーネント、NEcs::TWithout<...> で除外フィルタ）
	//   ForEach( []( CGameObject& Obj, FHealth& Health, const FShield* Shield, NEcs::TWithout<FInvincible> ) { ... } );
	//-------------------------------------------------------------------------
	template<typename TFunction>
	FORCEINLINE void AddSystem( NEcs::EPhase Phase, const char* Name, TFunction&& Function ) { Systems.Add( Phase, Name, Forward<TFunction>( Function ) ); }
	template<typename TFunction>
	FORCEINLINE void AddSystem( const char* Name, TFunction&& Function ) { Systems.Add( NEcs::EPhase::PreUpdate, Name, Forward<TFunction>( Function ) ); }

	// 条件に合う生きているオブジェクトをその場で走査し、処理した件数を返す（bool を返すラムダなら false で途中終了）
	template<typename TFunction>
	FORCEINLINE int32 ForEach( TFunction&& Function ) { return NEcs::ForEach( EcsWorld, TransformWorld, Forward<TFunction>( Function ) ); }

	// 指定したすべてのデータコンポーネントを持つ、生きているオブジェクトの数を返す
	template<typename TFirst, typename... TRest>
	FORCEINLINE int32 CountData() const { return NEcs::CountEntities<TFirst, TRest...>( EcsWorld ); }

	// データコンポーネント T を持つ最初の生きているオブジェクトのデータ（無ければ nullptr）
	template<typename T>
	FORCEINLINE T* FindFirstData()
	{
		TComponentStorage<T>* Storage = EcsWorld.FindStorage<T>();
		if ( Storage == nullptr ) return nullptr;
		const int32* Entities = Storage->GetEntities();
		const int32 Num = Storage->Num();
		for ( int32 i = 0; i < Num; ++i )
		{
			const FEcsWorld::FEntityRecord& Record = EcsWorld.GetRecord( Entities[i] );
			if ( Record.bAlive && !Record.bPendingDestroy ) return &Storage->GetDenseAt( i );
		}
		return nullptr;
	}

	template<typename T>
	FORCEINLINE const T* FindFirstData() const { return const_cast<CScene*>( this )->FindFirstData<T>(); }

	// データコンポーネント T を持つ最初の生きているオブジェクト（無ければ nullptr）
	template<typename T>
	FORCEINLINE CGameObject* FindFirstObjectWithData() const
	{
		const TComponentStorage<T>* Storage = EcsWorld.FindStorage<T>();
		if ( Storage == nullptr ) return nullptr;
		const int32* Entities = Storage->GetEntities();
		const int32 Num = Storage->Num();
		for ( int32 i = 0; i < Num; ++i )
		{
			const FEcsWorld::FEntityRecord& Record = EcsWorld.GetRecord( Entities[i] );
			if ( Record.bAlive && !Record.bPendingDestroy ) return Record.Object;
		}
		return nullptr;
	}

	FORCEINLINE FEcsWorld& GetEcsWorld() { return EcsWorld; }
	FORCEINLINE const FEcsWorld& GetEcsWorld() const { return EcsWorld; }
	FORCEINLINE NEcs::FSystemScheduler& GetSystems() { return Systems; }
	FORCEINLINE const NEcs::FSystemScheduler& GetSystems() const { return Systems; }

	//-------------------------------------------------------------------------
	// 描画システム（2D / 3D の描画を、DxLib を呼ばずに「記録」するだけにして、並列に動かす）
	//   記録は並列（ワーカー）、DxLib への実行は直列（メインスレッド）。実行の順番は、DrawOrder -> 生成順のままで、
	//   オブジェクトの Draw( FDrawContext& CommandList ) と混ぜても重なりの順番は変わらない。詳しくは System.h と DrawContext.h を参照。
	//
	//   AddDrawSystem( "Marker.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FMarker& Marker )
	//   {
	//       Draw.Circle( ..., Marker.Color );
	//   } );
	//-------------------------------------------------------------------------
	template<typename TFunction>
	FORCEINLINE void AddDrawSystem( const char* Name, TFunction&& Function ) { Systems.AddDraw( Name, Forward<TFunction>( Function ) ); }
	// 描画コマンドの実行先を差し替える（既定は、アプリの IDrawExecutor。記録するだけの実行先を渡して確かめる、など）。nullptr で既定に戻す
	FORCEINLINE void SetDrawExecutor( IDrawExecutor* InExecutor ) { DrawExecutorOverride = InExecutor; }
	// シーンが使う描画先を返す。差し替えがなければアプリの描画先を返し、所有権は持たない
	IDrawExecutor* GetDrawExecutor() const;

	// このシーンのサブシステム（T はクラスでもインターフェースでもよい）
	template<typename T>
	FORCEINLINE T* GetSubsystem() const { return Subsystems.Get<T>(); }
	FORCEINLINE const FSubsystemCollection& GetSubsystems() const { return Subsystems; }
	// シーン開始からの経過秒（このシーンが Tick された時間の合計）
	FORCEINLINE float64 GetElapsedTime() const { return ElapsedTime; }

protected:
	//-------------------------------------------------------------------------
	// 派生クラスで実装するイベント
	//-------------------------------------------------------------------------
	// シーン開始時（オブジェクトの生成はここで行う）
	virtual void OnEnter();
	// シーン終了時（この後に全オブジェクトが破棄される）
	virtual void OnExit();
	// 上に別のシーンが積まれた / 取り除かれた
	virtual void OnPause();
	virtual void OnResume();

	// オブジェクトの Tick より前に呼ばれる
	virtual void OnTick( float32 DeltaTime );
	// オブジェクトの Draw より後に呼ばれる
	virtual void OnDraw();

private:
	// CSceneManager から呼ばれる
	void Enter( ISceneManager* InSceneManager );
	void Exit();
	void TickScene( float32 DeltaTime );
	void DrawScene();

	void AddObject( const TSharedPtr<CGameObject>& Object );
	void RemoveDestroyedObjects();
	// OnDestroy を 1 回だけ呼ぶ
	void NotifyDestroy( CGameObject& Object );
	void RebuildDrawList();
	FORCEINLINE void MarkDrawOrderDirty() { bDrawOrderDirty = true; }

private:
	ISceneManager* SceneManager = nullptr;
	FSubsystemCollection Subsystems;
	uint32 ProfilerOwnerId = 0;

	// CTransformComponent のデータと、データコンポーネント。Objects より先に作られ、後に壊れる（オブジェクトが外れるときにデータを返すため）
	FTransformWorld TransformWorld;
	FEcsWorld EcsWorld;
	NEcs::FSystemScheduler Systems;

	// 生成順
	TArray<TSharedPtr<CGameObject>> Objects;
	// DrawOrder 順に並べた Objects
	TArray<CGameObject*> DrawList;
	bool bDrawOrderDirty = false;
	IDrawExecutor* DrawExecutorOverride = nullptr;
	// オブジェクトの Draw に渡すコマンドリストの入れ物。オブジェクトごとに空にして使い回す（確保した領域は残る）
	FDrawBuffer ObjectCommandList;

	uint64 NextObjectId = 1;
	float64 ElapsedTime = 0.0;
};
