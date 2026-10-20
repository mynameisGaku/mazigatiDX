#include "SceneManager.h"

#include "Library/Core/Profiler/ProfilerScope.h"

DEFINE_LOG_CATEGORY( LogScene )

namespace
{
	// OnEnter / OnExit の中で遷移を予約し続けた場合の無限ループ防止
	constexpr int32 MaxRequestsPerFrame = 16;
}

CSceneManager::~CSceneManager()
{
	Shutdown();
}

#pragma region Registration

void CSceneManager::RegisterSceneFactory( const FString& Name, FSceneFactory Factory )
{
	if ( Factories.Contains( Name ) )
	{
		LOG( LogScene, Warning, "Scene '%s' is already registered. Overwritten.", *Name );
	}
	Factories.Add( Name, Factory );
	LOG( LogScene, Verbose, "Registered scene '%s' (%d registered)", *Name, Factories.Num() );
}

TArray<FString> CSceneManager::GetRegisteredSceneNames() const
{
	TArray<FString> Names;
	for ( const auto& [Name, Factory] : Factories ) Names.Add( Name );
	Names.Sort();
	return Names;
}

TUniquePtr<CScene> CSceneManager::CreateScene( const FString& Name ) const
{
	const FSceneFactory* Factory = Factories.Find( Name );
	if ( Factory == nullptr ) return nullptr;

	TUniquePtr<CScene> Scene = ( *Factory )();
	Scene->SetName( Name );
	return Scene;
}

#pragma endregion

#pragma region Requests

void CSceneManager::ChangeScene( const FString& Name )
{
	Requests.Add( { ERequestType::Change, Name } );
	LOG( LogScene, Verbose, "ChangeScene requested: '%s' (applied at the start of the next Tick)", *Name );
}

void CSceneManager::PushScene( const FString& Name )
{
	Requests.Add( { ERequestType::Push, Name } );
	LOG( LogScene, Verbose, "PushScene requested: '%s' (applied at the start of the next Tick)", *Name );
}

void CSceneManager::PopScene()
{
	Requests.Add( { ERequestType::Pop, FString() } );
	LOG( LogScene, Verbose, "PopScene requested (applied at the start of the next Tick)" );
}

void CSceneManager::ApplyRequests()
{
	int32 NumApplied = 0;
	// OnEnter / OnExit の中で予約されたものも同じフレームで反映する
	while ( !Requests.IsEmpty() )
	{
		if ( NumApplied++ >= MaxRequestsPerFrame )
		{
			LOG( LogScene, Error, "Too many scene requests in one frame. Remaining requests were discarded." );
			Requests.Reset();
			break;
		}

		const FRequest Request = Requests[0];
		Requests.RemoveAt( 0 );

		switch ( Request.Type )
		{
		case ERequestType::Change:
		case ERequestType::Push:
		{
			TUniquePtr<CScene> Scene = CreateScene( Request.Name );
			if ( !Scene )
			{
				LOG( LogScene, Error, "Scene '%s' is not registered (registered: %s).", *Request.Name, *FString::Join( GetRegisteredSceneNames(), ", " ) );
				break;
			}
			if ( Request.Type == ERequestType::Change )
			{
				while ( !SceneStack.IsEmpty() ) PopSceneNow();
			}
			PushSceneNow( std::move( Scene ) );
			break;
		}

		case ERequestType::Pop:
			if ( SceneStack.IsEmpty() )
			{
				LOG( LogScene, Warning, "PopScene called with no scene." );
				break;
			}
			PopSceneNow();
			if ( CScene* Top = GetCurrentScene() )
			{
				LOG( LogScene, Verbose, "Resumed: %s", *Top->GetName() );
				Top->OnResume();
			}
			break;
		}
	}
}

void CSceneManager::PushSceneNow( TUniquePtr<CScene> Scene )
{
	if ( CScene* Top = GetCurrentScene() ) Top->OnPause();

	CScene* Raw = Scene.Get();
	SceneStack.Add( std::move( Scene ) );
	LOG( LogScene, Log, "Enter: %s (stack depth %d)", *Raw->GetName(), SceneStack.Num() );
	Raw->Enter( this );
}

void CSceneManager::PopSceneNow()
{
	CScene* Top = GetCurrentScene();
	LOG( LogScene, Log, "Exit: %s (ran %.1f s, %d object(s))", *Top->GetName(), Top->GetElapsedTime(), Top->GetNumObjects() );
	Top->Exit();
	SceneStack.Pop();
}

#pragma endregion

#pragma region Execution

void CSceneManager::Tick( float32 DeltaTime )
{
	if ( !Requests.IsEmpty() )
	{
		PROFILE_SCOPE_OWNER( "Scene", "SceneManager", "ApplyRequests" );
		ApplyRequests();
	}

	if ( CScene* Top = GetCurrentScene() ) Top->TickScene( DeltaTime );
}

void CSceneManager::Draw()
{
	for ( int32 i = 0; i < SceneStack.Num(); ++i )
	{
		SceneStack[i]->DrawScene();
	}
}

void CSceneManager::Shutdown()
{
	if ( !Requests.IsEmpty() ) LOG( LogScene, Warning, "Shutting down with %d scene request(s) still pending. They are discarded.", Requests.Num() );
	if ( !SceneStack.IsEmpty() ) LOG( LogScene, Log, "Shutting down: closing %d scene(s)", SceneStack.Num() );

	Requests.Reset();
	while ( !SceneStack.IsEmpty() ) PopSceneNow();
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

CScene* CSceneManager::GetCurrentScene() const
{
	return SceneStack.IsEmpty() ? nullptr : SceneStack.Last().Get();
}

CGameInstance* CSceneManager::GetGameInstance() const
{
	return GameInstance;
}

#pragma endregion
