#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ETickGroup
//   所有者（エンジン・ゲームインスタンス・シーン）の更新処理に対して、いつ Tick されるか
//=============================================================================
enum class ETickGroup : uint8
{
	PreUpdate,   // 所有者の更新より前（入力の取得など）
	PostUpdate,  // 所有者の更新より後（物理シミュレーションなど）
};

//=============================================================================
// ITickable
//   サブシステムがこれを継承すると、所属するコレクションから毎フレーム Tick される。
//
//   class CTimerSubsystem : public CGameInstanceSubsystem, public ITickable
//   {
//   public:
//       void Tick( float32 DeltaTime ) override { ... }
//   };
//=============================================================================
class ITickable
{
public:
	virtual ~ITickable() = default;

	virtual void Tick( float32 DeltaTime ) = 0;

	// false の間は Tick されない
	virtual bool IsTickable() const;
	virtual ETickGroup GetTickGroup() const;
};
