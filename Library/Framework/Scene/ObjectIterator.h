// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_SCENE_OBJECT_ITERATOR_H
#define FRAMEWORK_SCENE_OBJECT_ITERATOR_H


#include "Library/Core/Types/TypeTraits.h"
#include "Library/Framework/Scene/Scene.h"

// シーンで生存中の指定型と派生型を生成順に走査する。作成後の追加は含めない。
template<typename T>
class TObjectIterator
{
	static_assert( bIsBaseOf<CGameObject, TRemoveConstT<T>>, "TObjectIterator<T> requires a CGameObject type" );

public:
	// シーンは走査終了まで生存させる。走査中は Destroy 予約と SpawnObject を使える。
	FORCEINLINE explicit TObjectIterator( const CScene& Scene ) : Objects( Scene.GetObjects() ), Limit( Objects.Num() ) { SkipInvalid(); }
	// 現在の生存オブジェクトを返す。終端での参照は不正。
	FORCEINLINE T& operator*() const { return *operator->(); }
	// 現在の生存オブジェクトを返す。終端での参照は不正。
	FORCEINLINE T* operator->() const
	{
		COLLECTION_CHECK( static_cast<bool>( *this ), "TObjectIterator has no current object" );
		return static_cast<T*>( Objects[Index].Get() );
	}
	// 次の生存する指定型へ進む。
	FORCEINLINE TObjectIterator& operator++() { if ( Index < Limit ) ++Index; SkipInvalid(); return *this; }
	// 現在のオブジェクトが生存中かを返す。
	FORCEINLINE explicit operator bool() const { return Index < Limit && Index < Objects.Num() && Objects[Index]->IsAlive(); }
	// 元のシーン配列内の位置を返す。
	FORCEINLINE int32 GetIndex() const { return Index; }

private:
	// 破棄予約済みと別の型を飛ばす。
	FORCEINLINE void SkipInvalid()
	{
		while ( Index < Limit && Index < Objects.Num() )
		{
			if ( Objects[Index]->IsAlive() && Objects[Index]->template IsA<TRemoveConstT<T>>() ) break;
			++Index;
		}
	}
	// シーンの配列本体を参照し、要素の再確保に影響されない。
	const TArray<TSharedPtr<CGameObject>>& Objects;
	// 作成時点の件数。
	int32 Limit;
	// 現在のシーン内位置。
	int32 Index = 0;
};

#endif
