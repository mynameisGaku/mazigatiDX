// SPDX-License-Identifier: MIT
#include "UIMarkupLoader.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

namespace
{
	// ---- 値の読み取り -----------------------------------------------------

	FString Trim( const FString& Text )
	{
		return Text.TrimStartAndEnd();
	}

	bool ParseFloat( const FString& Text, float32& Out )
	{
		// 前後の空白を除いて、全体が数のときだけ（FString::TryParseFloat）
		const TOptional<float64> Value = Text.TryParseFloat();
		if ( !Value.IsSet() )
		{
			return false;
		}
		Out = static_cast<float32>( Value.GetValue() );
		return true;
	}

	bool ParseInt( const FString& Text, int32& Out )
	{
		float32 Value = 0.0f;
		if ( !ParseFloat( Text, Value ) || Value != static_cast<float32>( static_cast<int32>( Value ) ) )
		{
			return false;
		}
		Out = static_cast<int32>( Value );
		return true;
	}

	bool ParseBool( const FString& Text, bool& Out )
	{
		const FString Trimmed = Trim( Text ).ToLower();
		if ( Trimmed == "true" || Trimmed == "1" )
		{
			Out = true;
			return true;
		}
		if ( Trimmed == "false" || Trimmed == "0" )
		{
			Out = false;
			return true;
		}
		return false;
	}

	// "a, b, c" を、コンマで分けて、前後の空白を除く
	TArray<FString> SplitList( const FString& Text )
	{
		TArray<FString> Parts;
		FString Current;
		for ( int32 Index = 0; Index < Text.Len(); ++Index )
		{
			const char Ch = ( *Text )[Index];
			if ( Ch == ',' )
			{
				Parts.Add( Trim( Current ) );
				Current.Empty();
			}
			else
			{
				Current += Ch;
			}
		}
		Parts.Add( Trim( Current ) );
		return Parts;
	}

	bool ParseFloatList( const FString& Text, TArray<float32>& Out )
	{
		Out.Empty();
		const TArray<FString> Parts = SplitList( Text );
		for ( int32 Index = 0; Index < Parts.Num(); ++Index )
		{
			float32 Value = 0.0f;
			if ( !ParseFloat( Parts[Index], Value ) )
			{
				return false;
			}
			Out.Add( Value );
		}
		return true;
	}

	// 余白: 1 個（四辺）・2 個（左右・上下）・4 個（左・上・右・下）
	bool ParseMargin( const FString& Text, FUIMargin& Out )
	{
		TArray<float32> Values;
		if ( !ParseFloatList( Text, Values ) )
		{
			return false;
		}
		if ( Values.Num() == 1 )
		{
			Out = FUIMargin( Values[0] );
			return true;
		}
		if ( Values.Num() == 2 )
		{
			Out = FUIMargin( Values[0], Values[1] );
			return true;
		}
		if ( Values.Num() == 4 )
		{
			Out = FUIMargin( Values[0], Values[1], Values[2], Values[3] );
			return true;
		}
		return false;
	}

	bool ParseVector2( const FString& Text, FVector2& Out )
	{
		TArray<float32> Values;
		if ( !ParseFloatList( Text, Values ) || Values.Num() != 2 )
		{
			return false;
		}
		Out = FVector2( Values[0], Values[1] );
		return true;
	}

	int32 HexDigit( char Ch )
	{
		if ( Ch >= '0' && Ch <= '9' )
		{
			return Ch - '0';
		}
		if ( Ch >= 'a' && Ch <= 'f' )
		{
			return Ch - 'a' + 10;
		}
		if ( Ch >= 'A' && Ch <= 'F' )
		{
			return Ch - 'A' + 10;
		}
		return -1;
	}

