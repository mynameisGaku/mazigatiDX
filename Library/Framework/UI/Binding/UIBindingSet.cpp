// SPDX-License-Identifier: MIT
#include "UIBindingSet.h"

void CUIBindingSet::Clear()
{
	// 解除は、結んだ順の逆に
	for ( int32 Index = Unbinders.Num() - 1; Index >= 0; --Index )
	{
		if ( Unbinders[Index] )
		{
			Unbinders[Index]();
		}
	}
	Unbinders.Empty();
	Polls.Empty();
}

void CUIBindingSet::Absorb( CUIBindingSet& Other )
{
	if ( &Other == this )
	{
		return;
	}
	Unbinders.Append( Other.Unbinders );
	Polls.Append( Other.Polls );
	Other.Unbinders.Empty();
	Other.Polls.Empty();
}

void CUIBindingSet::Update()
{
	// 関数を呼んでいる最中に、結び付けが増えても、安全なように、数を先に決める
	const int32 Count = Polls.Num();
	for ( int32 Index = 0; Index < Count && Index < Polls.Num(); ++Index )
	{
		if ( Polls[Index] )
		{
			Polls[Index]();
		}
	}
}

void CUIBindingSet::BindText( TObservable<FString>& Source, const TSharedPtr<CUITextBox>& Box )
{
	// 観測値 → 入力欄（同じなら、触らない。カーソルが動かないように）
	Bind( Source, [Box]( const FString& Text )
	{
		if ( Box->GetText() != Text )
		{
			Box->SetText( Text, false );
		}
	} );

	// 入力欄 → 観測値
	TObservable<FString>* Pointer = &Source;
	const TWeakPtr<bool> Alive = Source.GetAliveToken();
	const int32 Id = Box->OnTextChanged.Add( [Pointer, Alive]( CUIWidget&, const FString& Text )
	{
		if ( Alive.IsValid() )
		{
			Pointer->Set( Text );
		}
	} );
	AddUnbinder( [Box, Id]() { Box->OnTextChanged.Remove( Id ); } );
}

void CUIBindingSet::BindChecked( TObservable<bool>& Source, const TSharedPtr<CUICheckBox>& CheckBox )
{
	Bind( Source, [CheckBox]( const bool& bChecked ) { CheckBox->SetChecked( bChecked, false ); } );

	TObservable<bool>* Pointer = &Source;
	const TWeakPtr<bool> Alive = Source.GetAliveToken();
	const int32 Id = CheckBox->OnToggled.Add( [Pointer, Alive]( CUIWidget&, bool bChecked )
	{
		if ( Alive.IsValid() )
		{
			Pointer->Set( bChecked );
		}
	} );
	AddUnbinder( [CheckBox, Id]() { CheckBox->OnToggled.Remove( Id ); } );
}

void CUIBindingSet::BindValue( TObservable<float32>& Source, const TSharedPtr<CUISlider>& Slider )
{
	// スライダーは、範囲と刻みに収めた値を見せる（観測値は、そのまま）
	Bind( Source, [Slider]( const float32& Value ) { Slider->SetValue( Value, false ); } );

	TObservable<float32>* Pointer = &Source;
	const TWeakPtr<bool> Alive = Source.GetAliveToken();
	const int32 Id = Slider->OnValueChanged.Add( [Pointer, Alive]( CUIWidget&, float32 Value )
	{
		if ( Alive.IsValid() )
		{
			Pointer->Set( Value );
		}
	} );
	AddUnbinder( [Slider, Id]() { Slider->OnValueChanged.Remove( Id ); } );
}

void CUIBindingSet::BindLabel( TObservable<FString>& Source, const TSharedPtr<CUILabel>& Label )
{
	Bind( Source, [Label]( const FString& Text ) { Label->SetText( Text ); } );
}

void CUIBindingSet::BindProgress( TObservable<float32>& Source, const TSharedPtr<CUIProgressBar>& Bar )
{
	Bind( Source, [Bar]( const float32& Value ) { Bar->SetValue( Value ); } );
}

void CUIBindingSet::BindEnabled( TObservable<bool>& Source, const TSharedPtr<CUIWidget>& Widget )
{
	Bind( Source, [Widget]( const bool& bEnabled ) { Widget->SetEnabled( bEnabled ); } );
}

void CUIBindingSet::BindVisible( TObservable<bool>& Source, const TSharedPtr<CUIWidget>& Widget )
{
	Bind( Source, [Widget]( const bool& bVisible ) { Widget->SetVisibility( bVisible ? EUIVisibility::Visible : EUIVisibility::Collapsed ); } );
}

void CUIBindingSet::BindCommand( const TSharedPtr<CUIButton>& Button, FUICommand& Command )
{
	// 実行できるか → ボタンの使える・使えない
	BindEnabled( Command.Enabled, Button );

	// 押す → 実行（実行できるときだけ）
	FUICommand* Pointer = &Command;
	const TWeakPtr<bool> Alive = Command.Enabled.GetAliveToken();
	const int32 Id = Button->OnClicked.Add( [Pointer, Alive]( CUIWidget& )
	{
		if ( Alive.IsValid() )
		{
			Pointer->TryExecute();
		}
	} );
	AddUnbinder( [Button, Id]() { Button->OnClicked.Remove( Id ); } );
}
