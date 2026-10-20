#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <type_traits>

#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Engine/TimeControl.h"
#include "Library/Framework/GameInstance/GameInstance.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/WindowSettings.h"

//=============================================================================
// CEngine
//   アプリケーション全体の実行役。CAppSubsystem を所有し、メインループを回す。
//   プラットフォームは IPlatform を実装した CAppSubsystem から取得するので、
//   エンジン自体は DxLib などの実装を知らない。
//
//   // WinMain
//   CEngine Engine;
//   return Engine.Run<CMyGame>( Settings );
//
//   起動: CAppSubsystem 生成 -> ゲームインスタンス Init
//   毎フレーム: IPlatform::BeginFrame -> サブシステム (PreUpdate) -> ゲーム Tick
//               -> サブシステム (PostUpdate) -> ゲーム Draw -> IPlatform::EndFrame
//   終了: ゲームインスタンス Term -> CAppSubsystem 破棄
//
//   実行中は CEngine::Get() / GetAppSubsystem<T>() でどこからでも取得できる。
//   このプロジェクトで唯一のグローバルな入口（他のシングルトンはサブシステムに置き換える）。
//=============================================================================
class CEngine
{
public:
	CEngine() = default;
	~CEngine();

	CEngine( const CEngine& ) = delete;
	CEngine& operator=( const CEngine& ) = delete;

	// Run の実行中だけ有効
	FORCEINLINE static CEngine* Get() { return RunningEngine; }

	//-------------------------------------------------------------------------
	// 実行
	//-------------------------------------------------------------------------
	template<typename TGameInstance>
	FORCEINLINE int32 Run( const FWindowSettings& InStartupSettings )
	{
		static_assert( std::is_base_of_v<CGameInstance, TGameInstance>, "Run<T>: T must derive from CGameInstance" );
		return Run( MakeUnique<TGameInstance>(), InStartupSettings );
	}

	// 戻り値は終了コード
	int32 Run( TUniquePtr<CGameInstance> InGameInstance, const FWindowSettings& InStartupSettings );

	// 起動時のウィンドウ設定（IPlatform の実装が初期化に使う）
	FORCEINLINE const FWindowSettings& GetStartupSettings() const { return StartupSettings; }

	//-------------------------------------------------------------------------
	// 参照
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE T* GetSubsystem() const { return Subsystems.Get<T>(); }
	FORCEINLINE const FSubsystemCollection& GetSubsystems() const { return Subsystems; }

	FORCEINLINE CGameInstance* GetGameInstance() const { return GameInstance.Get(); }

	// ゲームの時間の進め方（スケール・一時停止・コマ送り。何も設定しなければ、ふつうに進む）
	FORCEINLINE FEngineTimeControl& GetTimeControl() { return TimeControl; }
	FORCEINLINE const FEngineTimeControl& GetTimeControl() const { return TimeControl; }

private:
	static CEngine* RunningEngine;

	FWindowSettings StartupSettings;
	FSubsystemCollection Subsystems;
	TUniquePtr<CGameInstance> GameInstance;
	FEngineTimeControl TimeControl;
};

//=============================================================================
// どこからでも使える取得関数（エンジンが動いていなければ nullptr）
//=============================================================================
template<typename T>
FORCEINLINE T* GetAppSubsystem()
{
	CEngine* Engine = CEngine::Get();
	return Engine ? Engine->GetSubsystem<T>() : nullptr;
}

template<typename T>
FORCEINLINE T* GetGameInstanceSubsystem()
{
	CEngine* Engine = CEngine::Get();
	CGameInstance* GameInstance = Engine ? Engine->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<T>() : nullptr;
}
