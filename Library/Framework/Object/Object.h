#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"

class IPropertyInspector;

//=============================================================================
// CObject
//   フレームワークが扱うオブジェクト（シーン・ゲームオブジェクトなど）の共通基底。
//   名前・型名・型判定だけを持つ。寿命は TSharedPtr / TUniquePtr で管理するので
//   IUnknown のような独自の参照カウントは持たない。
//
//   if ( CPlayer* Player = Cast<CPlayer>( Object ) ) ...
//   if ( Object->IsA<CEnemy>() ) ...
//=============================================================================
class CObject
{
public:
	CObject() = default;
	virtual ~CObject() = default;

	// 同一性が重要なのでコピー・ムーブは禁止
	CObject( const CObject& ) = delete;
	CObject& operator=( const CObject& ) = delete;

	// 名前が未設定なら型名を返す
	FORCEINLINE FString GetName() const { return Name.IsEmpty() ? GetTypeName() : Name; }
	FORCEINLINE void SetName( const FString& InName ) { Name = InName; ++NameRevision; }

	// SetName で名前が付けられているか（false なら GetName() は型名）
	FORCEINLINE bool HasCustomName() const { return !Name.IsEmpty(); }
	// SetName のたびに増える。名前から作ったキャッシュ（プロファイラーの表示名など）の更新判定に使う
	FORCEINLINE uint32 GetNameRevision() const { return NameRevision; }

	// "CPlayer" のようなクラス名（デバッグ表示用）
	FString GetTypeName() const;

	template<typename T>
	FORCEINLINE bool IsA() const { return dynamic_cast<const T*>( this ) != nullptr; }

	// デバッグ表示用にプロパティを列挙する（派生クラスで override し、基底も呼ぶ）
	virtual void InspectProperties( IPropertyInspector& Inspector );

private:
	FString Name;
	uint32 NameRevision = 0;
};

//=============================================================================
// Cast<T>
//   CObject 派生同士の安全なダウンキャスト。失敗すると nullptr
//=============================================================================
template<typename To, typename From>
FORCEINLINE To* Cast( From* Object )
{
	static_assert( bIsBaseOf<CObject, To>, "Cast<T>: T must derive from CObject" );
	return dynamic_cast<To*>( Object );
}

template<typename To, typename From>
FORCEINLINE const To* Cast( const From* Object )
{
	static_assert( bIsBaseOf<CObject, To>, "Cast<T>: T must derive from CObject" );
	return dynamic_cast<const To*>( Object );
}
