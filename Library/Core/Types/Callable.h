// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <new>

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// TCallable<R( Args... )>
//   「引数 Args... で呼べて、R を返すもの」なら何でも入れられる入れ物。std::function の代わり。
//   関数・関数ポインター・ラムダ・関数オブジェクトを同じ型で持ち、コピーも移動もできる。空にもできる。
//   名前に TFunction を使わないのは、テンプレートの引数名として広く使われているため。
//
//   TCallable<int32( float32 )> Scale = []( float32 Value ) { return static_cast<int32>( Value * 2.0f ); };
//   if ( Scale ) { const int32 Result = Scale( 1.5f ); }       // 空のときに呼ぶのは不正
//   Scale = nullptr;                                          // 空にする
//
//   小さいもの（24 バイト以下で、例外を投げずに移動できるもの）は、この入れ物の中にそのまま置く。
//   それより大きいものは、ヒープに置く。どちらも、コピー・移動・破棄は中身に合わせて正しく行う。
//   R が void のときは、呼ぶものが何を返しても捨てる。R が void でなければ、R に変換できる値を返すものを入れられる。
//=============================================================================
template<typename Signature>
class TCallable;

template<typename R, typename... Args>
class TCallable<R( Args... )>
{
private:
	// FunctionType を Args... で呼んで R が得られるか。
	template<typename FunctionType, typename = void>
	struct TIsCallableAsImpl : TFalseType {};

	template<typename FunctionType>
	struct TIsCallableAsImpl<FunctionType, TVoidT<TInvokeResultT<FunctionType&, Args...>>> : TIntegralConstant<bool, bIsVoid<R> || bIsConvertible<TInvokeResultT<FunctionType&, Args...>, R>> {};

	template<typename FunctionType>
	using TIsCallableAs = TIsCallableAsImpl<FunctionType>;

public:
	// 何も入っていない状態。
	TCallable() noexcept = default;
	// 何も入っていない状態（nullptr から作る）。
	TCallable( decltype( nullptr ) ) noexcept {}

	// Function（Args... で呼べて、R に変換できる値を返すもの）をコピー・移動して持つ。TCallable 自身は対象外（コピー・移動の側が働く）。
	template<typename FunctionType, typename = TEnableIfT<!bIsSame<TDecayT<FunctionType>, TCallable> && TIsCallableAs<TDecayT<FunctionType>>::Value>>
	TCallable( FunctionType&& Function )
	{
		Construct<TDecayT<FunctionType>>( Forward<FunctionType>( Function ) );
	}

	// 中身もコピーする。
	TCallable( const TCallable& Other )
	{
		if ( Other.Ops != nullptr )
		{
			Other.Ops->Copy( Storage, Other.Storage );
			Ops = Other.Ops;
		}
	}

	// 中身を移し、元は空になる。
	TCallable( TCallable&& Other ) noexcept
	{
		if ( Other.Ops != nullptr )
		{
			Other.Ops->Move( Storage, Other.Storage );
			Ops = Other.Ops;
			Other.Ops = nullptr;
		}
	}

	// 今の中身を捨てて、Other の中身をコピーする。
	TCallable& operator=( const TCallable& Other )
	{
		if ( this != &Other )
		{
			TCallable Copy( Other );
			*this = MoveTemp( Copy );
		}
		return *this;
	}

	// 今の中身を捨てて、Other の中身を移す。Other は空になる。
	TCallable& operator=( TCallable&& Other ) noexcept
	{
		if ( this != &Other )
		{
			Reset();
			if ( Other.Ops != nullptr )
			{
				Other.Ops->Move( Storage, Other.Storage );
				Ops = Other.Ops;
				Other.Ops = nullptr;
			}
		}
		return *this;
	}

	// 空にする。
	TCallable& operator=( decltype( nullptr ) ) noexcept
	{
		Reset();
		return *this;
	}

	// 今の中身を捨てて、Function に入れ替える。
	template<typename FunctionType, typename = TEnableIfT<!bIsSame<TDecayT<FunctionType>, TCallable> && TIsCallableAs<TDecayT<FunctionType>>::Value>>
	TCallable& operator=( FunctionType&& Function )
	{
		*this = TCallable( Forward<FunctionType>( Function ) );
		return *this;
	}

	~TCallable() { Reset(); }

	// 何か入っているか。
	FORCEINLINE bool IsBound() const noexcept { return Ops != nullptr; }
	FORCEINLINE explicit operator bool() const noexcept { return Ops != nullptr; }

	// 中身を呼ぶ。空のときは呼べない（Debug では停止する）。中身が変わる（mutable なラムダなど）ものでも、const の入れ物から呼べる。
	R operator()( Args... InArgs ) const
	{
		assert( Ops != nullptr && "TCallable: called while empty" );
		return Ops->Invoke( Storage, Forward<Args>( InArgs )... );
	}

	// 空にして、中身を破棄する。
	void Reset() noexcept
	{
		if ( Ops != nullptr )
		{
			Ops->Destroy( Storage );
			Ops = nullptr;
		}
	}

