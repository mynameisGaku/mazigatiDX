// SPDX-License-Identifier: MIT

#include "Library/DebugUI/WorldEditorModelPreviewKey.h"

#include "Library/Core/Collection/Hash.h"

bool FWorldEditorModelPreviewKey::operator==( const FWorldEditorModelPreviewKey& Other ) const
{
	if ( ObjectIds.Num() != Other.ObjectIds.Num() ) return false;
	for ( int32 Index = 0; Index < ObjectIds.Num(); ++Index ) if ( ObjectIds[Index] != Other.ObjectIds[Index] ) return false;
	return true;
}

uint32 GetTypeHash( const FWorldEditorModelPreviewKey& Key )
{
	uint32 Hash = GetTypeHash( Key.ObjectIds.Num() ); // 経路長もhashへ含める初期値
	for ( const int32 ObjectId : Key.ObjectIds ) Hash = HashCombine( Hash, GetTypeHash( ObjectId ) );
	return Hash;
}
