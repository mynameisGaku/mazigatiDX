// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// TUIEvent<Args...>
//   UI のイベント（クリック・値の変更など）。複数の受け手を登録でき、Broadcast で、登録順に呼ぶ。
//
//   Button->OnClicked.Add( []( CUIWidget& Source ) { ... } );
//   const int32 Handle = Button->OnClicked.Add( Handler );   // 戻り値で、あとから外せる
//   Button->OnClicked.Remove( Handle );
//
//   - 呼んでいる最中に、受け手が自分や他の受け手を Add / Remove しても安全（呼ぶ前に、受け手の一覧を複写する。
//     呼んでいる間に Add されたものは、その回は呼ばれない。Remove されたものも、その回は、呼ばれる）。
//=============================================================================
template<typename... Args>
class TUIEvent
{
public:
	using FHandler = TCallable<void( Args... )>;

	// 受け手を足す。戻り値は、Remove に使う番号（1 以上）。空の受け手は足さず、0 を返す
	int32 Add( const FHandler& Handler )
	{
		if ( !Handler )
		{
			return 0;
		}
		FEntry Entry;
		Entry.Id = NextId++;
		Entry.Handler = Handler;
		Entries.Add( Entry );
		return Entry.Id;
	}

	// 番号の受け手を外す。なければ false
	bool Remove( int32 Id )
	{
		for ( int32 Index = 0; Index < Entries.Num(); ++Index )
		{
			if ( Entries[Index].Id == Id )
			{
				Entries.RemoveAt( Index );
				return true;
			}
		}
		return false;
	}

	void Clear() { Entries.Empty(); }
	int32 Num() const { return Entries.Num(); }
	bool IsBound() const { return !Entries.IsEmpty(); }

	// 登録順に、すべての受け手を呼ぶ
	void Broadcast( Args... Arguments ) const
	{
		TArray<FEntry> Snapshot = Entries;
		for ( int32 Index = 0; Index < Snapshot.Num(); ++Index )
		{
			Snapshot[Index].Handler( Arguments... );
		}
	}

private:
	struct FEntry
	{
		int32 Id = 0;
		FHandler Handler;
	};

	TArray<FEntry> Entries;
	int32 NextId = 1;
};
