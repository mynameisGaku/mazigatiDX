#include "GameApplication.h"

#include <initializer_list>

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/ConsoleWindow.h"
#include "Library/DebugUI/ImGuiPropertyInspector.h"
#include "Library/DebugUI/LogCategoryPanel.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Console/BuiltinCommands.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Math/Math.h"
#include "Library/Physics/WorldPhysics.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Source/Application/StartupHook.h"
#include "Source/Scene/GameScene.h"
#include "Source/Scene/TitleScene.h"
#include "VendorModules/ImGui/imgui.h"

DEFINE_LOG_CATEGORY_STATIC( LogGame, Log )

namespace
{
	const char* GamepadTypeToString( EGamepadType Type )
	{
		switch ( Type )
		{
		case EGamepadType::Generic:      return "Generic";
		case EGamepadType::Standard:     return "Standard";
		case EGamepadType::Xbox360:      return "Xbox 360";
		case EGamepadType::XboxOne:      return "Xbox One/Series";
		case EGamepadType::PS3:          return "PS3";
		case EGamepadType::PS4:          return "PS4";
		case EGamepadType::PS5:          return "PS5";
		case EGamepadType::SwitchPro:    return "Switch Pro";
		case EGamepadType::JoyConLeft:   return "Joy-Con (L)";
		case EGamepadType::JoyConRight:  return "Joy-Con (R)";
		case EGamepadType::JoyConPair:   return "Joy-Con (L/R)";
		case EGamepadType::Switch2Pro:   return "Switch 2 Pro";
		case EGamepadType::JoyCon2Left:  return "Joy-Con 2 (L)";
		case EGamepadType::JoyCon2Right: return "Joy-Con 2 (R)";
		case EGamepadType::JoyCon2Pair:  return "Joy-Con 2 (L/R)";
		case EGamepadType::GameCube:     return "GameCube";
		case EGamepadType::Steam:        return "Steam";
		default:                         return "Unknown";
		}
	}

	const char* SensorToString( EGamepadSensor Sensor )
	{
		switch ( Sensor )
		{
		case EGamepadSensor::Accelerometer:      return "Accelerometer (m/s^2)";
		case EGamepadSensor::Gyroscope:          return "Gyroscope (rad/s)";
		case EGamepadSensor::AccelerometerLeft:  return "Left Accel (m/s^2)";
		case EGamepadSensor::GyroscopeLeft:      return "Left Gyro (rad/s)";
		case EGamepadSensor::AccelerometerRight: return "Right Accel (m/s^2)";
		case EGamepadSensor::GyroscopeRight:     return "Right Gyro (rad/s)";
		default:                                 return "Sensor";
		}
	}
}

void CGameApplication::OnInit()
{
	LogWindow = MakeShared<CImGuiLogSink>();
	CLogManager::Get().AddSink( LogWindow );

	// ワールド（.world）の Collider を、物理の剛体にできるようにする（Framework は Physics を知らないので、Physics 側の設置を登録する）。
	// 物理のあるシーン（IPhysicsScene）に置くときだけ、剛体になる
	NPhysicsWorld::RegisterSpawners( FWorldSpawnerRegistry::Global() );

	CSceneManager& Scenes = GetSceneManager();
	Scenes.RegisterScene<CTitleScene>( "Title" );
	Scenes.RegisterScene<CGameScene>( "Game" );
	// 起動時の窓口（自己確認など、別のリポジトリのものが差し込む）。--selftest=ecs / assets / world で起動したときは、そのシーンから始まる
	if ( !NStartupHook::Run( Scenes ) ) Scenes.ChangeScene( "Title" );

	// --export-models（.mv1 が無い・古いものだけ）/ --export-models=force（すべて作り直す）: アセットのルートフォルダの下の
	// .fbx / .obj / .x / .pmx などを .mv1 にして、そのまま終了する（アセット出力の自動化用。結果はログに出る）
	if ( const char* CommandLine = GetCommandLineA(); CommandLine != nullptr && NCString::Find( CommandLine, "--export-models" ) != nullptr )
	{
		if ( CAssetManager* Assets = GetAppSubsystem<CAssetManager>() )
		{
			const FModelBatchExportResult Batch = Assets->ExportAllModels( NCString::Find( CommandLine, "--export-models=force" ) != nullptr );
			LOG( LogGame, Log, "Exported the models in '%s' as .mv1: %s", *Assets->GetRootDirectory(), *Batch.GetSummary() );
		}
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->RequestExit();
	}

#if COG_DEBUG_CONSOLE
	// コンソール: 組み込みのコマンドを登録し、autoexec.cfg（あれば。作業フォルダ・exe の近く・Config フォルダから探す）を実行する。
	// 起動引数 --console で、最初から開いておく
	NConsole::RegisterBuiltinCommands( FConsoleRegistry::Global() );
	{
		FString ScriptError;
		int32 NumCommands = 0, NumFailed = 0;
		if ( !NConsole::FindScriptFile( "autoexec.cfg" ).IsEmpty() )
		{
			if ( NConsole::ExecuteScriptFile( FConsoleRegistry::Global(), "autoexec.cfg", NConsole::GetGlobalOutput(), ScriptError, &NumCommands, &NumFailed ) )
				LOG( LogGame, Log, "autoexec.cfg: %d command(s), %d failed", NumCommands, NumFailed );
			else LOG( LogGame, Warning, "autoexec.cfg: %s", *ScriptError );
		}
		if ( const char* CommandLine = GetCommandLineA(); CommandLine != nullptr && NCString::Find( CommandLine, "--console" ) != nullptr )
		{
			bShowConsole = true;
			ConsoleWindow.bFocusInput = true;
		}
	}
#endif

	LOG( LogGame, Log, "Game started" );
}

