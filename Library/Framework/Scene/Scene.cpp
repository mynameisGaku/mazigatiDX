#include "Scene.h"

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Ecs/BuiltinSystems.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Library/Framework/Subsystem/SceneSubsystem.h"

CScene::CScene()
{
	NEcs::RegisterBuiltinSystems( Systems );
}

CScene::~CScene()
{
	// Exit されずに破棄された場合も、オブジェクト -> サブシステムの順で片付ける
	DestroyAllObjects();
	Subsystems.Deinitialize();
}

IDrawExecutor* CScene::GetDrawExecutor() const
{
	return DrawExecutorOverride != nullptr ? DrawExecutorOverride : GetAppSubsystem<IDrawExecutor>();
}

#pragma region Objects

void CScene::AddObject( const TSharedPtr<CGameObject>& Object )
{
	Object->Scene = this;
	Object->Id = NextObjectId++;
	Object->EntitySlot = EcsWorld.CreateEntity( Object.Get(), Object->Id, Object->bTickEnabled, Object->bVisible );
	Objects.Add( Object );
	bDrawOrderDirty = true;
	Object->NotifySceneAssigned();   // コンストラクタで付けたコンポーネントを、シーンのデータへ移す

	Object->OnSpawn();
	LOG( LogGameObject, Verbose, "Spawned %s in '%s' (%d object(s))", *Object->GetDebugName(), *GetName(), Objects.Num() );
}

TSharedPtr<CGameObject> CScene::FindObjectByName( const FString& Name ) const
{
	for ( const TSharedPtr<CGameObject>& Object : Objects )
	{
		if ( Object->IsAlive() && Object->GetName() == Name ) return Object;
	}
	LOG( LogScene, Verbose, "FindObjectByName: no live object named '%s' in '%s'", *Name, *GetName() );
	return nullptr;
}

TSharedPtr<CGameObject> CScene::FindObjectById( uint64 Id ) const
{
	for ( const TSharedPtr<CGameObject>& Object : Objects )
	{
		if ( Object->IsAlive() && Object->GetId() == Id ) return Object;
	}
	LOG( LogScene, Verbose, "FindObjectById: no live object #%llu in '%s'", static_cast<unsigned long long>( Id ), *GetName() );
	return nullptr;
}

TArray<CGameObject*> CScene::GetRootObjects() const
{
	TArray<CGameObject*> Roots;
	for ( const TSharedPtr<CGameObject>& Object : Objects )
	{
		if ( Object->GetParent() == nullptr ) Roots.Add( Object.Get() );
	}
	return Roots;
}

void CScene::DestroyAllObjects()
{
	if ( !Objects.IsEmpty() ) LOG( LogGameObject, Verbose, "Destroying all %d object(s) in '%s'", Objects.Num(), *GetName() );

	// OnDestroy の中で新たに生成されたものも破棄する
	while ( !Objects.IsEmpty() )
	{
		TArray<TSharedPtr<CGameObject>> Removing = MoveTemp( Objects );
		Objects.Reset();
		for ( const TSharedPtr<CGameObject>& Object : Removing )
		{
			Object->Destroy();
			NotifyDestroy( *Object );
		}
		// 全員の OnDestroy が終わってから親子関係とコンポーネントを外す
		for ( const TSharedPtr<CGameObject>& Object : Removing )
		{
			Object->DetachAll();
			Object->Scene = nullptr;
		}
	}
	DrawList.Reset();
	bDrawOrderDirty = false;
}

