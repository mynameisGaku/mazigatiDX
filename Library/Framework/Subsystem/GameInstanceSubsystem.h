#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Subsystem/Subsystem.h"

class CGameInstance;

//=============================================================================
// CGameInstanceSubsystem
//   ゲームインスタンスの開始から終了まで存在するサブシステム。
//   シーンをまたいで保持したいもの（スコア・セーブデータ・進行状況など）。
//
//   GetGameInstance()->GetSubsystem<Foo>()  /  GetGameInstanceSubsystem<Foo>()（Engine.h）
//=============================================================================
class CGameInstanceSubsystem : public CSubsystem
{
	friend class FSubsystemCollection;

public:
	using FSubsystemBase = CGameInstanceSubsystem;

	// false を返すと生成されない
	virtual bool ShouldCreateSubsystem( CGameInstance& GameInstance ) const;

	FORCEINLINE CGameInstance* GetGameInstance() const { return GameInstance; }

private:
	FORCEINLINE void SetOuter( CGameInstance* InGameInstance ) { GameInstance = InGameInstance; }

	CGameInstance* GameInstance = nullptr;
};
