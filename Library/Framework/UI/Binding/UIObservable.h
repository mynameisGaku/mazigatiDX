// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/UI/Core/UIEvent.h"

//=============================================================================
// TObservable<T> / TObservableArray<T> / FUICommand
//   MVVM のための、変更を知らせる値（ビューモデルのプロパティ）。UI の部品とは別に、ゲームのデータとして使える
//   （部品を知らない。UI なしでも、検査や他の用途に使える）。部品との結び付けは CUIBindingSet が行う。
//
//   struct FSettingsViewModel
//   {
//       TObservable<float32> Volume { 0.5f };
//       TObservable<bool> Music { true };
//       TObservable<FString> Name;
//       TObservableArray<FString> Items;
//       FUICommand Reset { []() { ... } };
//   };
//
//   - TObservable<T>: Set は、値が変わったときだけ（T::operator== で比べる）OnChanged を起こして true を返す。同じ値なら何もしない（無限の往復を防ぐ）。
//   - TObservableArray<T>: 追加・挿入・削除・置き換え・全部入れ替えを、種類と位置つきで OnChanged へ通知する。
//   - FUICommand: 実行する関数と、実行できるか（Enabled。TObservable<bool>）。ボタンへ結ぶと、押したときに実行し、Enabled に従って、押せる・押せないが替わる。
//   - コピーできない（結び付けが、値の置き場所を指すため）。ビューモデルは、結び付けの組（CUIBindingSet）より先に、消えてもよい（結び付けの側が、
//     観測値の生死を確かめて、解除する）。
//=============================================================================
template<typename T>
class TObservable
{
public:
	TObservable() = default;
	explicit TObservable( const T& InitialValue ) : Value( InitialValue ) {}
	TObservable( const TObservable& ) = delete;
	TObservable& operator=( const TObservable& ) = delete;

	const T& Get() const { return Value; }

	// 値を設定する。変わったら、OnChanged（新しい値）を起こして true。同じ値なら、何もせず false
	bool Set( const T& NewValue )
	{
		if ( Value == NewValue )
		{
			return false;
		}
		Value = NewValue;
		OnChanged.Broadcast( Value );
		return true;
	}

	// 値が変わったとき（引数: 新しい値）
	TUIEvent<const T&> OnChanged;

	// この観測値が生きている間だけ有効な印（結び付けが、解除するとき、観測値がまだあるかを確かめる）
	TWeakPtr<bool> GetAliveToken() const { return TWeakPtr<bool>( Alive ); }

private:
	T Value {};
	TSharedPtr<bool> Alive = MakeShared<bool>( true );
};

// 配列の変更の種類
enum class EObservableChange : uint8
{
	Reset,     // 全部が入れ替わった（Index = 0、Count = 新しい数）
	Added,     // Index から Count 個が加わった
	Removed,   // Index から Count 個が消えた
	Replaced   // Index の 1 つが、別の値になった
};

struct FObservableChange
{
	EObservableChange Kind = EObservableChange::Reset;
	int32 Index = 0;
	int32 Count = 0;
};

template<typename T>
class TObservableArray
{
public:
	TObservableArray() = default;
	TObservableArray( const TObservableArray& ) = delete;
	TObservableArray& operator=( const TObservableArray& ) = delete;

	int32 Num() const { return Items.Num(); }
	bool IsEmpty() const { return Items.IsEmpty(); }
	const T& operator[]( int32 Index ) const { return Items[Index]; }
	const TArray<T>& GetItems() const { return Items; }

	int32 Add( const T& Item )
	{
		const int32 Index = Items.Add( Item );
		Notify( EObservableChange::Added, Index, 1 );
		return Index;
	}

	// Index（0〜Num。範囲の外は末尾）へ差し込む
	void Insert( const T& Item, int32 Index )
	{
		const int32 Safe = ( Index < 0 || Index > Items.Num() ) ? Items.Num() : Index;
		Items.Insert( Item, Safe );
		Notify( EObservableChange::Added, Safe, 1 );
	}

	// Index を消す。範囲の外なら、何もせず false
	bool RemoveAt( int32 Index )
	{
		if ( !Items.IsValidIndex( Index ) )
		{
			return false;
		}
		Items.RemoveAt( Index );
		Notify( EObservableChange::Removed, Index, 1 );
		return true;
	}

	// Index の値を替える（同じ値でも通知する）。範囲の外なら、何もせず false
	bool Replace( int32 Index, const T& Item )
	{
		if ( !Items.IsValidIndex( Index ) )
		{
			return false;
		}
		Items[Index] = Item;
		Notify( EObservableChange::Replaced, Index, 1 );
		return true;
	}

	// 全部を入れ替える
	void Reset( const TArray<T>& NewItems )
	{
		Items = NewItems;
		Notify( EObservableChange::Reset, 0, Items.Num() );
	}

	void Clear()
	{
		Items.Empty();
		Notify( EObservableChange::Reset, 0, 0 );
	}

	// 変わったとき（引数: 種類・位置・個数）
	TUIEvent<const FObservableChange&> OnChanged;

	TWeakPtr<bool> GetAliveToken() const { return TWeakPtr<bool>( Alive ); }

private:
	void Notify( EObservableChange Kind, int32 Index, int32 Count )
	{
		FObservableChange Change;
		Change.Kind = Kind;
		Change.Index = Index;
		Change.Count = Count;
		OnChanged.Broadcast( Change );
	}

	TArray<T> Items;
	TSharedPtr<bool> Alive = MakeShared<bool>( true );
};

// ボタンなどへ結べる、操作（実行する関数 + 実行できるか）
class FUICommand
{
public:
	using FExecute = TCallable<void()>;

	explicit FUICommand( const FExecute& InExecute, bool bInEnabled = true ) : Execute( InExecute ), Enabled( bInEnabled ) {}
	FUICommand( const FUICommand& ) = delete;
	FUICommand& operator=( const FUICommand& ) = delete;

	// 実行できるとき（Enabled が true で、関数がある）だけ実行して true
	bool TryExecute() const
	{
		if ( !Enabled.Get() || !Execute )
		{
			return false;
		}
		Execute();
		return true;
	}

	FExecute Execute;
	TObservable<bool> Enabled;
};