void CGameApplication::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr ) return;

	// F11 でフルスクリーン切り替え
	if ( Input->IsKeyPressed( EKey::F11 ) )
	{
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->ToggleFullscreen();
	}

	// F1 でデバッグウィンドウの表示切り替え
	if ( Input->IsKeyPressed( EKey::F1 ) )
	{
		bShowDebugWindow = !bShowDebugWindow;
		LOG( LogGame, Verbose, "Debug windows %s", bShowDebugWindow ? "shown" : "hidden" );
	}

	// F2 でプロファイラーの記録を切り替え（止めたあとも記録は残るので、画面で結果を見られる）
	if ( Input->IsKeyPressed( EKey::F2 ) ) ToggleProfiler();

#if COG_DEBUG_CONSOLE
	// F3 でコンソールの開閉（開いたら、入力欄にフォーカスを移す）
	if ( Input->IsKeyPressed( EKey::F3 ) )
	{
		bShowConsole = !bShowConsole;
		if ( bShowConsole ) ConsoleWindow.bFocusInput = true;
	}
#endif
}

void CGameApplication::ToggleProfiler()
{
	IProfiler* Profiler = GetAppSubsystem<IProfiler>();
	if ( Profiler == nullptr )
	{
		LOG( LogGame, Warning, "Cannot toggle the profiler: no profiler subsystem." );
		return;
	}

	Profiler->SetEnabled( !Profiler->IsEnabled() );
	if ( Profiler->IsEnabled() ) bShowProfilerWindow = true;
}

void CGameApplication::OnDraw()
{
	const IPlatform* Platform = GetAppSubsystem<IPlatform>();
	if ( Platform == nullptr ) return;

	const CScene* Scene = GetSceneManager().GetCurrentScene();
	DrawFormatString( 16, 16, FColor32::White.ToDxColor(), "FPS: %.1f   Mode: %s   Scene: %s   [F11] Fullscreen   [F1] Debug",
					  Platform->GetFPS(), WindowModeToString( Platform->GetWindowMode() ), Scene ? *Scene->GetName() : "-" );

	if ( bShowDebugWindow )
	{
		PROFILE_SCOPE_OWNER( "Debug", "Game", "DebugWindows" );
		DrawDebugWindow();
	}

	if ( bShowProfilerWindow )
	{
		if ( IProfiler* Profiler = GetAppSubsystem<IProfiler>() )
		{
			// 画面自体の描画コストも計測に出る（表示が重い場合は Debug/ProfilerWindow を見る）
			PROFILE_SCOPE_OWNER( "Debug", "Game", "ProfilerWindow" );
			NDebugUI::DrawProfilerWindow( *Profiler, ProfilerWindowState, &bShowProfilerWindow );
		}
	}

	{
		PROFILE_SCOPE_OWNER( "Debug", "Game", "AssetTools" );
		NDebugUI::DrawAssetTools( AssetTools );
	}

#if COG_DEBUG_CONSOLE
	if ( bShowConsole )
	{
		PROFILE_SCOPE_OWNER( "Debug", "Game", "Console" );
		NDebugUI::DrawConsoleWindow( ConsoleWindow, &bShowConsole );
	}
#endif
}

