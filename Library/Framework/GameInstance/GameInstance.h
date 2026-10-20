#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Object/Object.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

class CEngine;

//=============================================================================
// CGameInstance
//   ゲーム 1 回分の状態を持つ。ゲームごとにこれを継承し、CEngine::Run<T> に渡す。
//   シーン管理と CGameInstanceSubsystem を所有する。
//
//   class CMyGame : public CGameInstance
//   {
//   protected:
//       void OnInit() override
//       {
//           GetSceneManager().RegisterScene<CTitleScene>( "Title" );
//           GetSceneManager().ChangeScene( "Title" );
//       }
//   };
//
//   開始: サブシステム生成 -> OnInit        終了: 全シーン終了 -> OnTerm -> サブシステム破棄
//   Tick: サブシステム (PreUpdate) -> OnTick -> シーン -> サブシステム (PostUpdate)
//   Draw: シーン -> OnDraw（デバッグ UI など一番手前に出すもの）
//=============================================================================
class CGameInstance : public CObject
{
	friend class CEngine;

public:
	FORCEINLINE CGameInstance() : SceneManager( this ) {}
	virtual ~CGameInstance() = default;

	FORCEINLINE CEngine* GetEngine() const { return Engine; }

	FORCEINLINE CSceneManager& GetSceneManager() { return SceneManager; }
	FORCEINLINE const CSceneManager& GetSceneManager() const { return SceneManager; }

	// このゲームインスタンスのサブシステム（T はクラスでもインターフェースでもよい）
	template<typename T>
	FORCEINLINE T* GetSubsystem() const { return Subsystems.Get<T>(); }
	FORCEINLINE const FSubsystemCollection& GetSubsystems() const { return Subsystems; }

protected:
	//-------------------------------------------------------------------------
	// 派生クラスで実装するイベント
	//-------------------------------------------------------------------------
	virtual void OnInit();
	virtual void OnTick( float32 DeltaTime );
	virtual void OnDraw();
	virtual void OnTerm();

private:
	// CEngine から呼ばれる
	void Init( CEngine* InEngine );
	void Tick( float32 DeltaTime );
	void Draw();
	void Term();

private:
	CEngine* Engine = nullptr;
	CSceneManager SceneManager;
	FSubsystemCollection Subsystems;
};