	// #RGB・#RRGGBB・#RRGGBBAA
	bool ParseColor( const FString& Text, FColor32& Out )
	{
		const FString Trimmed = Trim( Text );
		if ( Trimmed.Len() < 2 || ( *Trimmed )[0] != '#' )
		{
			return false;
		}
		const int32 Digits = Trimmed.Len() - 1;
		if ( Digits != 3 && Digits != 6 && Digits != 8 )
		{
			return false;
		}
		int32 Values[8] = {};
		for ( int32 Index = 0; Index < Digits; ++Index )
		{
			Values[Index] = HexDigit( ( *Trimmed )[Index + 1] );
			if ( Values[Index] < 0 )
			{
				return false;
			}
		}
		if ( Digits == 3 )
		{
			Out = FColor32( static_cast<uint8>( Values[0] * 17 ), static_cast<uint8>( Values[1] * 17 ), static_cast<uint8>( Values[2] * 17 ), 255 );
			return true;
		}
		Out = FColor32( static_cast<uint8>( Values[0] * 16 + Values[1] ), static_cast<uint8>( Values[2] * 16 + Values[3] ), static_cast<uint8>( Values[4] * 16 + Values[5] ),
						Digits == 8 ? static_cast<uint8>( Values[6] * 16 + Values[7] ) : static_cast<uint8>( 255 ) );
		return true;
	}

	// "{Binding 名前}" なら、名前を返す
	bool ParseBinding( const FString& Text, FString& OutName )
	{
		const FString Trimmed = Trim( Text );
		if ( Trimmed.Len() < 3 || ( *Trimmed )[0] != '{' || ( *Trimmed )[Trimmed.Len() - 1] != '}' )
		{
			return false;
		}
		const FString Inner = Trim( Trimmed.Mid( 1, Trimmed.Len() - 2 ) );
		if ( !Inner.StartsWith( "Binding" ) )
		{
			return false;
		}
		OutName = Trim( Inner.Mid( 7 ) );
		return !OutName.IsEmpty();
	}

	// "{Material パス}" なら、パスを返す（例: {Material Engine:/Materials/UI/GlowButton.gxmaterial}）
	bool ParseMaterialReference( const FString& Text, FString& OutPath )
	{
		const FString Trimmed = Trim( Text );
		if ( Trimmed.Len() < 3 || ( *Trimmed )[0] != '{' || ( *Trimmed )[Trimmed.Len() - 1] != '}' )
		{
			return false;
		}
		const FString Inner = Trim( Trimmed.Mid( 1, Trimmed.Len() - 2 ) );
		if ( !Inner.StartsWith( "Material " ) )
		{
			return false;
		}
		OutPath = Trim( Inner.Mid( 9 ) );
		return !OutPath.IsEmpty();
	}

