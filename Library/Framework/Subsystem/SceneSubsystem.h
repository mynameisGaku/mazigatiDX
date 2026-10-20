#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Subsystem/Subsystem.h"

class CScene;

//=============================================================================
// CSceneSubsystem
//   シーンの開始 (OnEnter の前) から終了 (OnExit とオブジェクト破棄の後) まで存在するサブシステム。
//   シーンごとに独立していてほしいもの（物理ワールド・敵の出現管理など）。
//   Tick されるのはシーンが Tick されている間（スタックの一番上）だけ。
//
//   GetScene()->GetSubsystem<Foo>()
//=============================================================================
class CSceneSubsystem : public CSubsystem
{
	friend class FSubsystemCollection;

public:
	using FSubsystemBase = CSceneSubsystem;

	// false を返すと生成されない（特定のシーンでだけ使いたい場合など）
	virtual bool ShouldCreateSubsystem( CScene& Scene ) const;

	FORCEINLINE CScene* GetScene() const { return Scene; }

private:
	FORCEINLINE void SetOuter( CScene* InScene ) { Scene = InScene; }

	CScene* Scene = nullptr;
};
