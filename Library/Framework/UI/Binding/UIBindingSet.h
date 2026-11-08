// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Optional.h"
#include "Library/Framework/UI/Binding/UIObservable.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"
#include "Library/Framework/UI/Widgets/UIListView.h"
#include "Library/Framework/UI/Widgets/UITextBox.h"

//=============================================================================
// CUIBindingSet
//   観測値（ビューモデル）と、UI の部品を、結ぶ。結び付けの組を 1 つ持ち、組が消える（または Clear）と、すべての結び付けが解除される。
//   結び付けは任意で、混ぜて使える（イベントの結び付け・直接の操作と、並べて使ってよい）。
//
//   CUIBindingSet Bindings;
//   Bindings.BindValue( ViewModel.Volume, VolumeSlider );          // 双方向: スライダーを動かすと Volume が変わり、Volume を変えるとスライダーが動く
//   Bindings.BindLabel( ViewModel.Title, TitleLabel );             // 一方向: Title が変わると、文字が変わる
//   Bindings.BindCommand( ResetButton, ViewModel.Reset );          // 押すと、実行する。実行できるか（Enabled）で、押せるかも替わる
//   Bindings.BindList( ViewModel.Items, ItemList );                // 配列が変わると、リストの行の数と中身が替わる
//   Bindings.Bind( ViewModel.Volume, []( const float32& Value ) { ... } );        // 好きな反映（一方向）
//   Bindings.BindFunction<int32>( []() { return Score; }, []( const int32& Value ) { ... } );   // 毎フレームの関数（Update で調べ、変わったときだけ反映）
//
//   - 結んだ直後に、いまの値を、部品へ 1 回反映する。
//   - 双方向は、値が同じなら何もしない（往復して止まらなくならない。カーソルも動かない）。スライダーは、範囲と刻みに収められた値を見せる
//     （観測値は、収められる前の値のまま）。
//   - 部品は、結び付けが、共有の参照で持つ（結んでいる間は、消えない）。観測値（ビューモデル）は、結び付けの組より先に消えてもよい（解除のとき、生死を確かめる）。
//   - BindFunction は、毎フレーム Update() を呼んだときに調べる（値の型は T::operator== で比べる）。
//=============================================================================
class CUIBindingSet
{
public:
	CUIBindingSet() = default;
	CUIBindingSet( const CUIBindingSet& ) = delete;
	CUIBindingSet& operator=( const CUIBindingSet& ) = delete;
	~CUIBindingSet() { Clear(); }

	// すべての結び付けを解除する
	void Clear();
	// 別の組の結び付けを、すべて、この組へ移す（Other は空になる。解除は、この組が受け持つ）
	void Absorb( CUIBindingSet& Other );
	// 結んでいる数
	int32 Num() const { return Unbinders.Num(); }
	// BindFunction の関数を調べて、変わったものを反映する（毎フレーム呼ぶ）
	void Update();

	// 一方向（観測値 → 好きな反映）。結んだ直後と、値が変わるたびに、Apply( 値 ) を呼ぶ
	template<typename T, typename TApply>
	void Bind( TObservable<T>& Source, TApply Apply )
	{
		Apply( Source.Get() );
		const int32 Id = Source.OnChanged.Add( [Apply]( const T& Value ) mutable { Apply( Value ); } );
		TObservable<T>* Pointer = &Source;
		const TWeakPtr<bool> Alive = Source.GetAliveToken();
		AddUnbinder( [Pointer, Alive, Id]()
		{
			if ( Alive.IsValid() )
			{
				Pointer->OnChanged.Remove( Id );
			}
		} );
	}

	// 毎フレームの関数（Update で、Getter を呼び、前と違うときだけ Setter を呼ぶ。結んだ直後にも 1 回）
	template<typename T, typename TGetter, typename TSetter>
	void BindFunction( TGetter Getter, TSetter Setter )
	{
		TSharedPtr<TOptional<T>> Last = MakeShared<TOptional<T>>();
		TCallable<void()> Poll = [Getter, Setter, Last]() mutable
		{
			const T Value = Getter();
			if ( !Last->IsSet() || !( Last->GetValue() == Value ) )
			{
				*Last = Value;
				Setter( Value );
			}
		};
		Poll();
		Polls.Add( Poll );
		AddUnbinder( [this, Index = Polls.Num() - 1]()
		{
			if ( Polls.IsValidIndex( Index ) )
			{
				Polls[Index] = TCallable<void()>();
			}
		} );
	}

	// 配列 → リストの行の数・中身（変わるたびに、SetItemCount と Refresh）。行を作る・入れる関数は、リスト自身に設定しておく
	template<typename T>
	void BindList( TObservableArray<T>& Source, const TSharedPtr<CUIListView>& List )
	{
		TObservableArray<T>* Pointer = &Source;
		auto Apply = [Pointer, List]()
		{
			List->SetItemCount( Pointer->Num() );
			List->Refresh();
		};
		Apply();
		const int32 Id = Source.OnChanged.Add( [Apply]( const FObservableChange& ) { Apply(); } );
		const TWeakPtr<bool> Alive = Source.GetAliveToken();
		AddUnbinder( [Pointer, Alive, Id]()
		{
			if ( Alive.IsValid() )
			{
				Pointer->OnChanged.Remove( Id );
			}
		} );
	}

	// 双方向
	void BindText( TObservable<FString>& Source, const TSharedPtr<CUITextBox>& Box );
	void BindChecked( TObservable<bool>& Source, const TSharedPtr<CUICheckBox>& CheckBox );
	void BindValue( TObservable<float32>& Source, const TSharedPtr<CUISlider>& Slider );

	// 一方向
	void BindLabel( TObservable<FString>& Source, const TSharedPtr<CUILabel>& Label );
	void BindProgress( TObservable<float32>& Source, const TSharedPtr<CUIProgressBar>& Bar );
	// true なら使える / false なら使えない
	void BindEnabled( TObservable<bool>& Source, const TSharedPtr<CUIWidget>& Widget );
	// true なら見える（Visible）/ false なら場所も取らない（Collapsed）
	void BindVisible( TObservable<bool>& Source, const TSharedPtr<CUIWidget>& Widget );

	// ボタン ⇔ 操作: 押す（クリック・決定）と、実行できるときだけ実行する。実行できるか（Enabled）に、ボタンの使える・使えないが従う
	void BindCommand( const TSharedPtr<CUIButton>& Button, FUICommand& Command );

private:
	void AddUnbinder( const TCallable<void()>& Unbinder ) { Unbinders.Add( Unbinder ); }

	TArray<TCallable<void()>> Unbinders;
	TArray<TCallable<void()>> Polls;
};
