#pragma once
#include "Library/Core/Collection/String.h"

class CScene;
class CGameInstance;

//=============================================================================
// ISceneManager
//   シーンから見たシーン管理。シーン側はこれだけを知っていればよい
//   （テストでは差し替えて遷移要求だけを記録する、といった使い方ができる）。
//
//   ChangeScene / PushScene / PopScene は予約のみで、次の Tick の先頭で反映される。
//=============================================================================
class ISceneManager
{
public:
	virtual ~ISceneManager() = default;

	// 積まれているシーンをすべて終了し、Name だけにする
	virtual void ChangeScene( const FString& Name ) = 0;
	// Name を一番上に積む（下のシーンは OnPause され、Tick が止まる）
	virtual void PushScene( const FString& Name ) = 0;
	// 一番上のシーンを終了する（下のシーンは OnResume される）
	virtual void PopScene() = 0;

	// 一番上のシーン（無ければ nullptr）
	virtual CScene* GetCurrentScene() const = 0;
	// 所有しているゲームインスタンス（単体で使っているなら nullptr）
	virtual CGameInstance* GetGameInstance() const = 0;
};
