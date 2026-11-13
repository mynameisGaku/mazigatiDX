// SPDX-License-Identifier: MIT
#include "UIMarkupBuilder.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

using namespace NUIMarkupValue;

// FUIMarkupBuilder: 部品を作る・名前の表から探す・1 つの要素と、その子を組み立てる

// 色: "@名前"（テーマ）か、#RGB・#RRGGBB・#RRGGBBAA
bool FUIMarkupBuilder::ResolveColor( const FString& Text, FColor32& Out )
{
	const FString Trimmed = Trim( Text );
	if ( Trimmed.Len() >= 2 && ( *Trimmed )[0] == '@' )
	{
		const FString TokenName = Trimmed.Mid( 1 );
		if ( Context.Theme && Context.Theme->FindColor( TokenName, Out ) )
		{
			return true;
		}
		MissingToken = Trimmed;
		return false;
	}
	return ParseColor( Text, Out );
}

// 数: "@名前"（テーマ）か、数
bool FUIMarkupBuilder::ResolveFloat( const FString& Text, float32& Out )
{
	const FString Trimmed = Trim( Text );
	if ( Trimmed.Len() >= 2 && ( *Trimmed )[0] == '@' )
	{
		const FString TokenName = Trimmed.Mid( 1 );
		if ( Context.Theme && Context.Theme->FindNumber( TokenName, Out ) )
		{
			return true;
		}
		MissingToken = Trimmed;
		return false;
	}
	return ParseFloat( Text, Out );
}

void FUIMarkupBuilder::Warn( const FUIXmlNode& Node, const FString& Message )
{
	Result.Warnings.Add( FString::Printf( "line %d: <%s> %s", Node.Line, *Node.Name, *Message ) );
}

bool FUIMarkupBuilder::Fail( const FUIXmlNode& Node, const FString& Message )
{
	if ( Result.Error.IsEmpty() )
	{
		Result.Error = FString::Printf( "line %d: <%s> %s", Node.Line, *Node.Name, *Message );
	}
	return false;
}

bool FUIMarkupBuilder::BadValue( const FUIXmlNode& Node, const FUIXmlAttribute& Attribute, const char* Expected )
{
	if ( !MissingToken.IsEmpty() )
	{
		const FString Token = MissingToken;
		MissingToken.Empty();
		return Fail( Node, FString::Printf( "the attribute %s=\"%s\" is not valid (the theme token '%s' is not defined)", *Attribute.Name, *Attribute.Value, *Token ) );
	}
	return Fail( Node, FString::Printf( "the attribute %s=\"%s\" is not valid (%s expected)", *Attribute.Name, *Attribute.Value, Expected ) );
}

// ---- 名前の表からの取得（無ければ警告） ----
TSharedPtr<CFont> FUIMarkupBuilder::FindFont( const FUIXmlNode& Node, const FString& Name )
{
	if ( const TSharedPtr<CFont>* Found = Context.Fonts.Find( Name ) )
	{
		return *Found;
	}
	TSharedPtr<CFont> ThemeFont;
	if ( Context.Theme && Context.Theme->FindFont( Name, ThemeFont ) )
	{
		return ThemeFont;
	}
	Warn( Node, FString::Printf( "the font '%s' is not registered", *Name ) );
	return TSharedPtr<CFont>();
}

// ブラシ: #色・{Material パス}（マテリアルのブラシ）・登録した名前。見つからなければ、false（警告は、呼んだ側で）
bool FUIMarkupBuilder::FindBrush( const FString& Value, FUIBrush& Out )
{
	FString MaterialPath;
	if ( ParseMaterialReference( Value, MaterialPath ) )
	{
		CMaterialSubsystem* Materials = GetAppSubsystem<CMaterialSubsystem>();
		const TSharedPtr<CMaterial> Material = Materials != nullptr ? Materials->LoadMaterial( MaterialPath ) : TSharedPtr<CMaterial>();
		if ( !Material.IsValid() || !Material->IsValid() )
		{
			return false;
		}
		Out = FUIBrush::MakeMaterial( Material );
		return true;
	}
	FColor32 Color;
	if ( ResolveColor( Value, Color ) )
	{
		Out = FUIBrush::MakeSolid( Color );
		return true;
	}
	if ( const FUIBrush* Found = Context.Brushes.Find( Trim( Value ) ) )
	{
		Out = *Found;
		return true;
	}
	return Context.Theme && Context.Theme->FindBrush( Trim( Value ), Out );
}

TCallable<void()> FUIMarkupBuilder::FindHandler( const FUIXmlNode& Node, const FString& Name )
{
	if ( const TCallable<void()>* Found = Context.Handlers.Find( Name ) )
	{
		return *Found;
	}
	Warn( Node, FString::Printf( "the handler '%s' is not registered", *Name ) );
	return TCallable<void()>();
}

