// SPDX-License-Identifier: MIT
#include "AIHotReload.h"

#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	FString Normalize( const FString& Path )
	{
		return Path.Replace( "\\", "/" );
	}
}

int32 NAI::ReloadObject( CGameObject& Object )
{
	FAIReloadContext Context;
	int32 Count = 0;
	if ( CStateMachineComponent* Machine = Object.GetComponent<CStateMachineComponent>() )
	{
		Count += Machine->Reload( Context ) ? 1 : 0;
	}
	if ( CBehaviorTreeComponent* Tree = Object.GetComponent<CBehaviorTreeComponent>() )
	{
		Count += Tree->Reload( Context ) ? 1 : 0;
	}
	return Count;
}

int32 NAI::ReloadAsset( CScene& Scene, const FString& Path )
{
	const FString Wanted = Normalize( Path );
	int32 Count = 0;
	for ( const TSharedPtr<CGameObject>& Object : Scene.GetObjects() )
	{
		if ( !Object.IsValid() )
		{
			continue;
		}
		CStateMachineComponent* Machine = Object->GetComponent<CStateMachineComponent>();
		CBehaviorTreeComponent* Tree = Object->GetComponent<CBehaviorTreeComponent>();
		const bool bMachineUses = Machine != nullptr && ( Normalize( Machine->GetPath() ) == Wanted || Normalize( Machine->GetBlackboardPath() ) == Wanted );
		const bool bTreeUses = Tree != nullptr && ( Normalize( Tree->GetPath() ) == Wanted || Normalize( Tree->GetBlackboardPath() ) == Wanted );
		if ( bMachineUses || bTreeUses )
		{
			Count += ReloadObject( *Object );
		}
	}
	return Count;
}
