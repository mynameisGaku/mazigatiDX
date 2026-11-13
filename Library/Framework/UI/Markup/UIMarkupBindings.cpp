// SPDX-License-Identifier: MIT
#include "UIMarkupBuilder.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

using namespace NUIMarkupValue;

// FUIMarkupBuilder: 結び付け（{Binding ...}）と、イベントの受け手

// ---- 結び付け（{Binding ...}）----
bool FUIMarkupBuilder::ApplyBindings( const FUIXmlNode& Node, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget )
{
	// ドロップダウンは、項目の結び付けを先に（選ぶ番号は、項目が入ってから決まるので、属性の並びに左右されないように）
	if ( Kind == EUIMarkupElementKind::Dropdown )
	{
		FString ItemsBinding;
		const FString* ItemsValue = Node.FindAttribute( "Items" );
		if ( ItemsValue != nullptr && ParseBinding( *ItemsValue, ItemsBinding ) )
		{
			TObservableArray<FString>* const* Found = Context.StringLists.Find( ItemsBinding );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the string list binding '%s' is not registered", *ItemsBinding ) );
			}
			else
			{
				Bindings.BindItems( **Found, StaticCastSharedPtr<CUIDropdown>( Widget ) );
			}
		}
	}
	for ( int32 Index = 0; Index < Node.Attributes.Num(); ++Index )
	{
		const FUIXmlAttribute& A = Node.Attributes[Index];
		FString BindingName;
		if ( !ParseBinding( A.Value, BindingName ) )
		{
			continue;
		}
		const FString& N = A.Name;
		if ( N == "Enabled" || N == "Visible" )
		{
			BindBool( Node, BindingName, Widget, N == "Visible" );
		}
		else if ( Kind == EUIMarkupElementKind::Label && N == "Text" )
		{
			TObservable<FString>* const* Found = Context.Strings.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the string binding '%s' is not registered", *BindingName ) );
			}
			else
			{
				Bindings.BindLabel( **Found, StaticCastSharedPtr<CUILabel>( Widget ) );
			}
		}
		else if ( Kind == EUIMarkupElementKind::TextBox && N == "Text" )
		{
			TObservable<FString>* const* Found = Context.Strings.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the string binding '%s' is not registered", *BindingName ) );
			}
			else
			{
				Bindings.BindText( **Found, StaticCastSharedPtr<CUITextBox>( Widget ) );
			}
		}
		else if ( Kind == EUIMarkupElementKind::CheckBox && N == "Checked" )
		{
			TObservable<bool>* const* Found = Context.Bools.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the bool binding '%s' is not registered", *BindingName ) );
			}
			else
			{
				Bindings.BindChecked( **Found, StaticCastSharedPtr<CUICheckBox>( Widget ) );
			}
		}
		else if ( ( Kind == EUIMarkupElementKind::Slider || Kind == EUIMarkupElementKind::ProgressBar ) && N == "Value" )
		{
			TObservable<float32>* const* Found = Context.Floats.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the number binding '%s' is not registered", *BindingName ) );
			}
			else if ( Kind == EUIMarkupElementKind::Slider )
			{
				Bindings.BindValue( **Found, StaticCastSharedPtr<CUISlider>( Widget ) );
			}
			else
			{
				Bindings.BindProgress( **Found, StaticCastSharedPtr<CUIProgressBar>( Widget ) );
			}
		}
		else if ( Kind == EUIMarkupElementKind::Button && N == "Command" )
		{
			FUICommand* const* Found = Context.Commands.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the command binding '%s' is not registered", *BindingName ) );
			}
			else
			{
				Bindings.BindCommand( StaticCastSharedPtr<CUIButton>( Widget ), **Found );
			}
		}
		else if ( ( Kind == EUIMarkupElementKind::ListView && N == "Items" ) )
		{
			// SetupList で済み
		}
		else if ( Kind == EUIMarkupElementKind::Dropdown && N == "Items" )
		{
			// 先に済ませた
		}
		else if ( Kind == EUIMarkupElementKind::Dropdown && N == "SelectedIndex" )
		{
			TObservable<int32>* const* Found = Context.Ints.Find( BindingName );
			if ( Found == nullptr )
			{
				Warn( Node, FString::Printf( "the number (whole) binding '%s' is not registered", *BindingName ) );
			}
			else
			{
				Bindings.BindSelectedIndex( **Found, StaticCastSharedPtr<CUIDropdown>( Widget ) );
			}
		}
		else
		{
			Warn( Node, FString::Printf( "the attribute %s cannot be bound", *N ) );
		}
	}
	return true;
}

// ---- イベントの受け手 ----
void FUIMarkupBuilder::ApplyHandlers( const FUIXmlNode& Node, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget )
{
	auto AddClicked = [&]( const char* Attribute, TUIEvent<CUIWidget&>& Event )
	{
		if ( const FString* Value = Node.FindAttribute( Attribute ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				Event.Add( [Handler]( CUIWidget& ) { Handler(); } );
			}
		}
	};
	switch ( Kind )
	{
	case EUIMarkupElementKind::Button:
		AddClicked( "OnClicked", Widget->OnClicked );
		break;
	case EUIMarkupElementKind::CheckBox:
		AddClicked( "OnClicked", Widget->OnClicked );
		if ( const FString* Value = Node.FindAttribute( "OnToggled" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUICheckBox>( Widget )->OnToggled.Add( [Handler]( CUIWidget&, bool ) { Handler(); } );
			}
		}
		break;
	case EUIMarkupElementKind::Slider:
		if ( const FString* Value = Node.FindAttribute( "OnValueChanged" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUISlider>( Widget )->OnValueChanged.Add( [Handler]( CUIWidget&, float32 ) { Handler(); } );
			}
		}
		break;
	case EUIMarkupElementKind::TextBox:
		if ( const FString* Value = Node.FindAttribute( "OnSubmitted" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUITextBox>( Widget )->OnSubmitted.Add( [Handler]( CUIWidget&, const FString& ) { Handler(); } );
			}
		}
		if ( const FString* Value = Node.FindAttribute( "OnCancelled" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUITextBox>( Widget )->OnCancelled.Add( [Handler]( CUIWidget& ) { Handler(); } );
			}
		}
		break;
	case EUIMarkupElementKind::Dropdown:
		if ( const FString* Value = Node.FindAttribute( "OnSelectionChanged" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUIDropdown>( Widget )->OnSelectionChanged.Add( [Handler]( CUIWidget&, int32 ) { Handler(); } );
			}
		}
		break;
	case EUIMarkupElementKind::ListView:
		if ( const FString* Value = Node.FindAttribute( "OnItemClicked" ) )
		{
			const TCallable<void()> Handler = FindHandler( Node, *Value );
			if ( Handler )
			{
				StaticCastSharedPtr<CUIListView>( Widget )->OnItemClicked.Add( [Handler]( CUIWidget&, int32 ) { Handler(); } );
			}
		}
		break;
	default:
		break;
	}
}
