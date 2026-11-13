// SPDX-License-Identifier: MIT
#include "UIMarkupBuilder.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

using namespace NUIMarkupValue;

// FUIMarkupBuilder: どの要素にもある属性（配置・見え方など）と、Enabled / Visible の結び付け

// ---- 共通の属性 ----
// 戻り値: 見つけた属性か（見つけて、値が壊れていたら、Fail を設定する）。bOk に、成功・失敗
bool FUIMarkupBuilder::ApplyCommon( const FUIXmlNode& Node, const FUIXmlAttribute& A, CUIWidget& Widget, FUILayoutProperties& L, bool& bOk )
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
	else if ( N == "Translation" || N == "TransformPivot" )
	{
		if ( !ParseVector2( A.Value, Vector ) )
		{
			bOk = BadValue( Node, A, "two numbers: x, y" );
			return true;
		}
		FUIRenderTransform Transform = Widget.GetRenderTransform();
		( N == "Translation" ? Transform.Translation : Transform.Pivot ) = Vector;
		Widget.SetRenderTransform( Transform );
	}
	else if ( N == "Scale" )
	{
		// 1 個（縦横とも）か、2 個（横・縦）
		TArray<float32> Values;
		if ( !ParseFloatList( A.Value, Values ) || ( Values.Num() != 1 && Values.Num() != 2 ) )
		{
			bOk = BadValue( Node, A, "one number, or two numbers: x, y" );
			return true;
		}
		FUIRenderTransform Transform = Widget.GetRenderTransform();
		Transform.Scale = FVector2( Values[0], Values.Num() == 2 ? Values[1] : Values[0] );
		Widget.SetRenderTransform( Transform );
	}
	else if ( N == "Rotation" )
	{
		// マークアップでは、度で書く
		if ( !ResolveFloat( A.Value, Number ) )
		{
			bOk = BadValue( Node, A, "a number (degrees)" );
			return true;
		}
		FUIRenderTransform Transform = Widget.GetRenderTransform();
		Transform.Rotation = NMath::ToRadians( Number );
		Widget.SetRenderTransform( Transform );
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
bool FUIMarkupBuilder::IsCommonName( const FString& N )
{
	static const char* Names[] = { "Name", "Width", "Height", "MinWidth", "MinHeight", "MaxWidth", "MaxHeight", "Margin", "Padding", "Offsets", "HAlign", "VAlign", "Weight", "Column", "Row",
								   "ColumnSpan", "RowSpan", "Anchor", "AnchorMin", "AnchorMax", "Pivot", "Visibility", "Opacity", "Clip", "HitTestable", "Interactive", "Focusable",
								   "Background", "Enabled", "Visible", "Tooltip", "Translation", "Scale", "Rotation", "TransformPivot" };
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
bool FUIMarkupBuilder::BindBool( const FUIXmlNode& Node, const FString& BindingName, const TSharedPtr<CUIWidget>& Widget, bool bVisible )
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
