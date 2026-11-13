// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Markup/UIMarkupLoader.h"
#include "Library/Framework/UI/Markup/UIMarkupValues.h"
#include "Library/Framework/UI/Markup/UIXml.h"

// マークアップの要素の種類
enum class EUIMarkupElementKind : uint8
{
	Panel,
	Canvas,
	Stack,
	Grid,
	ScrollBox,
	Label,
	Image,
	ProgressBar,
	Button,
	CheckBox,
	Slider,
	TextBox,
	ListView,
	Dropdown
};

//=============================================================================
// FUIMarkupBuilder: 読んだ XML の木から、部品の木を作る
//   CUIMarkupLoader::Load が、1 回の読み込みごとに 1 つ作って、Build を呼ぶ。警告とエラーは、Result に入る。
//   実装は、役割ごとのファイルに分けてある:
//     UIMarkupBuilder.cpp                部品を作る・名前の表から探す・1 つの要素と、その子を組み立てる
//     UIMarkupCommonAttributes.cpp       どの要素にもある属性（配置・見え方など）と、Enabled / Visible の結び付け
//     UIMarkupElementAttributes.cpp      要素ごとの属性と、属性を集めてから、まとめて設定するもの
//     UIMarkupBindings.cpp               結び付け（{Binding ...}）と、イベントの受け手
//     UIMarkupMotion.cpp                 動き（状態に合わせた動き・<Animation>）
//   マークアップの読み込み（UIMarkup*.cpp）の中だけで使う。
//=============================================================================
struct FUIMarkupBuilder
{
	FUIMarkupContext& Context;
	CUIBindingSet& Bindings;
	FUIMarkupResult& Result;
	FUIMarkupBuilder( FUIMarkupContext& InContext, CUIBindingSet& InBindings, FUIMarkupResult& InResult ) : Context( InContext ), Bindings( InBindings ), Result( InResult ) {}

	// "@名前" の、見つからなかった名前（直前の ResolveColor / ResolveFloat が失敗した理由）
	FString MissingToken;

	// 色: "@名前"（テーマ）か、#RGB・#RRGGBB・#RRGGBBAA
	bool ResolveColor( const FString& Text, FColor32& Out );

	// 数: "@名前"（テーマ）か、数
	bool ResolveFloat( const FString& Text, float32& Out );
	void Warn( const FUIXmlNode& Node, const FString& Message );
	bool Fail( const FUIXmlNode& Node, const FString& Message );
	bool BadValue( const FUIXmlNode& Node, const FUIXmlAttribute& Attribute, const char* Expected );

	// ---- 名前の表からの取得（無ければ警告） ----
	TSharedPtr<CFont> FindFont( const FUIXmlNode& Node, const FString& Name );

	// ブラシ: #色・{Material パス}（マテリアルのブラシ）・登録した名前。見つからなければ、false（警告は、呼んだ側で）
	bool FindBrush( const FString& Value, FUIBrush& Out );

	template<typename TStyle>
	bool FindStyle( const FUIXmlNode& Node, const TMap<FString, TStyle>& Table, const FString& Name, TStyle& Out, const char* Kind, bool ( FUITheme::*ThemeFind )( const FString&, TStyle& ) const )
	{
		if ( const TStyle* Found = Table.Find( Name ) )
		{
			Out = *Found;
			return true;
		}
		if ( Context.Theme && ( ( *Context.Theme ).*ThemeFind )( Name, Out ) )
		{
			return true;
		}
		Warn( Node, FString::Printf( "the %s style '%s' is not registered", Kind, *Name ) );
		return false;
	}
	TCallable<void()> FindHandler( const FUIXmlNode& Node, const FString& Name );

	// 要素を作る
	bool Create( const FUIXmlNode& Node, EUIMarkupElementKind& OutKind, TSharedPtr<CUIWidget>& OutWidget );

	// ---- 共通の属性 ----
	// 戻り値: 見つけた属性か（見つけて、値が壊れていたら、Fail を設定する）。bOk に、成功・失敗
	bool ApplyCommon( const FUIXmlNode& Node, const FUIXmlAttribute& A, CUIWidget& Widget, FUILayoutProperties& L, bool& bOk );

	// 共通の属性の名前か（要素ごとの属性を、先に調べるとき、重ならないように）
	static bool IsCommonName( const FString& N );

	// ---- 結び付け（共通の Enabled / Visible） ----
	bool BindBool( const FUIXmlNode& Node, const FString& BindingName, const TSharedPtr<CUIWidget>& Widget, bool bVisible );

	// ---- 要素ごとの属性 ----
	// 戻り値: 続けてよいか。見つけた属性は bHandled を true に。値が壊れていれば、Fail
	bool ApplySpecific( const FUIXmlNode& Node, const FUIXmlAttribute& A, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget, FUILayoutProperties& L, bool& bHandled );

	// ---- 属性を集めて、まとめて設定するもの ----
	bool SetupScrollBox( const FUIXmlNode& Node, CUIScrollBox& Scroll );
	bool SetupImage( const FUIXmlNode& Node, CUIImage& Image );
	bool SetupButton( const FUIXmlNode& Node, CUIButton& Button );
	bool SetupSlider( const FUIXmlNode& Node, const TSharedPtr<CUISlider>& Slider );
	bool SetupDropdown( const FUIXmlNode& Node, const TSharedPtr<CUIDropdown>& Dropdown );
	bool SetupList( const FUIXmlNode& Node, const TSharedPtr<CUIListView>& List );

	// ---- 結び付け（{Binding ...}）----
	bool ApplyBindings( const FUIXmlNode& Node, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget );

	// ---- イベントの受け手 ----
	void ApplyHandlers( const FUIXmlNode& Node, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget );

	// ---- 動き ----
	// 状態に合わせた動きの属性か（HoverScale など。SetupStateMotion が、まとめて読む）
	static bool IsStateMotionName( const FString& N );

	// 動かす仕組み（CUIAnimator）。文脈に無ければ、警告して null
	CUIAnimator* FindAnimator( const FUIXmlNode& Node, const char* What );
	bool ReadEasing( const FUIXmlNode& Node, const char* AttributeName, EEasing& Out );

	// 数の属性を読む（無ければ、Out はそのまま）
	bool ReadFloat( const FUIXmlNode& Node, const char* AttributeName, float32& Out, bool& bOutFound );
	bool ReadVector2( const FUIXmlNode& Node, const char* AttributeName, FVector2& Out, bool& bOutFound );

	// HoverScale・PressedScale・FocusScale・HoverOffset・PressedOffset・FocusOffset・MotionDuration・MotionEasing
	bool SetupStateMotion( const FUIXmlNode& Node, const TSharedPtr<CUIWidget>& Widget );

	// <Animation Property="Opacity" From="0" To="1" Duration="0.3" Delay="0.1" Easing="CubicOut" Loop="Once" />
	//   親の要素の部品を、読み込んだときに動かし始める。From を省くと、いまの値から。Rotation は、度で書く
	bool SetupAnimation( const FUIXmlNode& Node, const TSharedPtr<CUIWidget>& Widget );

	// ---- 1 つの要素と、その子 ----
	TSharedPtr<CUIWidget> Build( const FUIXmlNode& Node );
};