	bool ParseAlignment( const FString& Text, EUIAlignment& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "start" || Value == "left" || Value == "top" )
		{
			Out = EUIAlignment::Start;
		}
		else if ( Value == "center" )
		{
			Out = EUIAlignment::Center;
		}
		else if ( Value == "end" || Value == "right" || Value == "bottom" )
		{
			Out = EUIAlignment::End;
		}
		else if ( Value == "stretch" )
		{
			Out = EUIAlignment::Stretch;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseOrientation( const FString& Text, EUIOrientation& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "horizontal" )
		{
			Out = EUIOrientation::Horizontal;
		}
		else if ( Value == "vertical" )
		{
			Out = EUIOrientation::Vertical;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseVisibility( const FString& Text, EUIVisibility& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "visible" )
		{
			Out = EUIVisibility::Visible;
		}
		else if ( Value == "hidden" )
		{
			Out = EUIVisibility::Hidden;
		}
		else if ( Value == "collapsed" )
		{
			Out = EUIVisibility::Collapsed;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseImageMode( const FString& Text, EUIImageMode& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "stretch" )
		{
			Out = EUIImageMode::Stretch;
		}
		else if ( Value == "fit" )
		{
			Out = EUIImageMode::Fit;
		}
		else if ( Value == "fill" )
		{
			Out = EUIImageMode::Fill;
		}
		else if ( Value == "tile" )
		{
			Out = EUIImageMode::Tile;
		}
		else if ( Value == "nineslice" )
		{
			Out = EUIImageMode::NineSlice;
		}
		else
		{
			return false;
		}
		return true;
	}

	// アンカーの名前（9 か所と、全体）
	bool ParseAnchor( const FString& Text, FVector2& OutMin, FVector2& OutMax, FVector2& OutPivot )
	{
		const FString Value = Trim( Text ).ToLower();
		struct FPreset
		{
			const char* Name;
			float32 X;
			float32 Y;
		};
		static const FPreset Presets[] = { { "topleft", 0.0f, 0.0f }, { "top", 0.5f, 0.0f }, { "topright", 1.0f, 0.0f }, { "left", 0.0f, 0.5f }, { "center", 0.5f, 0.5f },
										   { "right", 1.0f, 0.5f }, { "bottomleft", 0.0f, 1.0f }, { "bottom", 0.5f, 1.0f }, { "bottomright", 1.0f, 1.0f } };
		for ( const FPreset& Preset : Presets )
		{
			if ( Value == Preset.Name )
			{
				OutMin = OutMax = OutPivot = FVector2( Preset.X, Preset.Y );
				return true;
			}
		}
		if ( Value == "stretch" )
		{
			OutMin = FVector2( 0.0f, 0.0f );
			OutMax = FVector2( 1.0f, 1.0f );
			OutPivot = FVector2( 0.0f, 0.0f );
			return true;
		}
		return false;
	}

	// 列・行の指定: "100, Auto, *, 2*"（数 = 画素・Auto = 中身・* = 残りを 1・n* = 残りを n）
	bool ParseTracks( const FString& Text, TArray<FUITrack>& Out )
	{
		Out.Empty();
		const TArray<FString> Parts = SplitList( Text );
		for ( int32 Index = 0; Index < Parts.Num(); ++Index )
		{
			const FString Part = Parts[Index].ToLower();
			if ( Part == "auto" )
			{
				Out.Add( FUITrack::MakeAuto() );
			}
			else if ( Part == "*" )
			{
				Out.Add( FUITrack::MakeWeight( 1.0f ) );
			}
			else if ( Part.Len() >= 2 && ( *Part )[Part.Len() - 1] == '*' )
			{
				float32 Weight = 0.0f;
				if ( !ParseFloat( Part.Left( Part.Len() - 1 ), Weight ) || Weight <= 0.0f )
				{
					return false;
				}
				Out.Add( FUITrack::MakeWeight( Weight ) );
			}
			else
			{
				float32 Pixels = 0.0f;
				if ( !ParseFloat( Part, Pixels ) || Pixels < 0.0f )
				{
					return false;
				}
				Out.Add( FUITrack::MakeFixed( Pixels ) );
			}
		}
		return true;
	}

	enum class EKind : uint8
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
}

// ---- 部品の木を作る -----------------------------------------------------
struct FMarkupBuilder
{
	FUIMarkupContext& Context;
	CUIBindingSet& Bindings;
	FUIMarkupResult& Result;

	FMarkupBuilder( FUIMarkupContext& InContext, CUIBindingSet& InBindings, FUIMarkupResult& InResult ) : Context( InContext ), Bindings( InBindings ), Result( InResult ) {}

	// "@名前" の、見つからなかった名前（直前の ResolveColor / ResolveFloat が失敗した理由）
	FString MissingToken;

	// 色: "@名前"（テーマ）か、#RGB・#RRGGBB・#RRGGBBAA
	bool ResolveColor( const FString& Text, FColor32& Out )
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
	bool ResolveFloat( const FString& Text, float32& Out )
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

	void Warn( const FUIXmlNode& Node, const FString& Message )
	{
		Result.Warnings.Add( FString::Printf( "line %d: <%s> %s", Node.Line, *Node.Name, *Message ) );
	}

	bool Fail( const FUIXmlNode& Node, const FString& Message )
	{
		if ( Result.Error.IsEmpty() )
		{
			Result.Error = FString::Printf( "line %d: <%s> %s", Node.Line, *Node.Name, *Message );
		}
		return false;
	}

	bool BadValue( const FUIXmlNode& Node, const FUIXmlAttribute& Attribute, const char* Expected )
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
	TSharedPtr<CFont> FindFont( const FUIXmlNode& Node, const FString& Name )
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
	bool FindBrush( const FString& Value, FUIBrush& Out )
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

	TCallable<void()> FindHandler( const FUIXmlNode& Node, const FString& Name )
	{
		if ( const TCallable<void()>* Found = Context.Handlers.Find( Name ) )
		{
			return *Found;
		}
		Warn( Node, FString::Printf( "the handler '%s' is not registered", *Name ) );
		return TCallable<void()>();
	}

	// 要素を作る
	bool Create( const FUIXmlNode& Node, EKind& OutKind, TSharedPtr<CUIWidget>& OutWidget )
	{
		const FString& Name = Node.Name;
		if ( Name == "Panel" )
		{
			OutKind = EKind::Panel;
			OutWidget = MakeShared<CUIWidget>();
		}
		else if ( Name == "Canvas" )
		{
			OutKind = EKind::Canvas;
			OutWidget = MakeShared<CUICanvasPanel>();
		}
		else if ( Name == "VerticalStack" || Name == "HorizontalStack" || Name == "Stack" )
		{
			OutKind = EKind::Stack;
			TSharedPtr<CUIStackPanel> Stack = MakeShared<CUIStackPanel>();
			Stack->SetOrientation( Name == "HorizontalStack" ? EUIOrientation::Horizontal : EUIOrientation::Vertical );
			OutWidget = Stack;
		}
		else if ( Name == "Grid" )
		{
			OutKind = EKind::Grid;
			OutWidget = MakeShared<CUIGridPanel>();
		}
		else if ( Name == "ScrollBox" )
		{
			OutKind = EKind::ScrollBox;
			OutWidget = MakeShared<CUIScrollBox>();
		}
		else if ( Name == "Label" )
		{
			OutKind = EKind::Label;
			OutWidget = MakeShared<CUILabel>();
		}
		else if ( Name == "Image" )
		{
			OutKind = EKind::Image;
			OutWidget = MakeShared<CUIImage>();
		}
		else if ( Name == "ProgressBar" )
		{
			OutKind = EKind::ProgressBar;
			OutWidget = MakeShared<CUIProgressBar>();
		}
		else if ( Name == "Button" )
		{
			OutKind = EKind::Button;
			OutWidget = MakeShared<CUIButton>();
		}
		else if ( Name == "CheckBox" )
		{
			OutKind = EKind::CheckBox;
			OutWidget = MakeShared<CUICheckBox>();
		}
		else if ( Name == "Slider" )
		{
			OutKind = EKind::Slider;
			OutWidget = MakeShared<CUISlider>();
		}
		else if ( Name == "TextBox" )
		{
			OutKind = EKind::TextBox;
			OutWidget = MakeShared<CUITextBox>();
		}
		else if ( Name == "ListView" )
		{
			OutKind = EKind::ListView;
			OutWidget = MakeShared<CUIListView>();
		}
		else if ( Name == "Dropdown" )
		{
			OutKind = EKind::Dropdown;
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

	// ---- 共通の属性 ----
	// 戻り値: 見つけた属性か（見つけて、値が壊れていたら、Fail を設定する）。bOk に、成功・失敗
	bool ApplyCommon( const FUIXmlNode& Node, const FUIXmlAttribute& A, CUIWidget& Widget, FUILayoutProperties& L, bool& bOk )
	{
		const FString& N = A.Name;
		float32 Number = 0.0f;
		bool bFlag = false;
		FVector2 Vector;
		FUIMargin Margin;
		bOk = true;
		if ( N == "Name" )
		{
			Widget.SetName( A.Value );
		}
		else if ( N == "Tooltip" )
		{
			Widget.SetTooltip( A.Value );
		}
		else if ( N == "Width" || N == "Height" || N == "MinWidth" || N == "MinHeight" || N == "MaxWidth" || N == "MaxHeight" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 0.0f )
			{
				bOk = BadValue( Node, A, "a number of 0 or more" );
				return true;
			}
			if ( N == "Width" )
			{
				L.FixedWidth = Number;
			}
			else if ( N == "Height" )
			{
				L.FixedHeight = Number;
			}
			else if ( N == "MinWidth" )
			{
				L.MinSize.x = Number;
			}
			else if ( N == "MinHeight" )
			{
				L.MinSize.y = Number;
			}
			else if ( N == "MaxWidth" )
			{
				L.MaxSize.x = Number;
			}
			else
			{
				L.MaxSize.y = Number;
			}
		}
		else if ( N == "Margin" || N == "Padding" || N == "Offsets" )
		{
			if ( !ParseMargin( A.Value, Margin ) || ( N == "Offsets" && ResolveFloat( A.Value, Number ) ) )
			{
				// Offsets は、4 個の数だけ（1 個・2 個は、意味が曖昧）
				bOk = BadValue( Node, A, N == "Offsets" ? "four numbers: left, top, right, bottom" : "1, 2 or 4 numbers" );
				return true;
			}
			if ( N == "Margin" )
			{
				L.Margin = Margin;
			}
			else if ( N == "Padding" )
			{
				L.Padding = Margin;
			}
			else
			{
				TArray<float32> Four;
				if ( !ParseFloatList( A.Value, Four ) || Four.Num() != 4 )
				{
					bOk = BadValue( Node, A, "four numbers: left, top, right, bottom" );
					return true;
				}
				L.Offsets = Margin;
			}
		}
		else if ( N == "HAlign" || N == "VAlign" )
		{
			EUIAlignment Alignment = EUIAlignment::Stretch;
			if ( !ParseAlignment( A.Value, Alignment ) )
			{
				bOk = BadValue( Node, A, "Start, Center, End or Stretch" );
				return true;
			}
			( N == "HAlign" ? L.HorizontalAlignment : L.VerticalAlignment ) = Alignment;
		}
		else if ( N == "Weight" )
		{
			if ( !ResolveFloat( A.Value, Number ) || Number < 0.0f )
			{
				bOk = BadValue( Node, A, "a number of 0 or more" );
				return true;
			}
			L.Weight = Number;
		}
		else if ( N == "Column" || N == "Row" || N == "ColumnSpan" || N == "RowSpan" )
		{
			int32 Integer = 0;
			if ( !ParseInt( A.Value, Integer ) || Integer < 0 || ( N.EndsWith( "Span" ) && Integer < 1 ) )
			{
				bOk = BadValue( Node, A, "a whole number (a span is 1 or more)" );
				return true;
			}
			( N == "Column" ? L.Column : ( N == "Row" ? L.Row : ( N == "ColumnSpan" ? L.ColumnSpan : L.RowSpan ) ) ) = Integer;
		}
		else if ( N == "Anchor" )
		{
			if ( !ParseAnchor( A.Value, L.AnchorMin, L.AnchorMax, L.Pivot ) )
			{
				bOk = BadValue( Node, A, "TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight or Stretch" );
				return true;
			}
		}
		else if ( N == "AnchorMin" || N == "AnchorMax" || N == "Pivot" )
		{
			if ( !ParseVector2( A.Value, Vector ) )
			{
				bOk = BadValue( Node, A, "two numbers: x, y" );
				return true;
			}
			( N == "AnchorMin" ? L.AnchorMin : ( N == "AnchorMax" ? L.AnchorMax : L.Pivot ) ) = Vector;
		}
		else if ( N == "Visibility" )
		{
			EUIVisibility Visibility = EUIVisibility::Visible;
			if ( !ParseVisibility( A.Value, Visibility ) )
			{
				bOk = BadValue( Node, A, "Visible, Hidden or Collapsed" );
				return true;
			}
			Widget.SetVisibility( Visibility );
		}
		else if ( N == "Opacity" )
		{
			if ( !ResolveFloat( A.Value, Number ) )
			{
				bOk = BadValue( Node, A, "a number from 0 to 1" );
				return true;
			}
			Widget.SetOpacity( Number );
		}
		else if ( N == "Clip" || N == "HitTestable" || N == "Interactive" || N == "Focusable" )
		{
			if ( !ParseBool( A.Value, bFlag ) )
			{
				bOk = BadValue( Node, A, "true or false" );
				return true;
			}
			if ( N == "Clip" )
			{
				Widget.SetClipChildren( bFlag );
			}
			else if ( N == "HitTestable" )
			{
				Widget.SetHitTestable( bFlag );
			}
			else if ( N == "Interactive" )
			{
				Widget.SetInteractive( bFlag );
			}
			else
			{
				Widget.SetFocusable( bFlag );
			}
		}
		else if ( N == "Background" )
		{
			FUIBrush Brush;
			if ( !FindBrush( A.Value, Brush ) )
			{
				Warn( Node, FString::Printf( "the background '%s' is neither a #color nor a registered brush", *A.Value ) );
			}
			else
			{
				Widget.SetBackground( Brush );
			}
		}
		else if ( N == "Enabled" || N == "Visible" )
		{
			// 値が真偽なら、そのまま。{Binding 名前} は、あとの段階で結ぶ
			FString BindingName;
			if ( ParseBinding( A.Value, BindingName ) )
			{
				return true;
			}
			if ( !ParseBool( A.Value, bFlag ) )
			{
				bOk = BadValue( Node, A, "true, false or {Binding name}" );
				return true;
			}
			if ( N == "Enabled" )
			{
				Widget.SetEnabled( bFlag );
			}
			else
			{
				Widget.SetVisibility( bFlag ? EUIVisibility::Visible : EUIVisibility::Collapsed );
			}
		}
		else
		{
			return false;
		}
		return true;
	}

	// 共通の属性の名前か（要素ごとの属性を、先に調べるとき、重ならないように）
	static bool IsCommonName( const FString& N )
	{
		static const char* Names[] = { "Name", "Width", "Height", "MinWidth", "MinHeight", "MaxWidth", "MaxHeight", "Margin", "Padding", "Offsets", "HAlign", "VAlign", "Weight", "Column", "Row",
									   "ColumnSpan", "RowSpan", "Anchor", "AnchorMin", "AnchorMax", "Pivot", "Visibility", "Opacity", "Clip", "HitTestable", "Interactive", "Focusable",
									   "Background", "Enabled", "Visible", "Tooltip" };
		for ( const char* Name : Names )
		{
			if ( N == Name )
			{
				return true;
			}
		}
		return false;
	}

	// ---- 結び付け（共通の Enabled / Visible） ----
	bool BindBool( const FUIXmlNode& Node, const FString& BindingName, const TSharedPtr<CUIWidget>& Widget, bool bVisible )
	{
		TObservable<bool>* const* Found = Context.Bools.Find( BindingName );
		if ( Found == nullptr )
		{
			Warn( Node, FString::Printf( "the bool binding '%s' is not registered", *BindingName ) );
			return true;
		}
		if ( bVisible )
		{
			Bindings.BindVisible( **Found, Widget );
		}
		else
		{
			Bindings.BindEnabled( **Found, Widget );
		}
		return true;
	}

	// ---- 要素ごとの属性 ----
	// 戻り値: 続けてよいか。見つけた属性は bHandled を true に。値が壊れていれば、Fail
	bool ApplySpecific( const FUIXmlNode& Node, const FUIXmlAttribute& A, EKind Kind, const TSharedPtr<CUIWidget>& Widget, FUILayoutProperties& L, bool& bHandled )
	{
		bHandled = true;
		const FString& N = A.Name;
		float32 Number = 0.0f;
		bool bFlag = false;
		switch ( Kind )
		{
		case EKind::Stack:
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
		case EKind::Grid:
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
		case EKind::ScrollBox:
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
		case EKind::Label:
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
		case EKind::Image:
		{
			// Source・Mode・NineSlice・Tint は、まとめて、あとで（SetupImage）
			if ( N == "Source" || N == "Mode" || N == "NineSlice" || N == "Tint" )
			{
				return true;
			}
			break;
		}
		case EKind::ProgressBar:
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
		case EKind::Button:
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
		case EKind::CheckBox:
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
		case EKind::Slider:
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
		case EKind::TextBox:
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
		case EKind::ListView:
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
		case EKind::Dropdown:
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
	bool SetupScrollBox( const FUIXmlNode& Node, CUIScrollBox& Scroll )
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

	bool SetupImage( const FUIXmlNode& Node, CUIImage& Image )
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

	bool SetupButton( const FUIXmlNode& Node, CUIButton& Button )
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

	bool SetupSlider( const FUIXmlNode& Node, const TSharedPtr<CUISlider>& Slider )
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

	bool SetupDropdown( const FUIXmlNode& Node, const TSharedPtr<CUIDropdown>& Dropdown )
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

	bool SetupList( const FUIXmlNode& Node, const TSharedPtr<CUIListView>& List )
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

	// ---- 結び付け（{Binding ...}）----
	bool ApplyBindings( const FUIXmlNode& Node, EKind Kind, const TSharedPtr<CUIWidget>& Widget )
	{
		// ドロップダウンは、項目の結び付けを先に（選ぶ番号は、項目が入ってから決まるので、属性の並びに左右されないように）
		if ( Kind == EKind::Dropdown )
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
			else if ( Kind == EKind::Label && N == "Text" )
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
			else if ( Kind == EKind::TextBox && N == "Text" )
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
			else if ( Kind == EKind::CheckBox && N == "Checked" )
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
			else if ( ( Kind == EKind::Slider || Kind == EKind::ProgressBar ) && N == "Value" )
			{
				TObservable<float32>* const* Found = Context.Floats.Find( BindingName );
				if ( Found == nullptr )
				{
					Warn( Node, FString::Printf( "the number binding '%s' is not registered", *BindingName ) );
				}
				else if ( Kind == EKind::Slider )
				{
					Bindings.BindValue( **Found, StaticCastSharedPtr<CUISlider>( Widget ) );
				}
				else
				{
					Bindings.BindProgress( **Found, StaticCastSharedPtr<CUIProgressBar>( Widget ) );
				}
			}
			else if ( Kind == EKind::Button && N == "Command" )
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
			else if ( ( Kind == EKind::ListView && N == "Items" ) )
			{
				// SetupList で済み
			}
			else if ( Kind == EKind::Dropdown && N == "Items" )
			{
				// 先に済ませた
			}
			else if ( Kind == EKind::Dropdown && N == "SelectedIndex" )
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
	void ApplyHandlers( const FUIXmlNode& Node, EKind Kind, const TSharedPtr<CUIWidget>& Widget )
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
		case EKind::Button:
			AddClicked( "OnClicked", Widget->OnClicked );
			break;
		case EKind::CheckBox:
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
		case EKind::Slider:
			if ( const FString* Value = Node.FindAttribute( "OnValueChanged" ) )
			{
				const TCallable<void()> Handler = FindHandler( Node, *Value );
				if ( Handler )
				{
					StaticCastSharedPtr<CUISlider>( Widget )->OnValueChanged.Add( [Handler]( CUIWidget&, float32 ) { Handler(); } );
				}
			}
			break;
		case EKind::TextBox:
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
		case EKind::Dropdown:
			if ( const FString* Value = Node.FindAttribute( "OnSelectionChanged" ) )
			{
				const TCallable<void()> Handler = FindHandler( Node, *Value );
				if ( Handler )
				{
					StaticCastSharedPtr<CUIDropdown>( Widget )->OnSelectionChanged.Add( [Handler]( CUIWidget&, int32 ) { Handler(); } );
				}
			}
			break;
		case EKind::ListView:
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

	// ---- 1 つの要素と、その子 ----
	TSharedPtr<CUIWidget> Build( const FUIXmlNode& Node )
	{
		EKind Kind = EKind::Panel;
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
		case EKind::ScrollBox:
			bOk = SetupScrollBox( Node, static_cast<CUIScrollBox&>( *Widget ) );
			break;
		case EKind::Image:
			bOk = SetupImage( Node, static_cast<CUIImage&>( *Widget ) );
			break;
		case EKind::Button:
			bOk = SetupButton( Node, static_cast<CUIButton&>( *Widget ) );
			break;
		case EKind::Slider:
			bOk = SetupSlider( Node, StaticCastSharedPtr<CUISlider>( Widget ) );
			break;
		case EKind::ListView:
			bOk = SetupList( Node, StaticCastSharedPtr<CUIListView>( Widget ) );
			break;
		case EKind::Dropdown:
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
			TSharedPtr<CUIWidget> Child = Build( *Node.Children[Index] );
			if ( !Child )
			{
				return TSharedPtr<CUIWidget>();
			}
			Widget->AddChild( Child );
		}

		ApplyBindings( Node, Kind, Widget );
		ApplyHandlers( Node, Kind, Widget );
		return Widget;
	}
};

FUIMarkupResult CUIMarkupLoader::Load( const FString& Markup, FUIMarkupContext& Context, CUIBindingSet& Bindings )
{
	FUIMarkupResult Result;
	const FUIXmlResult Xml = NUIXml::Parse( Markup );
	if ( !Xml.bSuccess )
	{
		Result.Error = Xml.Error;
		return Result;
	}

	// 失敗したとき、途中まで結んだ結び付けが残らないよう、いまの数を覚えておく（失敗なら、この回の分を戻すため、別の組で作ってから移す）
	FMarkupBuilder Builder( Context, Bindings, Result );
	TSharedPtr<CUIWidget> Root = Builder.Build( *Xml.Root );
	if ( !Root )
	{
		if ( Result.Error.IsEmpty() )
		{
			Result.Error = "the markup could not be built";
		}
		return Result;
	}
	if ( Context.bWarningsAreErrors && !Result.Warnings.IsEmpty() )
	{
		Result.Error = Result.Warnings[0];
		return Result;
	}
	Result.bSuccess = true;
	Result.Root = Root;
	return Result;
}
