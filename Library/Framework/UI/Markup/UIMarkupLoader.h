// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Map.h"
#include "Library/Framework/UI/Animation/UIAnimator.h"
#include "Library/Framework/UI/Binding/UIBindingSet.h"
#include "Library/Framework/UI/Core/UIPanels.h"
#include "Library/Framework/UI/Markup/UIXml.h"
#include "Library/Framework/UI/Style/UITheme.h"
#include "Library/Framework/UI/Widgets/UIControlWidgets.h"
#include "Library/Framework/UI/Widgets/UIDropdown.h"
#include "Library/Framework/UI/Widgets/UIDisplayWidgets.h"
#include "Library/Framework/UI/Widgets/UIListView.h"
#include "Library/Framework/UI/Widgets/UIScrollBox.h"
#include "Library/Framework/UI/Widgets/UITextBox.h"

//=============================================================================
// UI のマークアップ（.gxui）: XML で、部品の木を書く
//   JSON の画面ではなく、XML（UXML・XAML に近い形）で、UI を書ける。書いたものは、コードで組んだ木と、同じ部品の木になる。
//
//   <Panel Name="Window" Background="Window" Padding="24,20">
//     <VerticalStack Spacing="12">
//       <Label Text="{Binding Title}" Font="Title" Color="#FFE296" />
//       <CheckBox Checked="{Binding Music}" Style="Default" Width="28" Height="28" />
//       <Slider Value="{Binding Volume}" Min="0" Max="100" Step="5" Height="28" Style="Default" />
//       <TextBox Text="{Binding Name}" Placeholder="Your name" MaxLength="16" Height="38" Style="Default" />
//       <HorizontalStack Spacing="8" Height="44">
//         <Button Text="OK" Width="120" Height="44" Style="Default" OnClicked="Confirm" />
//         <Button Text="Reset" Width="120" Height="44" Style="Default" Command="{Binding Reset}" />
//       </HorizontalStack>
//     </VerticalStack>
//   </Panel>
//
//   FUIMarkupContext Context;                         // 名前の表: フォント・ブラシ・スタイル・イベントの受け手・ビューモデルの値
//   Context.RegisterFont( "Title", TitleFont );
//   Context.RegisterBrush( "Window", WindowBrush );
//   Context.RegisterButtonStyle( "Default", ButtonStyle );
//   Context.RegisterHandler( "Confirm", []() { ... } );
//   Context.RegisterFloat( "Volume", ViewModel.Volume );   // ほか RegisterString / RegisterBool / RegisterCommand / RegisterStringList
//   CUIBindingSet Bindings;                           // 結び付けは、この組が持つ（消えると解除される）
//   const FUIMarkupResult Result = CUIMarkupLoader::Load( Markup, Context, Bindings );
//
// 要素: Panel（子を重ねる）・Canvas・VerticalStack・HorizontalStack・Grid・ScrollBox・Label・Image・ProgressBar・Button・CheckBox・Slider・TextBox・ListView・Dropdown
// 共通の属性: Name・Width・Height（固定の大きさ）・MinWidth・MinHeight・MaxWidth・MaxHeight・Margin・Padding（数が 1・2・4 個）・HAlign・VAlign（Start / Center / End / Stretch）・
//   Weight・Column・Row・ColumnSpan・RowSpan・Anchor（TopLeft・Top・TopRight・Left・Center・Right・BottomLeft・Bottom・BottomRight・Stretch）・AnchorMin・AnchorMax・
//   Offsets（左・上・右・下）・Pivot・Visibility（Visible / Hidden / Collapsed）・Enabled・Opacity・Clip・HitTestable・Background（#RRGGBB[AA] か、ブラシの名前）
//   Translation（x,y）・Scale（1 個か x,y）・Rotation（度）・TransformPivot（x,y。0〜1）: 描くときだけ掛ける変換（レイアウトは変えない）
//   各要素の属性は、Docs/UI.md の「マークアップ（XML）」を参照。
// 動き（文脈に SetAnimator で CUIAnimator を渡す。無ければ、警告して無視する）:
//   HoverScale・PressedScale・FocusScale・HoverOffset・PressedOffset・FocusOffset・MotionDuration・MotionEasing: 状態に合わせた動き（FUIStateMotion）
//   <Animation Property="Opacity" From="0" To="1" Duration="0.3" Delay="0.1" Easing="CubicOut" Loop="Once" />: 親の要素を、読み込んだときに動かし始める
//     Property は Opacity・TranslationX・TranslationY・Scale・ScaleX・ScaleY・Rotation（度）。From を省くと、いまの値から。Loop は Once・Loop・PingPong
// テーマ: 文脈にテーマ（FUITheme）を渡すと、フォント・ブラシ・スタイルの名前は、文脈の登録に無ければ、テーマから探す。
//   色と数の属性には、"@名前" と書ける（Color="@Accent"、Spacing="@Gap"）。
// 値の書き方:
//   {Binding 名前}  : ビューモデルの値へ結ぶ（文脈に登録した名前。Text・Checked・Value・Command・Items・Enabled・Visible など）。双方向になる属性は、入力欄の Text・
//                     チェックボックスの Checked・スライダーの Value
//   数・真偽・"x,y"・色（#RGB・#RRGGBB・#RRGGBBAA）・列挙の名前
// 読み込みの結果:
//   - 読めない XML・知らない要素・属性の値の書き間違いは、Success = false（理由と行番号つき。何も作らない）。
//   - 文脈に無い名前（フォント・ブラシ・スタイル・イベントの受け手・結び付けの名前）と、知らない属性は、警告（Warnings）にして、その属性を無視して続ける。
//     FUIMarkupContext::bWarningsAreErrors を true にすると、警告も失敗にする。
//=============================================================================
class FUIMarkupContext
{
public:
	void RegisterFont( const FString& Name, const TSharedPtr<CFont>& Font ) { Fonts.Add( Name, Font ); }
	void RegisterBrush( const FString& Name, const FUIBrush& Brush ) { Brushes.Add( Name, Brush ); }
	void RegisterButtonStyle( const FString& Name, const FUIButtonStyle& Style ) { ButtonStyles.Add( Name, Style ); }
	void RegisterCheckBoxStyle( const FString& Name, const FUICheckBoxStyle& Style ) { CheckBoxStyles.Add( Name, Style ); }
	void RegisterSliderStyle( const FString& Name, const FUISliderStyle& Style ) { SliderStyles.Add( Name, Style ); }
	void RegisterTextBoxStyle( const FString& Name, const FUITextBoxStyle& Style ) { TextBoxStyles.Add( Name, Style ); }
	void RegisterDropdownStyle( const FString& Name, const FUIDropdownStyle& Style ) { DropdownStyles.Add( Name, Style ); }
	// ドロップダウンの一覧を積む画面のスタック（Dropdown を使うときに渡す。ドロップダウンより長く生きること）
	void SetPopupHost( CUIScreenStack* InHost ) { PopupHost = InHost; }
	// ブラシの画像を読む関数（Image の Source。成功したら true で、画像とその大きさ（画素）を返す）
	using FImageLoader = TCallable<bool( const FString& Path, TSharedPtr<IImage>& OutImage, FVector2& OutSize )>;
	void SetImageLoader( const FImageLoader& Loader ) { ImageLoader = Loader; }

