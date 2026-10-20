#include "Component.h"

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CComponent::OnAttach()
{
}

void CComponent::OnDetach()
{
}

void CComponent::OnOwnerSceneChanged( CScene* NewScene )
{
}

void CComponent::OnDestroyRequested()
{
}

void CComponent::Destroy()
{
	if ( bPendingDestroy ) return;
	bPendingDestroy = true;
	OnDestroyRequested();
}

#pragma endregion