// 要素を作る
bool FUIMarkupBuilder::Create( const FUIXmlNode& Node, EUIMarkupElementKind& OutKind, TSharedPtr<CUIWidget>& OutWidget )
{
	const FString& Name = Node.Name;
	if ( Name == "Panel" )
	{
		OutKind = EUIMarkupElementKind::Panel;
		OutWidget = MakeShared<CUIWidget>();
	}
	else if ( Name == "Canvas" )
	{
		OutKind = EUIMarkupElementKind::Canvas;
		OutWidget = MakeShared<CUICanvasPanel>();
	}
	else if ( Name == "VerticalStack" || Name == "HorizontalStack" || Name == "Stack" )
	{
		OutKind = EUIMarkupElementKind::Stack;
		TSharedPtr<CUIStackPanel> Stack = MakeShared<CUIStackPanel>();
		Stack->SetOrientation( Name == "HorizontalStack" ? EUIOrientation::Horizontal : EUIOrientation::Vertical );
		OutWidget = Stack;
	}
	else if ( Name == "Grid" )
	{
		OutKind = EUIMarkupElementKind::Grid;
		OutWidget = MakeShared<CUIGridPanel>();
	}
	else if ( Name == "ScrollBox" )
	{
		OutKind = EUIMarkupElementKind::ScrollBox;
		OutWidget = MakeShared<CUIScrollBox>();
	}
	else if ( Name == "Label" )
	{
		OutKind = EUIMarkupElementKind::Label;
		OutWidget = MakeShared<CUILabel>();
	}
	else if ( Name == "Image" )
	{
		OutKind = EUIMarkupElementKind::Image;
		OutWidget = MakeShared<CUIImage>();
	}
	else if ( Name == "ProgressBar" )
	{
		OutKind = EUIMarkupElementKind::ProgressBar;
		OutWidget = MakeShared<CUIProgressBar>();
	}
	else if ( Name == "Button" )
	{
		OutKind = EUIMarkupElementKind::Button;
		OutWidget = MakeShared<CUIButton>();
	}
	else if ( Name == "CheckBox" )
	{
		OutKind = EUIMarkupElementKind::CheckBox;
		OutWidget = MakeShared<CUICheckBox>();
	}
	else if ( Name == "Slider" )
	{
		OutKind = EUIMarkupElementKind::Slider;
		OutWidget = MakeShared<CUISlider>();
	}
	else if ( Name == "TextBox" )
	{
		OutKind = EUIMarkupElementKind::TextBox;
		OutWidget = MakeShared<CUITextBox>();
	}
	else if ( Name == "ListView" )
	{
		OutKind = EUIMarkupElementKind::ListView;
		OutWidget = MakeShared<CUIListView>();
	}
	else if ( Name == "Dropdown" )
	{
		OutKind = EUIMarkupElementKind::Dropdown;
		TSharedPtr<CUIDropdown> Dropdown = MakeShared<CUIDropdown>();
		Dropdown->SetPopupHost( Context.PopupHost );
		OutWidget = Dropdown;
	}
	else
	{
		return Fail( Node, "is not a known element (Panel, Canvas, VerticalStack, HorizontalStack, Stack, Grid, ScrollBox, Label, Image, ProgressBar, Button, CheckBox, Slider, TextBox, ListView, Dropdown)" );
	}
	return true;
}

// ---- 1 つの要素と、その子 ----
TSharedPtr<CUIWidget> FUIMarkupBuilder::Build( const FUIXmlNode& Node )
{
	EUIMarkupElementKind Kind = EUIMarkupElementKind::Panel;
	TSharedPtr<CUIWidget> Widget;
	if ( !Create( Node, Kind, Widget ) )
	{
		return TSharedPtr<CUIWidget>();
	}

	FUILayoutProperties Layout = Widget->GetLayout();
	for ( int32 Index = 0; Index < Node.Attributes.Num(); ++Index )
	{
		const FUIXmlAttribute& A = Node.Attributes[Index];
		bool bOk = true;
		if ( IsStateMotionName( A.Name ) )
		{
			continue;
		}
		if ( ApplyCommon( Node, A, *Widget, Layout, bOk ) )
		{
			if ( !bOk )
			{
				return TSharedPtr<CUIWidget>();
			}
			continue;
		}
		bool bHandled = false;
		if ( !ApplySpecific( Node, A, Kind, Widget, Layout, bHandled ) )
		{
			return TSharedPtr<CUIWidget>();
		}
		if ( !bHandled )
		{
			Warn( Node, FString::Printf( "the attribute '%s' is not known and is ignored", *A.Name ) );
		}
	}
	Widget->SetLayout( Layout );

	// 属性を集めて、まとめて設定するもの
	bool bOk = true;
	switch ( Kind )
	{
	case EUIMarkupElementKind::ScrollBox:
		bOk = SetupScrollBox( Node, static_cast<CUIScrollBox&>( *Widget ) );
		break;
	case EUIMarkupElementKind::Image:
		bOk = SetupImage( Node, static_cast<CUIImage&>( *Widget ) );
		break;
	case EUIMarkupElementKind::Button:
		bOk = SetupButton( Node, static_cast<CUIButton&>( *Widget ) );
		break;
	case EUIMarkupElementKind::Slider:
		bOk = SetupSlider( Node, StaticCastSharedPtr<CUISlider>( Widget ) );
		break;
	case EUIMarkupElementKind::ListView:
		bOk = SetupList( Node, StaticCastSharedPtr<CUIListView>( Widget ) );
		break;
	case EUIMarkupElementKind::Dropdown:
		bOk = SetupDropdown( Node, StaticCastSharedPtr<CUIDropdown>( Widget ) );
		break;
	default:
		break;
	}
	if ( !bOk )
	{
		return TSharedPtr<CUIWidget>();
	}

	// 子
	for ( int32 Index = 0; Index < Node.Children.Num(); ++Index )
	{
		if ( Node.Children[Index]->Name == "Animation" )
		{
			if ( !SetupAnimation( *Node.Children[Index], Widget ) )
			{
				return TSharedPtr<CUIWidget>();
			}
			continue;
		}
		TSharedPtr<CUIWidget> Child = Build( *Node.Children[Index] );
		if ( !Child )
		{
			return TSharedPtr<CUIWidget>();
		}
		Widget->AddChild( Child );
	}

	ApplyBindings( Node, Kind, Widget );
	ApplyHandlers( Node, Kind, Widget );
	// 状態の動きは、最後に付ける（Scale・Translation の属性を読んだあとの値が、元の値になる）
	if ( !SetupStateMotion( Node, Widget ) )
	{
		return TSharedPtr<CUIWidget>();
	}
	return Widget;
}