	// 動かす仕組み（状態に合わせた動きと <Animation> に使う。部品の木より長く生きること。ホストが、毎フレーム Update を呼ぶ）
	void SetAnimator( CUIAnimator* InAnimator ) { Animator = InAnimator; }

	// イベントの受け手（OnClicked="名前" など）
	void RegisterHandler( const FString& Name, const TCallable<void()>& Handler ) { Handlers.Add( Name, Handler ); }

	// ビューモデルの値（{Binding 名前}）。ビューモデルは、結び付けの組が消えるまで、あってよい（先に消えても安全）
	void RegisterString( const FString& Name, TObservable<FString>& Value ) { Strings.Add( Name, &Value ); }
	void RegisterFloat( const FString& Name, TObservable<float32>& Value ) { Floats.Add( Name, &Value ); }
	void RegisterBool( const FString& Name, TObservable<bool>& Value ) { Bools.Add( Name, &Value ); }
	void RegisterInt( const FString& Name, TObservable<int32>& Value ) { Ints.Add( Name, &Value ); }
	void RegisterCommand( const FString& Name, FUICommand& Command ) { Commands.Add( Name, &Command ); }
	void RegisterStringList( const FString& Name, TObservableArray<FString>& Items ) { StringLists.Add( Name, &Items ); }

	// テーマ（名前つきの色・数・ブラシ・フォント・スタイル）。文脈に登録した名前を先に探し、無ければテーマから探す。
	// 色と数は、"@名前" でテーマから読む（Color="@Accent"、Spacing="@Gap"）。テーマに無い "@名前" は、失敗にする
	void SetTheme( const TSharedPtr<FUITheme>& InTheme ) { Theme = InTheme; }
	const TSharedPtr<FUITheme>& GetTheme() const { return Theme; }

	// true なら、警告（文脈に無い名前・知らない属性）も、読み込みの失敗にする
	bool bWarningsAreErrors = false;

private:
	friend class CUIMarkupLoader;
	friend struct FUIMarkupBuilder;

	TMap<FString, TSharedPtr<CFont>> Fonts;
	TMap<FString, FUIBrush> Brushes;
	TMap<FString, FUIButtonStyle> ButtonStyles;
	TMap<FString, FUICheckBoxStyle> CheckBoxStyles;
	TMap<FString, FUISliderStyle> SliderStyles;
	TMap<FString, FUITextBoxStyle> TextBoxStyles;
	TMap<FString, FUIDropdownStyle> DropdownStyles;
	CUIScreenStack* PopupHost = nullptr;
	CUIAnimator* Animator = nullptr;
	FImageLoader ImageLoader;
	TSharedPtr<FUITheme> Theme;
	TMap<FString, TCallable<void()>> Handlers;
	TMap<FString, TObservable<FString>*> Strings;
	TMap<FString, TObservable<float32>*> Floats;
	TMap<FString, TObservable<bool>*> Bools;
	TMap<FString, TObservable<int32>*> Ints;
	TMap<FString, FUICommand*> Commands;
	TMap<FString, TObservableArray<FString>*> StringLists;
};

struct FUIMarkupResult
{
	bool bSuccess = false;
	TSharedPtr<CUIWidget> Root;
	FString Error;               // 失敗の理由（英語。行番号つき）
	TArray<FString> Warnings;    // 警告（英語。行番号つき）
};

class CUIMarkupLoader
{
public:
	// マークアップ（XML の文字列）から、部品の木を作る。結び付けは Bindings へ足す
	static FUIMarkupResult Load( const FString& Markup, FUIMarkupContext& Context, CUIBindingSet& Bindings );
};
