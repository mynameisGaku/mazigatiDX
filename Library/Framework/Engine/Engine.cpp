#include "Engine.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogEngine, Log )

CEngine* CEngine::RunningEngine = nullptr;

CEngine::~CEngine()
{
	if ( RunningEngine == this ) RunningEngine = nullptr;
}

int32 CEngine::Run( TUniquePtr<CGameInstance> InGameInstance, const FWindowSettings& InStartupSettings )
{
	if ( RunningEngine != nullptr )
	{
		LOG( LogEngine, Error, "Another engine is already running." );
		return -1;
	}
	if ( !InGameInstance )
	{
		LOG( LogEngine, Error, "Run called without a game instance." );
		return -1;
	}

	RunningEngine = this;
	StartupSettings = InStartupSettings;

	LOG( LogEngine, Log, "Starting: game instance %s, window %dx%d (%s), vsync %s, max fps %d",
		 *InGameInstance->GetName(), StartupSettings.Width, StartupSettings.Height, WindowModeToString( StartupSettings.WindowMode ),
		 StartupSettings.bVSync ? "on" : "off", StartupSettings.MaxFPS );

	Subsystems.SetLabel( "App" );
	Subsystems.Initialize<CAppSubsystem>( *this );
	LOG( LogEngine, Log, "%d app subsystem(s) ready", Subsystems.GetAll().Num() );

	IPlatform* Platform = GetSubsystem<IPlatform>();
	if ( Platform == nullptr || !Platform->IsInitialized() )
	{
		LOG( LogEngine, Error, "No platform is available. Register a CAppSubsystem that implements IPlatform." );
		Subsystems.Deinitialize();
		RunningEngine = nullptr;
		return -1;
	}

	// 無くてもよい（無ければ計測しない）
	IProfiler* Profiler = GetSubsystem<IProfiler>();
	if ( Profiler == nullptr ) LOG( LogEngine, Warning, "No profiler is available. Profiling is disabled." );

	GameInstance = MoveTemp( InGameInstance );
	GameInstance->Init( this );

	const int64 StartTick = NPlatformTime::GetTicks();
	const uint64 StartFrameCount = Platform->GetFrameCount();
	LOG( LogEngine, Log, "Main loop started" );

	for ( ;; )
	{
		// 前のフレームの計測を確定し、このフレームの記録を始める（Platform.BeginFrame も計測に含めるため最初に呼ぶ）
		if ( Profiler != nullptr ) Profiler->BeginFrame();

		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "Platform.BeginFrame" );
			if ( !Platform->BeginFrame() ) break;
		}
		const float32 DeltaTime = TimeControl.Apply( Platform->GetDeltaTime() );

		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "AppSubsystems.PreUpdate" );
			Subsystems.Tick( DeltaTime, ETickGroup::PreUpdate );
		}
		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "GameInstance.Tick" );
			GameInstance->Tick( DeltaTime );
		}
		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "AppSubsystems.PostUpdate" );
			Subsystems.Tick( DeltaTime, ETickGroup::PostUpdate );
		}
		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "GameInstance.Draw" );
			GameInstance->Draw();
		}
		{
			PROFILE_SCOPE_OWNER( "Engine", "Engine", "Platform.EndFrame" );
			Platform->EndFrame();
		}
	}

	const float64 ElapsedSeconds = NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - StartTick ) / 1000.0;
	const uint64 NumFrames = Platform->GetFrameCount() - StartFrameCount;
	LOG( LogEngine, Log, "Main loop ended: %llu frame(s) in %.1f s (average %.1f FPS)", static_cast<unsigned long long>( NumFrames ),
		 ElapsedSeconds, ElapsedSeconds > 0.0 ? static_cast<float64>( NumFrames ) / ElapsedSeconds : 0.0 );

	GameInstance->Term();
	GameInstance.Reset();

	Subsystems.Deinitialize();
	LOG( LogEngine, Log, "Stopped" );
	RunningEngine = nullptr;
	return 0;
}
