// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDropdown.h"
#include "Library/Framework/UI/Widgets/UITextBox.h"

class CFont;

//=============================================================================
// FUITheme: UI の見た目の「名前つきの値」の集まり（テーマ）
//   色・数・ブラシ・フォント・部品のスタイルを、名前で持つ。UI は、値そのものではなく、名前で参照する
//   （マークアップでは、色は Color="@Accent"、数は Spacing="@Gap"、スタイルは Style="Default"）。
//   テーマを差し替えれば、同じ UI の見た目が、まとめて変わる。
//
//   継承: テーマは、親のテーマを持てる。名前が自分に無ければ、親へ探しに行く（親の親へも）。
//   「基本のテーマ」を作り、その子で、変えたいところだけを上書きする。
//
//   TSharedPtr<FUITheme> Base = Style.MakeTheme( Body, Title );   // FUIDefaultStyle から、基本のテーマ
//   TSharedPtr<FUITheme> Blue = MakeShared<FUITheme>();
//   Blue->SetParent( Base );
//   Blue->SetColor( "Accent", FColor32( 120, 180, 255 ) );        // Accent だけ、基本と違う
//   Context.SetTheme( Blue );                                     // マークアップの文脈へ
//
// 名前の区別は、大文字と小文字を区別する。種類（色・数・ブラシ・フォント・各スタイル）ごとに、別の名前の置き場。
//=============================================================================
class FUITheme
{
public:
	const FString& GetName() const { return Name; }
	void SetName( const FString& InName ) { Name = InName; }

	// 親のテーマ（名前が自分に無いときの探し先）。自分自身か、自分を親にしているテーマを親にすると、輪になるので、false で断る
	bool SetParent( const TSharedPtr<FUITheme>& InParent );
	const TSharedPtr<FUITheme>& GetParent() const { return Parent; }

	// ---- 色 ----
	void SetColor( const FString& TokenName, const FColor32& Color ) { Colors.Add( TokenName, Color ); }
	bool FindColor( const FString& TokenName, FColor32& OutColor ) const { return Find( &FUITheme::Colors, TokenName, OutColor ); }
	FColor32 GetColor( const FString& TokenName, const FColor32& Default = FColor32( 255, 255, 255, 255 ) ) const;

	// ---- 数（余白・間隔・大きさなど） ----
	void SetNumber( const FString& TokenName, float32 Value ) { Numbers.Add( TokenName, Value ); }
	bool FindNumber( const FString& TokenName, float32& OutValue ) const { return Find( &FUITheme::Numbers, TokenName, OutValue ); }
	float32 GetNumber( const FString& TokenName, float32 Default = 0.0f ) const;

	// ---- ブラシ・フォント ----
	void SetBrush( const FString& TokenName, const FUIBrush& Brush ) { Brushes.Add( TokenName, Brush ); }
	bool FindBrush( const FString& TokenName, FUIBrush& OutBrush ) const { return Find( &FUITheme::Brushes, TokenName, OutBrush ); }
	void SetFont( const FString& TokenName, const TSharedPtr<CFont>& Font ) { Fonts.Add( TokenName, Font ); }
	bool FindFont( const FString& TokenName, TSharedPtr<CFont>& OutFont ) const { return Find( &FUITheme::Fonts, TokenName, OutFont ); }

	// ---- 部品のスタイル ----
	void SetButtonStyle( const FString& StyleName, const FUIButtonStyle& Style ) { ButtonStyles.Add( StyleName, Style ); }
	bool FindButtonStyle( const FString& StyleName, FUIButtonStyle& OutStyle ) const { return Find( &FUITheme::ButtonStyles, StyleName, OutStyle ); }
	void SetCheckBoxStyle( const FString& StyleName, const FUICheckBoxStyle& Style ) { CheckBoxStyles.Add( StyleName, Style ); }
	bool FindCheckBoxStyle( const FString& StyleName, FUICheckBoxStyle& OutStyle ) const { return Find( &FUITheme::CheckBoxStyles, StyleName, OutStyle ); }
	void SetSliderStyle( const FString& StyleName, const FUISliderStyle& Style ) { SliderStyles.Add( StyleName, Style ); }
	bool FindSliderStyle( const FString& StyleName, FUISliderStyle& OutStyle ) const { return Find( &FUITheme::SliderStyles, StyleName, OutStyle ); }
	void SetDropdownStyle( const FString& StyleName, const FUIDropdownStyle& Style ) { DropdownStyles.Add( StyleName, Style ); }
	bool FindDropdownStyle( const FString& StyleName, FUIDropdownStyle& OutStyle ) const { return Find( &FUITheme::DropdownStyles, StyleName, OutStyle ); }
	void SetTextBoxStyle( const FString& StyleName, const FUITextBoxStyle& Style ) { TextBoxStyles.Add( StyleName, Style ); }
	bool FindTextBoxStyle( const FString& StyleName, FUITextBoxStyle& OutStyle ) const { return Find( &FUITheme::TextBoxStyles, StyleName, OutStyle ); }

	// 自分だけが持つ名前の数（親の分は数えない）
	int32 NumOwnTokens() const { return Colors.Num() + Numbers.Num() + Brushes.Num() + Fonts.Num() + ButtonStyles.Num() + CheckBoxStyles.Num() + SliderStyles.Num() + TextBoxStyles.Num() + DropdownStyles.Num(); }

private:
	// 自分から親へ、名前を探す
	template<typename TValue>
	bool Find( TMap<FString, TValue> FUITheme::*Table, const FString& TokenName, TValue& OutValue ) const
	{
		for ( const FUITheme* Theme = this; Theme != nullptr; Theme = Theme->Parent.Get() )
		{
			if ( const TValue* Found = ( Theme->*Table ).Find( TokenName ) )
			{
				OutValue = *Found;
				return true;
			}
		}
		return false;
	}

	FString Name;
	TSharedPtr<FUITheme> Parent;
	TMap<FString, FColor32> Colors;
	TMap<FString, float32> Numbers;
	TMap<FString, FUIBrush> Brushes;
	TMap<FString, TSharedPtr<CFont>> Fonts;
	TMap<FString, FUIButtonStyle> ButtonStyles;
	TMap<FString, FUICheckBoxStyle> CheckBoxStyles;
	TMap<FString, FUISliderStyle> SliderStyles;
	TMap<FString, FUITextBoxStyle> TextBoxStyles;
	TMap<FString, FUIDropdownStyle> DropdownStyles;
};