	// nullptr との比較（空かどうか）。
	friend FORCEINLINE bool operator==( const TCallable& Callable, decltype( nullptr ) ) noexcept { return !Callable.IsBound(); }
	friend FORCEINLINE bool operator==( decltype( nullptr ), const TCallable& Callable ) noexcept { return !Callable.IsBound(); }
	friend FORCEINLINE bool operator!=( const TCallable& Callable, decltype( nullptr ) ) noexcept { return Callable.IsBound(); }
	friend FORCEINLINE bool operator!=( decltype( nullptr ), const TCallable& Callable ) noexcept { return Callable.IsBound(); }

private:
	// 中身の型ごとの操作の表。
	struct FOps
	{
		// 中身を呼ぶ。
		R ( *Invoke )( unsigned char* Object, Args... InArgs );
		// Source の中身を Dest へコピーして作る。
		void ( *Copy )( unsigned char* Dest, const unsigned char* Source );
		// Source の中身を Dest へ移して作り、Source の側は片付ける。
		void ( *Move )( unsigned char* Dest, unsigned char* Source ) noexcept;
		// 中身を破棄する。
		void ( *Destroy )( unsigned char* Object ) noexcept;
	};

	// 入れ物の中にそのまま置ける大きさ。
	static constexpr usize InlineSize = 3 * sizeof( void* );

	// FunctionType を入れ物の中にそのまま置けるか。
	template<typename FunctionType>
	static constexpr bool bFitsInline = sizeof( FunctionType ) <= InlineSize && alignof( FunctionType ) <= alignof( void* ) && bIsNothrowMoveConstructible<FunctionType>;

	// 入れ物の中に置く型の操作。
	template<typename FunctionType, bool bInline = bFitsInline<FunctionType>>
	struct TOps
	{
		static R Invoke( unsigned char* Object, Args... InArgs )
		{
			FunctionType& Function = *Launder( reinterpret_cast<FunctionType*>( Object ) );
			if constexpr ( bIsVoid<R> ) { Function( Forward<Args>( InArgs )... ); }
			else { return static_cast<R>( Function( Forward<Args>( InArgs )... ) ); }
		}
		static void Copy( unsigned char* Dest, const unsigned char* Source ) { new ( Dest ) FunctionType( *Launder( reinterpret_cast<const FunctionType*>( Source ) ) ); }
		static void Move( unsigned char* Dest, unsigned char* Source ) noexcept
		{
			FunctionType* Moved = Launder( reinterpret_cast<FunctionType*>( Source ) );
			new ( Dest ) FunctionType( MoveTemp( *Moved ) );
			Moved->~FunctionType();
		}
		static void Destroy( unsigned char* Object ) noexcept { Launder( reinterpret_cast<FunctionType*>( Object ) )->~FunctionType(); }
		static constexpr FOps Table = { &Invoke, &Copy, &Move, &Destroy };
	};

	// ヒープに置く型の操作（入れ物の中には、ヒープのアドレスだけを置く）。
	template<typename FunctionType>
	struct TOps<FunctionType, false>
	{
		static FunctionType*& Pointer( unsigned char* Object ) { return *Launder( reinterpret_cast<FunctionType**>( Object ) ); }
		static FunctionType* const& Pointer( const unsigned char* Object ) { return *Launder( reinterpret_cast<FunctionType* const*>( Object ) ); }
		static R Invoke( unsigned char* Object, Args... InArgs )
		{
			FunctionType& Function = *Pointer( Object );
			if constexpr ( bIsVoid<R> ) { Function( Forward<Args>( InArgs )... ); }
			else { return static_cast<R>( Function( Forward<Args>( InArgs )... ) ); }
		}
		static void Copy( unsigned char* Dest, const unsigned char* Source ) { new ( Dest ) FunctionType*( new FunctionType( *Pointer( Source ) ) ); }
		static void Move( unsigned char* Dest, unsigned char* Source ) noexcept { new ( Dest ) FunctionType*( Pointer( Source ) ); }
		static void Destroy( unsigned char* Object ) noexcept { delete Pointer( Object ); }
		static constexpr FOps Table = { &Invoke, &Copy, &Move, &Destroy };
	};

	// Function から中身を作る。
	template<typename DecayedType, typename FunctionType>
	void Construct( FunctionType&& Function )
	{
		if constexpr ( bFitsInline<DecayedType> ) new ( Storage ) DecayedType( Forward<FunctionType>( Function ) );
		else new ( Storage ) DecayedType*( new DecayedType( Forward<FunctionType>( Function ) ) );
		Ops = &TOps<DecayedType>::Table;
	}

	// 今の中身の操作の表。空なら nullptr。
	const FOps* Ops = nullptr;
	// 中身を置く領域。const の入れ物からでも、中身が変わるものを呼べるよう mutable にしている。
	alignas( void* ) mutable unsigned char Storage[InlineSize];
};
