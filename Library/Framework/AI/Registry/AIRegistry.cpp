// SPDX-License-Identifier: MIT
#include "AIRegistry.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"

void CAIRegistry::AddAlias( const char* OldName, const char* Name )
{
	Aliases.Add( FString( OldName ), FString( Name ) );
}

FString CAIRegistry::ResolveName( const FString& Name ) const
{
	// 別名の連鎖を辿る（循環しても止まるように、回数を限る）
	FString Current = Name;
	for ( int32 Depth = 0; Depth < 8; ++Depth )
	{
		const FString* Next = Aliases.Find( Current );
		if ( Next == nullptr )
		{
			break;
		}
		Current = *Next;
	}
	return Current;
}

const CAIRegistry::FStateActionEntry* CAIRegistry::FindStateAction( const FString& Name ) const
{
	const int32* Index = StateActionIndex.Find( ResolveName( Name ) );
	return Index != nullptr ? &StateActions[*Index] : nullptr;
}

const CAIRegistry::FConditionEntry* CAIRegistry::FindCondition( const FString& Name ) const
{
	const int32* Index = ConditionIndex.Find( ResolveName( Name ) );
	return Index != nullptr ? &Conditions[*Index] : nullptr;
}

void CAIRegistry::Clear()
{
	StateActions.Empty();
	StateActionIndex.Empty();
	Conditions.Empty();
	ConditionIndex.Empty();
	Aliases.Empty();
}

void CAIRegistry::NotifyDuplicate( const FString& Name )
{
	LOG( LogAI, Warning, "The AI part '%s' was registered twice. The later one replaced the earlier one.", *Name );
}

CAIRegistry& NAI::GetRegistry()
{
	static CAIRegistry Registry;
	return Registry;
}
