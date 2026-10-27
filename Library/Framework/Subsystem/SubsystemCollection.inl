#pragma once

#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Utility.h"

// SubsystemCollection.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// SubsystemCollection.h の末尾から include される。直接 include しない

template<typename TBase, typename TOuter>
void FSubsystemCollection::Initialize( TOuter& Outer )
{
	Deinitialize();

	TArray<NSubsystemRegistry::TEntry<TBase>> Registered = NSubsystemRegistry::GetEntries<TBase>();
	// 登録順は .cpp のリンク順に左右されるので、クラス名順にして毎回同じ順番にする
	Registered.StableSort( []( const auto& A, const auto& B ) { return NCString::Compare( A.Type->name(), B.Type->name() ) < 0; } );

	for ( const NSubsystemRegistry::TEntry<TBase>& Entry : Registered )
	{
		TUniquePtr<TBase> Subsystem = Entry.Factory();
		Subsystem->SetOuter( &Outer );
		if ( !Subsystem->ShouldCreateSubsystem( Outer ) )
		{
			NotifySkipped( *Subsystem );
			continue;
		}
		Entries.Add( { TUniquePtr<CSubsystem>( MoveTemp( Subsystem ) ), EState::Uninitialized } );
	}

	InitializeAll();
}