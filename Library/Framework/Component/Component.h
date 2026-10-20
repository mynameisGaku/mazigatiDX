#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Object/Object.h"

class CGameObject;
class CScene;

//=============================================================================
// CComponent
//   CGameObject に付けて機能を足す部品。所有者 (Owner) が TUniquePtr で所有する。
//   ITickable も継承すると、所有者の Tick に合わせて自動で Tick される
//   （PreUpdate なら所有者の Tick の前、PostUpdate なら後）。
//
//   class CHealthComponent : public CComponent
//   {
//   public:
//       void OnAttach() override { ... }
//   };
//
//   CHealthComponent* Health = Object->AddComponent<CHealthComponent>();
//   Health->Destroy();   // 取り外しは予約。所有者の Tick の後で OnDetach が呼ばれて破棄される
//=============================================================================
class CComponent : public CObject
{
	friend class CGameObject;

public:
	// AddComponent の中で呼ばれる（所有者がまだシーンに入っていない場合もある）
	virtual void OnAttach();
	// 取り外し時・所有者の破棄時
	virtual void OnDetach();
	// 所有者がシーンに入ったとき（AddComponent のときにまだ入っていなかったコンポーネントが、シーンの持つデータへ移る機会）
	virtual void OnOwnerSceneChanged( CScene* NewScene );
	// 取り外しが予約されたとき（Destroy() が呼ばれた直後に 1 回）
	virtual void OnDestroyRequested();

	FORCEINLINE CGameObject* GetOwner() const { return Owner; }

	// 取り外しを予約する
	void Destroy();
	FORCEINLINE bool IsPendingDestroy() const { return bPendingDestroy; }

private:
	CGameObject* Owner = nullptr;
	bool bPendingDestroy = false;
};
