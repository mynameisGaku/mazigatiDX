#include "Subsystem.h"

#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Framework/Subsystem/GameInstanceSubsystem.h"
#include "Library/Framework/Subsystem/SceneSubsystem.h"

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

bool CAppSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return true;
}

bool CGameInstanceSubsystem::ShouldCreateSubsystem( CGameInstance& GameInstance ) const
{
	return true;
}

bool CSceneSubsystem::ShouldCreateSubsystem( CScene& Scene ) const
{
	return true;
}

void CSubsystem::Initialize( FSubsystemCollection& Collection )
{
}

void CSubsystem::Deinitialize()
{
}

#pragma endregion
