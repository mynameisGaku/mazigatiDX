#include "ITickable.h"

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

bool ITickable::IsTickable() const
{
	return true;
}

ETickGroup ITickable::GetTickGroup() const
{
	return ETickGroup::PreUpdate;
}

#pragma endregion
