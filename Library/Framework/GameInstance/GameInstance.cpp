#include "GameInstance.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Framework/Subsystem/GameInstanceSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogGameInstance, Log )

void CGameInstance::Init( CEngine* InEngine )
{
	Engine = InEngine;
	LOG( LogGameInstance, Log, "Init: %s", *GetName() );

	Subsystems.SetLabel( "GameInstance" );
	Subsystems.Initialize<CGameInstanceSubsystem>( *this );
	LOG( LogGameInstance, Log, "%d game instance subsystem(s) ready", Subsystems.GetAll().Num() );

	OnInit();
}

void CGameInstance::Tick( float32 DeltaTime )
{
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "Subsystems.PreUpdate" );
		Subsystems.Tick( DeltaTime, ETickGroup::PreUpdate );
	}
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "OnTick" );
		OnTick( DeltaTime );
	}
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "SceneManager.Tick" );
		SceneManager.Tick( DeltaTime );
	}
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "Subsystems.PostUpdate" );
		Subsystems.Tick( DeltaTime, ETickGroup::PostUpdate );
	}
}

void CGameInstance::Draw()
{
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "SceneManager.Draw" );
		SceneManager.Draw();
	}
	{
		PROFILE_SCOPE_OWNER( "GameInstance", "GameInstance", "OnDraw" );
		OnDraw();
	}
}

void CGameInstance::Term()
{
	// シーンの OnExit からゲームインスタンスのサブシステムを使えるよう、シーンを先に終了する
	LOG( LogGameInstance, Log, "Term: %s", *GetName() );
	SceneManager.Shutdown();
	OnTerm();
	Subsystems.Deinitialize();
	Engine = nullptr;
}

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CGameInstance::OnInit()
{
}

void CGameInstance::OnTick( float32 DeltaTime )
{
}

void CGameInstance::OnDraw()
{
}

void CGameInstance::OnTerm()
{
}

#pragma endregion