void CGameApplication::OnTerm()
{
	NDebugUI::ShutdownAssetTools( AssetTools );
	LOG( LogGame, Log, "Game finished" );
	CLogManager::Get().RemoveSink( LogWindow );
	LogWindow.Reset();
}

void CGameApplication::DrawDebugWindow()
{
	const ImVec2 MainOrigin = ImGui::GetMainViewport()->Pos;   // 初回の配置の基準（メインウィンドウの左上）
	IPlatform* Platform = GetAppSubsystem<IPlatform>();

	// 初めて開くときの配置（以降は ImGui が imgui.ini に覚えている位置と大きさを使う）。重ならないように並べる
	ImGui::SetNextWindowPos( ImVec2( MainOrigin.x + 16.0f, MainOrigin.y + 64.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( 360.0f, 430.0f ), ImGuiCond_FirstUseEver );
	if ( ImGui::Begin( "Window", &bShowDebugWindow ) )
	{
		ImGui::Text( "FPS: %.1f  (%.2f ms)", Platform->GetFPS(), Platform->GetDeltaTime() * 1000.0f );
		ImGui::Text( "Screen: %d x %d", Platform->GetScreenWidth(), Platform->GetScreenHeight() );

		const EWindowMode Current = Platform->GetWindowMode();
		for ( EWindowMode Mode : { EWindowMode::Windowed, EWindowMode::Borderless, EWindowMode::Fullscreen } )
		{
			if ( ImGui::RadioButton( WindowModeToString( Mode ), Current == Mode ) ) Platform->SetWindowMode( Mode );
			ImGui::SameLine();
		}
		ImGui::NewLine();

		// カーソルの表示はゲーム側が決める（フレームワークは変更しない）
		bool bCursorVisible = Platform->IsMouseCursorVisible();
		if ( ImGui::Checkbox( "Mouse Cursor", &bCursorVisible ) ) Platform->SetMouseCursorVisible( bCursorVisible );

		// プロファイラー: ボタン 1 つで記録のオン / オフ（F2 でも同じ）
		if ( const IProfiler* Profiler = GetAppSubsystem<IProfiler>() )
		{
			if ( ImGui::Button( Profiler->IsEnabled() ? "Profiler: ON   [F2]" : "Profiler: OFF  [F2]" ) ) ToggleProfiler();
			ImGui::SameLine();
			ImGui::Checkbox( "Show profiler window", &bShowProfilerWindow );
		}

		NDebugUI::DrawAssetToolsButtons( AssetTools );
#if COG_DEBUG_CONSOLE
		if ( ImGui::Checkbox( "Console  [F3]", &bShowConsole ) && bShowConsole ) ConsoleWindow.bFocusInput = true;
#endif

		if ( ImGui::CollapsingHeader( "Log categories" ) ) NDebugUI::DrawLogCategories();

		if ( ImGui::Button( "Exit" ) ) Platform->RequestExit();
	}
	ImGui::End();

	ImGui::SetNextWindowPos( ImVec2( MainOrigin.x + 392.0f, MainOrigin.y + 64.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( 520.0f, 90.0f ), ImGuiCond_FirstUseEver );
	if ( ImGui::Begin( "Input", &bShowDebugWindow ) )
	{
		if ( const IMouseInput* Mouse = GetAppSubsystem<IMouseInput>() )
		{
			const FMouseState& State = Mouse->GetMouseState();
			ImGui::Text( "Mouse: (%d, %d)  Delta: (%d, %d)  Wheel: (%.1f, %.1f)  Buttons: 0x%02X  Focused: %s",
						 State.X, State.Y, State.DeltaX, State.DeltaY, State.WheelX, State.WheelY,
						 State.Buttons, State.bFocused ? "Yes" : "No" );
		}

		ImGui::Separator();

		if ( IGamepadInput* Gamepads = GetAppSubsystem<IGamepadInput>() )
		{
			const TArray<FGamepadInfo>& Devices = Gamepads->GetDevices();
			ImGui::Text( "Gamepads: %d connected", Devices.Num() );

			for ( const FGamepadInfo& Info : Devices )
			{
				const FGamepadState* State = Gamepads->GetState( Info.Id );
				if ( State == nullptr ) continue;

				if ( !ImGui::TreeNodeEx( reinterpret_cast<const void*>( static_cast<uintptr>( Info.Id ) ),
										 ImGuiTreeNodeFlags_DefaultOpen,
										 "#%u %s [%s] (VID:0x%04X PID:0x%04X)",
										 Info.Id, *Info.Name, GamepadTypeToString( Info.Type ), Info.Vendor, Info.Product ) )
				{
					continue;
				}

				ImGui::Text( "Left Stick: (%.2f, %.2f)   Right Stick: (%.2f, %.2f)   Triggers: L=%.2f R=%.2f",
							 State->GetAxis( EGamepadAxis::LeftX ), State->GetAxis( EGamepadAxis::LeftY ),
							 State->GetAxis( EGamepadAxis::RightX ), State->GetAxis( EGamepadAxis::RightY ),
							 State->GetAxis( EGamepadAxis::LeftTrigger ), State->GetAxis( EGamepadAxis::RightTrigger ) );

				for ( int32 s = 0; s < static_cast<int32>( EGamepadSensor::Count ); ++s )
				{
					const EGamepadSensor SensorType = static_cast<EGamepadSensor>( s );
					const FGamepadSensorState& Sensor = State->Sensors[s];
					if ( !Sensor.bAvailable ) continue;

					bool bEnabled = Sensor.bEnabled;
					if ( ImGui::Checkbox( SensorToString( SensorType ), &bEnabled ) )
					{
						Gamepads->EnableSensor( Info.Id, SensorType, bEnabled );
					}
					if ( Sensor.bValid )
					{
						ImGui::SameLine();
						ImGui::Text( "(%.2f, %.2f, %.2f)", Sensor.Values[0], Sensor.Values[1], Sensor.Values[2] );
					}
				}

				if ( Info.bRumble && ImGui::Button( "Rumble" ) )
				{
					Gamepads->Rumble( Info.Id, 0.5f, 0.8f, 250 );
				}
				if ( Info.bTriggerRumble )
				{
					if ( Info.bRumble ) ImGui::SameLine();
					if ( ImGui::Button( "Trigger Rumble" ) )
					{
						Gamepads->RumbleTriggers( Info.Id, 0.6f, 0.6f, 250 );
					}
				}
				if ( Info.bLed )
				{
					if ( Info.bRumble || Info.bTriggerRumble ) ImGui::SameLine();
					if ( ImGui::Button( "LED Cyan" ) )
					{
						Gamepads->SetLed( Info.Id, 0, 200, 255 );
					}
				}

				ImGui::TreePop();
			}
		}
	}
	ImGui::End();

	ImGui::SetNextWindowPos( ImVec2( MainOrigin.x + 928.0f, MainOrigin.y + 64.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( 340.0f, 330.0f ), ImGuiCond_FirstUseEver );
	if ( ImGui::Begin( "Scene", &bShowDebugWindow ) )
	{
		CSceneManager& Scenes = GetSceneManager();

		// 登録済みシーンへ切り替え
		for ( const FString& Name : Scenes.GetRegisteredSceneNames() )
		{
			if ( ImGui::Button( *Name ) ) Scenes.ChangeScene( Name );
			ImGui::SameLine();
		}
		ImGui::NewLine();
		ImGui::Separator();

		// スタック（下から順）。オブジェクトは親子階層・コンポーネント付きで表示される
		for ( const TUniquePtr<CScene>& Scene : Scenes.GetSceneStack() )
		{
			if ( !ImGui::TreeNodeEx( Scene.Get(), ImGuiTreeNodeFlags_DefaultOpen, "%s  (%d objects, %.1fs)",
									 *Scene->GetName(), Scene->GetNumObjects(), Scene->GetElapsedTime() ) ) continue;

			NDebugUI::DrawSubsystems( "Scene Subsystems", Scene->GetSubsystems() );
			if ( ImGui::TreeNodeEx( "Objects", ImGuiTreeNodeFlags_DefaultOpen ) )
			{
				NDebugUI::DrawSceneHierarchy( *Scene );
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		ImGui::Separator();
		if ( const CEngine* Engine = GetEngine() ) NDebugUI::DrawSubsystems( "App Subsystems", Engine->GetSubsystems() );
		NDebugUI::DrawSubsystems( "GameInstance Subsystems", GetSubsystems() );
	}
	ImGui::End();

	if ( LogWindow )
	{
		ImGui::SetNextWindowPos( ImVec2( MainOrigin.x + 392.0f, MainOrigin.y + 170.0f ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( ImVec2( 520.0f, 300.0f ), ImGuiCond_FirstUseEver );
		LogWindow->Draw( "Output Log" );
	}
}
