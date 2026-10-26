#pragma once
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Memory/SmartPointer.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Framework/Scene/ISceneManager.h"
#include "Library/Framework/Scene/Scene.h"

DECLARE_LOG_CATEGORY_EXTERN( LogScene, Log )

//=============================================================================
// CSceneManager
//   シーンを名前で登録し、切り替え・積み重ね（ポーズメニューなど）を管理する。
//   DxLib に依存しないので、DeltaTime を渡せば単体でも動かせる。
//
//   SceneManager.RegisterScene<CTitleScene>( "Title" );
//   SceneManager.RegisterScene<CGameScene>( "Game" );
//   SceneManager.RegisterScene<CPauseScene>( "Pause" );
//   SceneManager.ChangeScene( "Title" );
//
//   // 毎フレーム
//   SceneManager.Tick( DeltaTime );
//   SceneManager.Draw();
//
//   - ChangeScene / PushScene / PopScene は予約のみ。次の Tick の先頭でまとめて反映する
//     （シーンやオブジェクトの Tick の途中で自分自身が破棄されないようにするため）
//   - Tick されるのは一番上のシーンだけ。Draw は下から順に全シーン
//=============================================================================
class CSceneManager : public ISceneManager
{
public:
	FORCEINLINE explicit CSceneManager( CGameInstance* InGameInstance = nullptr ) : GameInstance( InGameInstance ) {}
	virtual ~CSceneManager();

	CSceneManager( const CSceneManager& ) = delete;
	CSceneManager& operator=( const CSceneManager& ) = delete;

	//-------------------------------------------------------------------------
	// 登録
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE void RegisterScene( const FString& Name )
	{
		static_assert( bIsBaseOf<CScene, T>, "RegisterScene<T>: T must derive from CScene" );
		static_assert( bIsDefaultConstructible<T>, "RegisterScene<T>: T must be default constructible" );
		RegisterSceneFactory( Name, []() -> TUniquePtr<CScene> { return MakeUnique<T>(); } );
	}

	FORCEINLINE bool IsSceneRegistered( const FString& Name ) const { return Factories.Contains( Name ); }
	TArray<FString> GetRegisteredSceneNames() const;

	//-------------------------------------------------------------------------
	// 切り替え（予約）
	//-------------------------------------------------------------------------
	void ChangeScene( const FString& Name ) override;
	void PushScene( const FString& Name ) override;
	void PopScene() override;

	FORCEINLINE bool HasPendingRequests() const { return !Requests.IsEmpty(); }

	//-------------------------------------------------------------------------
	// 実行
	//-------------------------------------------------------------------------
	void Tick( float32 DeltaTime );
	void Draw();
	// すべてのシーンを終了する（予約も破棄する）
	void Shutdown();

	//-------------------------------------------------------------------------
	// 参照
	//-------------------------------------------------------------------------
	CScene* GetCurrentScene() const override;
	CGameInstance* GetGameInstance() const override;

	template<typename T>
	FORCEINLINE T* GetCurrentScene() const { return Cast<T>( GetCurrentScene() ); }

	// 下から順
	FORCEINLINE const TArray<TUniquePtr<CScene>>& GetSceneStack() const { return SceneStack; }

private:
	using FSceneFactory = TUniquePtr<CScene> ( * )();

	enum class ERequestType : uint8
	{
		Change,
		Push,
		Pop,
	};

	struct FRequest
	{
		ERequestType Type;
		FString Name;
	};

	void RegisterSceneFactory( const FString& Name, FSceneFactory Factory );
	void ApplyRequests();
	TUniquePtr<CScene> CreateScene( const FString& Name ) const;
	void PushSceneNow( TUniquePtr<CScene> Scene );
	void PopSceneNow();

private:
	CGameInstance* GameInstance = nullptr;
	TMap<FString, FSceneFactory> Factories;
	TArray<TUniquePtr<CScene>> SceneStack;
	TArray<FRequest> Requests;
};