void CScene::RemoveDestroyedObjects()
{
	// OnDestroy の中で別のオブジェクトが Destroy / SpawnObject されることがあるので、
	// 新しく OnDestroy を呼んだものが無くなるまでインデックスで走査する
	bool bNotified = true;
	while ( bNotified )
	{
		bNotified = false;
		for ( int32 i = 0; i < Objects.Num(); ++i )
		{
			CGameObject& Object = *Objects[i];
			if ( Object.IsPendingDestroy() && !Object.bDestroyNotified )
			{
				NotifyDestroy( Object );
				bNotified = true;
			}
		}
	}

	// 全員の OnDestroy が終わってから親子関係とコンポーネントを外す（OnDestroy から親を参照できるように）
	for ( int32 i = 0; i < Objects.Num(); ++i )
	{
		if ( Objects[i]->IsPendingDestroy() ) Objects[i]->DetachAll();
	}

	const int32 NumRemoved = Objects.RemoveAll( []( const TSharedPtr<CGameObject>& Object )
	{
		if ( !Object->IsPendingDestroy() ) return false;
		Object->Scene = nullptr;
		return true;
	} );
	if ( NumRemoved > 0 ) bDrawOrderDirty = true;
}

void CScene::NotifyDestroy( CGameObject& Object )
{
	if ( Object.bDestroyNotified ) return;
	Object.bDestroyNotified = true;
	LOG( LogGameObject, Verbose, "Destroying %s in '%s'", *Object.GetDebugName(), *GetName() );
	Object.OnDestroy();
}

void CScene::RebuildDrawList()
{
	DrawList.Reset();
	DrawList.Reserve( Objects.Num() );
	for ( const TSharedPtr<CGameObject>& Object : Objects ) DrawList.Add( Object.Get() );

	// 安定ソートなので同じ DrawOrder なら生成順
	DrawList.StableSort( []( const CGameObject* A, const CGameObject* B ) { return A->GetDrawOrder() < B->GetDrawOrder(); } );
	bDrawOrderDirty = false;
}

#pragma endregion

#pragma region Lifecycle

void CScene::Enter( ISceneManager* InSceneManager )
{
	SceneManager = InSceneManager;
	ElapsedTime = 0.0;
	Subsystems.SetLabel( GetName() );
	Subsystems.Initialize<CSceneSubsystem>( *this );
	OnEnter();
	LOG( LogScene, Verbose, "'%s' entered: %d object(s) after OnEnter", *GetName(), Objects.Num() );
}

void CScene::Exit()
{
	OnExit();
	DestroyAllObjects();
	Subsystems.Deinitialize();
	SceneManager = nullptr;
}

void CScene::TickScene( float32 DeltaTime )
{
	ElapsedTime += DeltaTime;

	// 記録中なら、シーン名を ID にしておく（以降の計測区間の Owner になる）
	ProfilerOwnerId = NProfiler::GRecorder ? NProfiler::GRecorder->InternString( *GetName() ) : 0u;
	PROFILE_SCOPE_LAZY( SceneScope, FProfileScope( "Scene", ProfilerOwnerId, "TickScene" ) );

	{
		PROFILE_SCOPE_LAZY( PreScope, FProfileScope( "Scene", ProfilerOwnerId, "Subsystems.PreUpdate" ) );
		Subsystems.Tick( DeltaTime, ETickGroup::PreUpdate );
	}
	{
		PROFILE_SCOPE_LAZY( OnTickScope, FProfileScope( "Scene", ProfilerOwnerId, "OnTick" ) );
		OnTick( DeltaTime );
	}

	// このフレーム中に生成されたものは次のフレームから Tick する（システムも同じ範囲を対象にする）
	const uint64 TickCutoffId = NextObjectId - 1;
	IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
	{
		PROFILE_SCOPE_LAZY( SystemsScope, FProfileScope( "Scene", ProfilerOwnerId, "Systems.PreUpdate" ) );
		Systems.Run( NEcs::EPhase::PreUpdate, DeltaTime, TickCutoffId, EcsWorld, TransformWorld, Jobs );
	}

	{
		PROFILE_SCOPE_LAZY( ObjectsScope, FProfileScope( "Scene", ProfilerOwnerId, "Objects.Tick" ) );

		const int32 NumToTick = Objects.Num();
		for ( int32 i = 0; i < NumToTick; ++i )
		{
			// Tick 中の SpawnObject で配列が再確保されても大丈夫なよう、毎回インデックスで取り出す
			CGameObject* Object = Objects[i].Get();
			if ( Object->IsAlive() ) Object->TickObject( DeltaTime );
		}
	}

	{
		PROFILE_SCOPE_LAZY( SystemsScope, FProfileScope( "Scene", ProfilerOwnerId, "Systems.PostUpdate" ) );
		Systems.Run( NEcs::EPhase::PostUpdate, DeltaTime, TickCutoffId, EcsWorld, TransformWorld, Jobs );
	}

	{
		PROFILE_SCOPE_LAZY( PostScope, FProfileScope( "Scene", ProfilerOwnerId, "Subsystems.PostUpdate" ) );
		Subsystems.Tick( DeltaTime, ETickGroup::PostUpdate );
	}

	{
		PROFILE_SCOPE_LAZY( CleanupScope, FProfileScope( "Scene", ProfilerOwnerId, "RemoveDestroyedObjects" ) );
		RemoveDestroyedObjects();
	}
}

