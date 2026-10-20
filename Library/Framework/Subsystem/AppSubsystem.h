#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Subsystem/Subsystem.h"

class CEngine;

//=============================================================================
// CAppSubsystem
//   アプリケーション（CEngine::Run）の開始から終了まで存在するサブシステム。
//   プラットフォーム・入力・オーディオなど、ゲームの状態に関係なく常に必要なもの。
//
//   Foo* Foo = GetAppSubsystem<Foo>();   // どこからでも取得できる（Engine.h）
//=============================================================================
class CAppSubsystem : public CSubsystem
{
	friend class FSubsystemCollection;

public:
	using FSubsystemBase = CAppSubsystem;

	// false を返すと生成されない
	virtual bool ShouldCreateSubsystem( CEngine& Engine ) const;

	FORCEINLINE CEngine* GetEngine() const { return Engine; }

private:
	FORCEINLINE void SetOuter( CEngine* InEngine ) { Engine = InEngine; }

	CEngine* Engine = nullptr;
};
