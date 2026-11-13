// SPDX-License-Identifier: MIT
#include "UIMarkupBuilder.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

using namespace NUIMarkupValue;

// FUIMarkupBuilder: 要素ごとの属性と、属性を集めてから、まとめて設定するもの

// ---- 要素ごとの属性 ----
// 戻り値: 続けてよいか。見つけた属性は bHandled を true に。値が壊れていれば、Fail
bool FUIMarkupBuilder::ApplySpecific( const FUIXmlNode& Node, const FUIXmlAttribute& A, EUIMarkupElementKind Kind, const TSharedPtr<CUIWidget>& Widget, FUILayoutProperties& L, bool& bHandled )
{
	bHandled = true;
	const FString& N = A.Name;
	float32 Number = 0.0f;
	bool bFlag = false;
	switch ( Kind )
	{
	case EUIMarkupElementKind::Stack:
	{
		CUIStackPanel& Stack = static_cast<CUIStackPanel&>( *Widget );
		if ( N == "Spacing" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 0.0f )
			{
				return BadValue( Node, A, "a number of 0 or more" );
			}
			Stack.SetSpacing( Number );
			return true;
		}
		if ( N == "Orientation" )
		{
			EUIOrientation Orientation = EUIOrientation::Vertical;
			if ( !ParseOrientation( A.Value, Orientation ) )
			{
				return BadValue( Node, A, "Horizontal or Vertical" );
			}
			Stack.SetOrientation( Orientation );
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Grid:
	{
		CUIGridPanel& Grid = static_cast<CUIGridPanel&>( *Widget );
		if ( N == "Columns" || N == "Rows" )
		{
			TArray<FUITrack> Tracks;
			if ( !ParseTracks( A.Value, Tracks ) )
			{
				return BadValue( Node, A, "a list like \"100, Auto, *, 2*\"" );
			}
			if ( N == "Columns" )
			{
				Grid.SetColumns( Tracks );
			}
			else
			{
				Grid.SetRows( Tracks );
			}
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::ScrollBox:
	{
		CUIScrollBox& Scroll = static_cast<CUIScrollBox&>( *Widget );
		if ( N == "Orientation" )
		{
			EUIOrientation Orientation = EUIOrientation::Vertical;
			if ( !ParseOrientation( A.Value, Orientation ) )
			{
				return BadValue( Node, A, "Horizontal or Vertical" );
			}
			Scroll.SetOrientation( Orientation );
			return true;
		}
		if ( N == "WheelStep" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 1.0f )
			{
				return BadValue( Node, A, "a number of 1 or more" );
			}
			Scroll.SetWheelStep( Number );
			return true;
		}
		if ( N == "ScrollBarWidth" || N == "ScrollBarTrack" || N == "ScrollBarThumb" )
		{
			// 3 つ組。先に集めておき、まとめて設定する（SetupScrollBar）
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Label:
	{
		CUILabel& Label = static_cast<CUILabel&>( *Widget );
		if ( N == "Text" )
		{
			FString BindingName;
			if ( !ParseBinding( A.Value, BindingName ) )
			{
				Label.SetText( A.Value );
			}
			return true;
		}
		if ( N == "Font" )
		{
			Label.SetFont( FindFont( Node, A.Value ) );
			return true;
		}
		if ( N == "Color" )
		{
			FColor32 Color;
			if ( !ResolveColor( A.Value, Color ) )
			{
				return BadValue( Node, A, "a color like #RRGGBB" );
			}
			Label.SetColor( Color );
			return true;
		}
		if ( N == "MaxWidth" || N == "MaxLines" )
		{
			return true; // 共通の MaxWidth と区別するため、Label では、下で扱う
		}
		if ( N == "TextAlign" || N == "VTextAlign" )
		{
			EUIAlignment Alignment = EUIAlignment::Start;
			if ( !ParseAlignment( A.Value, Alignment ) )
			{
				return BadValue( Node, A, "Start, Center, End or Stretch" );
			}
			if ( N == "TextAlign" )
			{
				Label.SetHorizontalTextAlignment( Alignment );
			}
			else
			{
				Label.SetVerticalTextAlignment( Alignment );
			}
			return true;
		}
		if ( N == "WrapWidth" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 0.0f )
			{
				return BadValue( Node, A, "a number of 0 or more" );
			}
			Label.SetMaxWidth( Number );
			return true;
		}
		if ( N == "Lines" )
		{
			int32 Lines = 0;
			if ( !ParseInt( A.Value, Lines ) || Lines < 0 )
			{
				return BadValue( Node, A, "a whole number of 0 or more" );
			}
			Label.SetMaxLines( Lines );
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Image:
	{
		// Source・Mode・NineSlice・Tint は、まとめて、あとで（SetupImage）
		if ( N == "Source" || N == "Mode" || N == "NineSlice" || N == "Tint" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::ProgressBar:
	{
		CUIProgressBar& Bar = static_cast<CUIProgressBar&>( *Widget );
		if ( N == "Value" )
		{
			FString BindingName;
			if ( ParseBinding( A.Value, BindingName ) )
			{
				return true;
			}
			if ( !ResolveFloat( A.Value, Number ) )
			{
				return BadValue( Node, A, "a number from 0 to 1" );
			}
			Bar.SetValue( Number );
			return true;
		}
		if ( N == "Orientation" )
		{
			EUIOrientation Orientation = EUIOrientation::Horizontal;
			if ( !ParseOrientation( A.Value, Orientation ) )
			{
				return BadValue( Node, A, "Horizontal or Vertical" );
			}
			Bar.SetOrientation( Orientation );
			return true;
		}
		if ( N == "Back" || N == "Fill" )
		{
			FUIBrush Brush;
			if ( !FindBrush( A.Value, Brush ) )
			{
				Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", *A.Value ) );
			}
			else if ( N == "Back" )
			{
				Bar.SetBackBrush( Brush );
			}
			else
			{
				Bar.SetFillBrush( Brush );
			}
			return true;
		}
		if ( N == "FillInset" )
		{
			FUIMargin Margin;
			if ( !ParseMargin( A.Value, Margin ) )
			{
				return BadValue( Node, A, "1, 2 or 4 numbers" );
			}
			Bar.SetFillInset( Margin );
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Button:
	{
		CUIButton& Button = static_cast<CUIButton&>( *Widget );
		if ( N == "Style" )
		{
			FUIButtonStyle Style;
			if ( FindStyle( Node, Context.ButtonStyles, A.Value, Style, "button", &FUITheme::FindButtonStyle ) )
			{
				Button.SetStyle( Style );
			}
			return true;
		}
		if ( N == "Text" || N == "Font" || N == "TextColor" || N == "Command" )
		{
			return true; // 文字は、まとめて、あとで（SetupButton）。Command は、結び付けの段階
		}
		if ( N == "OnClicked" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::CheckBox:
	{
		CUICheckBox& Box = static_cast<CUICheckBox&>( *Widget );
		if ( N == "Checked" )
		{
			FString BindingName;
			if ( ParseBinding( A.Value, BindingName ) )
			{
				return true;
			}
			if ( !ParseBool( A.Value, bFlag ) )
			{
				return BadValue( Node, A, "true, false or {Binding name}" );
			}
			Box.SetChecked( bFlag, false );
			return true;
		}
		if ( N == "Style" )
		{
			FUICheckBoxStyle Style;
			if ( FindStyle( Node, Context.CheckBoxStyles, A.Value, Style, "check box", &FUITheme::FindCheckBoxStyle ) )
			{
				Box.SetStyle( Style );
			}
			return true;
		}
		if ( N == "OnToggled" || N == "OnClicked" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Slider:
	{
		CUISlider& Slider = static_cast<CUISlider&>( *Widget );
		if ( N == "Min" || N == "Max" )
		{
			return true; // まとめて、あとで（範囲は、2 つで 1 組）
		}
		if ( N == "Step" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 0.0f )
			{
				return BadValue( Node, A, "a number of 0 or more" );
			}
			Slider.SetStep( Number );
			return true;
		}
		if ( N == "Value" )
		{
			FString BindingName;
			if ( ParseBinding( A.Value, BindingName ) )
			{
				return true;
			}
			if ( !ResolveFloat( A.Value, Number ) )
			{
				return BadValue( Node, A, "a number or {Binding name}" );
			}
			return true; // 値は、範囲と刻みを決めたあとで（SetupSlider）
		}
		if ( N == "Orientation" )
		{
			EUIOrientation Orientation = EUIOrientation::Horizontal;
			if ( !ParseOrientation( A.Value, Orientation ) )
			{
				return BadValue( Node, A, "Horizontal or Vertical" );
			}
			Slider.SetOrientation( Orientation );
			return true;
		}
		if ( N == "Style" )
		{
			FUISliderStyle Style;
			if ( FindStyle( Node, Context.SliderStyles, A.Value, Style, "slider", &FUITheme::FindSliderStyle ) )
			{
				Slider.SetStyle( Style );
			}
			return true;
		}
		if ( N == "OnValueChanged" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::TextBox:
	{
		CUITextBox& Box = static_cast<CUITextBox&>( *Widget );
		if ( N == "Text" )
		{
			FString BindingName;
			if ( !ParseBinding( A.Value, BindingName ) )
			{
				Box.SetText( A.Value, false );
			}
			return true;
		}
		if ( N == "Placeholder" )
		{
			Box.SetPlaceholder( A.Value );
			return true;
		}
		if ( N == "MaxLength" )
		{
			int32 Length = 0;
			if ( !ParseInt( A.Value, Length ) || Length < 0 )
			{
				return BadValue( Node, A, "a whole number of 0 or more" );
			}
			Box.SetMaxLength( Length );
			return true;
		}
		if ( N == "Password" )
		{
			if ( !ParseBool( A.Value, bFlag ) )
			{
				return BadValue( Node, A, "true or false" );
			}
			Box.SetPasswordMode( bFlag );
			return true;
		}
		if ( N == "Multiline" )
		{
			if ( !ParseBool( A.Value, bFlag ) )
			{
				return BadValue( Node, A, "true or false" );
			}
			Box.SetMultiline( bFlag );
			return true;
		}
		if ( N == "WordWrap" )
		{
			if ( !ParseBool( A.Value, bFlag ) )
			{
				return BadValue( Node, A, "true or false" );
			}
			Box.SetWordWrap( bFlag );
			return true;
		}
		if ( N == "Font" )
		{
			Box.SetFont( FindFont( Node, A.Value ) );
			return true;
		}
		if ( N == "Style" )
		{
			FUITextBoxStyle Style;
			if ( FindStyle( Node, Context.TextBoxStyles, A.Value, Style, "text box", &FUITheme::FindTextBoxStyle ) )
			{
				Box.SetStyle( Style );
			}
			return true;
		}
		if ( N == "OnSubmitted" || N == "OnCancelled" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::ListView:
	{
		CUIListView& List = static_cast<CUIListView&>( *Widget );
		if ( N == "Orientation" )
		{
			EUIOrientation Orientation = EUIOrientation::Vertical;
			if ( !ParseOrientation( A.Value, Orientation ) )
			{
				return BadValue( Node, A, "Horizontal or Vertical" );
			}
			List.SetOrientation( Orientation );
			return true;
		}
		if ( N == "ItemHeight" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 1.0f )
			{
				return BadValue( Node, A, "a number of 1 or more" );
			}
			List.SetItemHeight( Number );
			return true;
		}
		if ( N == "Items" || N == "Font" || N == "ItemColor" || N == "SelectedBackground" || N == "ItemPadding" || N == "ScrollBarWidth" || N == "ScrollBarTrack" || N == "ScrollBarThumb" )
		{
			return true; // まとめて、あとで（SetupList）
		}
		if ( N == "OnItemClicked" )
		{
			return true;
		}
		break;
	}
	case EUIMarkupElementKind::Dropdown:
	{
		CUIDropdown& Dropdown = static_cast<CUIDropdown&>( *Widget );
		if ( N == "Placeholder" )
		{
			Dropdown.SetPlaceholder( A.Value );
			return true;
		}
		if ( N == "Font" )
		{
			Dropdown.SetFont( FindFont( Node, A.Value ) );
			return true;
		}
		if ( N == "Style" )
		{
			FUIDropdownStyle Style;
			if ( FindStyle( Node, Context.DropdownStyles, A.Value, Style, "dropdown", &FUITheme::FindDropdownStyle ) )
			{
				Dropdown.SetDropdownStyle( Style );
			}
			return true;
		}
		if ( N == "SelectedIndex" )
		{
			FString BindingName;
			int32 Index = 0;
			if ( ParseBinding( A.Value, BindingName ) )
			{
				return true;
			}
			if ( !ParseInt( A.Value, Index ) || Index < -1 )
			{
				return BadValue( Node, A, "a whole number of -1 or more, or {Binding name}" );
			}
			return true; // 項目を入れてから（SetupDropdown）
		}
		if ( N == "Items" || N == "OnSelectionChanged" )
		{
			return true;
		}
		break;
	}
	default:
		break;
	}
	( void ) L;
	bHandled = false;
	return true;
}

// ---- 属性を集めて、まとめて設定するもの ----
bool FUIMarkupBuilder::SetupScrollBox( const FUIXmlNode& Node, CUIScrollBox& Scroll )
{
	FUIBrush Track;
	FUIBrush Thumb;
	float32 Width = 0.0f;
	if ( const FString* Value = Node.FindAttribute( "ScrollBarWidth" ) )
	{
		if ( !ResolveFloat( *Value, Width ) || Width < 0.0f )
		{
			return Fail( Node, "the attribute ScrollBarWidth is not valid (a number of 0 or more expected)" );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "ScrollBarTrack" ) )
	{
		if ( !FindBrush( *Value, Track ) )
		{
			Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", **Value ) );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "ScrollBarThumb" ) )
	{
		if ( !FindBrush( *Value, Thumb ) )
		{
			Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", **Value ) );
		}
	}
	Scroll.SetScrollBar( Track, Thumb, Width );
	return true;
}

bool FUIMarkupBuilder::SetupImage( const FUIXmlNode& Node, CUIImage& Image )
{
	EUIImageMode Mode = EUIImageMode::Stretch;
	if ( const FString* Value = Node.FindAttribute( "Mode" ) )
	{
		if ( !ParseImageMode( *Value, Mode ) )
		{
			return Fail( Node, "the attribute Mode is not valid (Stretch, Fit, Fill, Tile or NineSlice expected)" );
		}
	}
	FUIMargin NineSlice;
	if ( const FString* Value = Node.FindAttribute( "NineSlice" ) )
	{
		if ( !ParseMargin( *Value, NineSlice ) )
		{
			return Fail( Node, "the attribute NineSlice is not valid (1, 2 or 4 numbers expected)" );
		}
		Mode = EUIImageMode::NineSlice;
	}
	FColor32 Tint( 255, 255, 255, 255 );
	if ( const FString* Value = Node.FindAttribute( "Tint" ) )
	{
		if ( !ResolveColor( *Value, Tint ) )
		{
			return Fail( Node, "the attribute Tint is not valid (a color like #RRGGBB expected)" );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "Source" ) )
	{
		TSharedPtr<IImage> Texture;
		FVector2 Size( 0.0f, 0.0f );
		if ( !Context.ImageLoader )
		{
			Warn( Node, "an image loader is not set (SetImageLoader), so Source is ignored" );
		}
		else if ( !Context.ImageLoader( *Value, Texture, Size ) )
		{
			Warn( Node, FString::Printf( "the image '%s' could not be loaded", **Value ) );
		}
		else
		{
			Image.SetImage( Texture, Size, Mode );
			Image.SetNineSliceMargin( NineSlice );
			Image.SetTint( Tint );
		}
	}
	return true;
}

bool FUIMarkupBuilder::SetupButton( const FUIXmlNode& Node, CUIButton& Button )
{
	// Text: 中央に、ラベルを 1 つ入れる
	const FString* Text = Node.FindAttribute( "Text" );
	if ( Text == nullptr )
	{
		return true;
	}
	TSharedPtr<CUILabel> Caption = MakeShared<CUILabel>();
	Caption->SetText( *Text );
	if ( const FString* Font = Node.FindAttribute( "Font" ) )
	{
		Caption->SetFont( FindFont( Node, *Font ) );
	}
	if ( const FString* ColorText = Node.FindAttribute( "TextColor" ) )
	{
		FColor32 Color;
		if ( !ResolveColor( *ColorText, Color ) )
		{
			return Fail( Node, "the attribute TextColor is not valid (a color like #RRGGBB expected)" );
		}
		Caption->SetColor( Color );
	}
	FUILayoutProperties CaptionLayout;
	CaptionLayout.HorizontalAlignment = EUIAlignment::Center;
	CaptionLayout.VerticalAlignment = EUIAlignment::Center;
	Caption->SetLayout( CaptionLayout );
	Caption->SetHitTestable( false );
	Button.AddChild( Caption );
	return true;
}

bool FUIMarkupBuilder::SetupSlider( const FUIXmlNode& Node, const TSharedPtr<CUISlider>& Slider )
{
	float32 Minimum = 0.0f;
	float32 Maximum = 1.0f;
	if ( const FString* Value = Node.FindAttribute( "Min" ) )
	{
		if ( !ResolveFloat( *Value, Minimum ) )
		{
			return Fail( Node, "the attribute Min is not valid (a number expected)" );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "Max" ) )
	{
		if ( !ResolveFloat( *Value, Maximum ) )
		{
			return Fail( Node, "the attribute Max is not valid (a number expected)" );
		}
	}
	if ( Maximum < Minimum )
	{
		return Fail( Node, "Max must not be smaller than Min" );
	}
	Slider->SetRange( Minimum, Maximum );
	// リテラルの値（結び付けではないとき）は、範囲と刻みを決めたあとで
	if ( const FString* Value = Node.FindAttribute( "Value" ) )
	{
		FString BindingName;
		float32 Number = 0.0f;
		if ( !ParseBinding( *Value, BindingName ) && ResolveFloat( *Value, Number ) )
		{
			Slider->SetValue( Number, false );
		}
	}
	return true;
}

bool FUIMarkupBuilder::SetupDropdown( const FUIXmlNode& Node, const TSharedPtr<CUIDropdown>& Dropdown )
{
	// 項目: 結び付け {Binding 名前}（文字列の配列）か、コンマで区切った文字（Items="Low, Medium, High"）
	if ( const FString* Value = Node.FindAttribute( "Items" ) )
	{
		FString BindingName;
		if ( !ParseBinding( *Value, BindingName ) )
		{
			Dropdown->SetItems( SplitList( *Value ) );
		}
	}
	// 選ぶ番号（リテラル）。結び付けのときは、結び付けの段階で
	if ( const FString* Value = Node.FindAttribute( "SelectedIndex" ) )
	{
		FString BindingName;
		int32 Index = 0;
		if ( !ParseBinding( *Value, BindingName ) && ParseInt( *Value, Index ) )
		{
			Dropdown->SetSelectedIndex( Index, false );
		}
	}
	return true;
}

bool FUIMarkupBuilder::SetupList( const FUIXmlNode& Node, const TSharedPtr<CUIListView>& List )
{
	// ScrollBar（3 つ組）
	FUIBrush Track;
	FUIBrush Thumb;
	float32 BarWidth = 0.0f;
	if ( const FString* Value = Node.FindAttribute( "ScrollBarWidth" ) )
	{
		if ( !ResolveFloat( *Value, BarWidth ) || BarWidth < 0.0f )
		{
			return Fail( Node, "the attribute ScrollBarWidth is not valid (a number of 0 or more expected)" );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "ScrollBarTrack" ) )
	{
		if ( !FindBrush( *Value, Track ) )
		{
			Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", **Value ) );
		}
	}
	if ( const FString* Value = Node.FindAttribute( "ScrollBarThumb" ) )
	{
		if ( !FindBrush( *Value, Thumb ) )
		{
			Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", **Value ) );
		}
	}
	List->SetScrollBar( Track, Thumb, BarWidth );

	// 行: ラベル。選ばれている行の背景は、SelectedBackground（ブラシの名前か #色）
	TSharedPtr<CFont> Font;
	if ( const FString* Value = Node.FindAttribute( "Font" ) )
	{
		Font = FindFont( Node, *Value );
	}
	FColor32 ItemColor( 235, 238, 245, 255 );
	if ( const FString* Value = Node.FindAttribute( "ItemColor" ) )
	{
		if ( !ResolveColor( *Value, ItemColor ) )
		{
			return Fail( Node, "the attribute ItemColor is not valid (a color like #RRGGBB expected)" );
		}
	}
	FUIBrush SelectedBackground;
	if ( const FString* Value = Node.FindAttribute( "SelectedBackground" ) )
	{
		if ( !FindBrush( *Value, SelectedBackground ) )
		{
			Warn( Node, FString::Printf( "the brush '%s' is neither a #color nor a registered brush", **Value ) );
		}
	}
	FUIMargin ItemPadding( 10.0f, 0.0f );
	if ( const FString* Value = Node.FindAttribute( "ItemPadding" ) )
	{
		if ( !ParseMargin( *Value, ItemPadding ) )
		{
			return Fail( Node, "the attribute ItemPadding is not valid (1, 2 or 4 numbers expected)" );
		}
	}

	const FString* ItemsValue = Node.FindAttribute( "Items" );
	FString BindingName;
	if ( ItemsValue == nullptr || !ParseBinding( *ItemsValue, BindingName ) )
	{
		if ( ItemsValue != nullptr )
		{
			return Fail( Node, "the attribute Items must be a binding like {Binding name}" );
		}
		return true;
	}
	TObservableArray<FString>* const* Found = Context.StringLists.Find( BindingName );
	if ( Found == nullptr )
	{
		Warn( Node, FString::Printf( "the string list binding '%s' is not registered", *BindingName ) );
		return true;
	}
	TObservableArray<FString>* Items = *Found;
	const TWeakPtr<bool> Alive = Items->GetAliveToken();
	List->SetItemFactory(
		[Font, ItemColor, ItemPadding]() -> TSharedPtr<CUIWidget >
		{
			TSharedPtr<CUILabel> Label = MakeShared<CUILabel>();
			Label->SetFont( Font );
			Label->SetColor( ItemColor );
			FUILayoutProperties Layout;
			Layout.HorizontalAlignment = EUIAlignment::Start;
			Layout.VerticalAlignment = EUIAlignment::Center;
			Layout.Padding = ItemPadding;
			Label->SetLayout( Layout );
			return Label;
		},
		[Items, Alive, SelectedBackground]( CUIWidget& Item, int32 Index, bool bSelected )
		{
			CUILabel& Label = static_cast<CUILabel&>( Item );
			Label.SetText( Alive.IsValid() && Items->GetItems().IsValidIndex( Index ) ? ( *Items )[Index] : FString() );
			Label.SetBackground( bSelected ? SelectedBackground : FUIBrush() );
		} );
	Bindings.BindList( *Items, List );
	return true;
}