void CScene::DrawScene()
{
	// Tick されていないシーン（スタックの下にあるもの）も Draw されるので、ここでも ID を更新する
	ProfilerOwnerId = NProfiler::GRecorder ? NProfiler::GRecorder->InternString( *GetName() ) : 0u;
	PROFILE_SCOPE_LAZY( SceneScope, FProfileScope( "Scene", ProfilerOwnerId, "DrawScene" ) );

	if ( bDrawOrderDirty )
	{
		PROFILE_SCOPE_LAZY( SortScope, FProfileScope( "Scene", ProfilerOwnerId, "RebuildDrawList" ) );
		RebuildDrawList();
	}

	// 描画の前に、古くなったワールド値を、ジョブシステムで一斉に計算しておく（呼ばなくても、読む側が最新を計算するので結果は同じ）
	{
		PROFILE_SCOPE_LAZY( TransformScope, FProfileScope( "Scene", ProfilerOwnerId, "Transforms.Update" ) );
		TransformWorld.Update( GetAppSubsystem<IJobSystem>() );
	}

	// 描画システムがあれば、全オブジェクト分の描画を先に（並列に）記録しておく。実行は、下のループで、描画順に 1 つずつ
	IDrawExecutor* Executor = nullptr;
	if ( Systems.HasDrawSystems() )
	{
		Executor = GetDrawExecutor();
		if ( Executor != nullptr )
		{
			PROFILE_SCOPE_LAZY( RecordScope, FProfileScope( "Scene", ProfilerOwnerId, "Systems.Draw" ) );
			Systems.Run( NEcs::EPhase::Draw, 0.0f, ~0ull, EcsWorld, TransformWorld, GetAppSubsystem<IJobSystem>() );
		}
	}

	{
		PROFILE_SCOPE_LAZY( ObjectsScope, FProfileScope( "Scene", ProfilerOwnerId, "Objects.Draw" ) );
		for ( int32 i = 0; i < DrawList.Num(); ++i )
		{
			CGameObject* Object = DrawList[i];
			if ( !Object->IsAlive() || !Object->IsVisible() ) continue;

			Object->DrawObject();
			// そのオブジェクトの描画システムの出力は、Draw() の直後に流す
			if ( Executor != nullptr ) Systems.ReplayDraw( Object->GetEntitySlot(), *Executor );
		}
	}

	{
		PROFILE_SCOPE_LAZY( OnDrawScope, FProfileScope( "Scene", ProfilerOwnerId, "OnDraw" ) );
		OnDraw();
	}
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CScene::OnEnter()
{
}

void CScene::OnExit()
{
}

void CScene::OnPause()
{
}

void CScene::OnResume()
{
}

void CScene::OnTick( float32 DeltaTime )
{
}

void CScene::OnDraw()
{
}

#pragma endregion
