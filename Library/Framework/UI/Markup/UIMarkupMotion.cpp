// SPDX-License-Identifier: MIT
#include "UIMarkupBuilder.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

using namespace NUIMarkupValue;

// FUIMarkupBuilder: 動き（状態に合わせた動き・<Animation>）

// ---- 動き ----
// 状態に合わせた動きの属性か（HoverScale など。SetupStateMotion が、まとめて読む）
bool FUIMarkupBuilder::IsStateMotionName( const FString& N )
{
	static const char* Names[] = { "HoverScale", "PressedScale", "FocusScale", "HoverOffset", "PressedOffset", "FocusOffset", "MotionDuration", "MotionEasing" };
	for ( const char* Name : Names )
	{
		if ( N == Name )
		{
			return true;
		}
	}
	return false;
}

// 動かす仕組み（CUIAnimator）。文脈に無ければ、警告して null
CUIAnimator* FUIMarkupBuilder::FindAnimator( const FUIXmlNode& Node, const char* What )
{
	if ( Context.Animator == nullptr )
	{
		Warn( Node, FString::Printf( "an animator is not set (SetAnimator), so %s is ignored", What ) );
	}
	return Context.Animator;
}

bool FUIMarkupBuilder::ReadEasing( const FUIXmlNode& Node, const char* AttributeName, EEasing& Out )
{
	const FString* Text = Node.FindAttribute( AttributeName );
	if ( Text == nullptr )
	{
		return true;
	}
	if ( !NEasing::FromString( *Trim( *Text ), Out ) )
	{
		return Fail( Node, FString::Printf( "the attribute %s=\"%s\" is not valid (an easing name like CubicOut expected)", AttributeName, **Text ) );
	}
	return true;
}

// 数の属性を読む（無ければ、Out はそのまま）
bool FUIMarkupBuilder::ReadFloat( const FUIXmlNode& Node, const char* AttributeName, float32& Out, bool& bOutFound )
{
	bOutFound = false;
	const FString* Text = Node.FindAttribute( AttributeName );
	if ( Text == nullptr )
	{
		return true;
	}
	if ( !ResolveFloat( *Text, Out ) )
	{
		MissingToken.Empty();
		return Fail( Node, FString::Printf( "the attribute %s=\"%s\" is not valid (a number expected)", AttributeName, **Text ) );
	}
	bOutFound = true;
	return true;
}

bool FUIMarkupBuilder::ReadVector2( const FUIXmlNode& Node, const char* AttributeName, FVector2& Out, bool& bOutFound )
{
	bOutFound = false;
	const FString* Text = Node.FindAttribute( AttributeName );
	if ( Text == nullptr )
	{
		return true;
	}
	if ( !ParseVector2( *Text, Out ) )
	{
		return Fail( Node, FString::Printf( "the attribute %s=\"%s\" is not valid (two numbers: x, y expected)", AttributeName, **Text ) );
	}
	bOutFound = true;
	return true;
}

// HoverScale・PressedScale・FocusScale・HoverOffset・PressedOffset・FocusOffset・MotionDuration・MotionEasing
bool FUIMarkupBuilder::SetupStateMotion( const FUIXmlNode& Node, const TSharedPtr<CUIWidget>& Widget )
{
	FUIStateMotion Motion;
	bool bAny = false;
	bool bFound = false;
	if ( !ReadFloat( Node, "HoverScale", Motion.HoverScale, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadFloat( Node, "PressedScale", Motion.PressedScale, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadFloat( Node, "FocusScale", Motion.FocusScale, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadVector2( Node, "HoverOffset", Motion.HoverOffset, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadVector2( Node, "PressedOffset", Motion.PressedOffset, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadVector2( Node, "FocusOffset", Motion.FocusOffset, bFound ) )
	{
		return false;
	}
	bAny = bAny || bFound;
	if ( !ReadFloat( Node, "MotionDuration", Motion.Duration, bFound ) )
	{
		return false;
	}
	if ( Motion.Duration < 0.0f )
	{
		return Fail( Node, "the attribute MotionDuration is not valid (a number of 0 or more expected)" );
	}
	if ( !ReadEasing( Node, "MotionEasing", Motion.Easing ) )
	{
		return false;
	}
	if ( !bAny )
	{
		if ( Node.HasAttribute( "MotionDuration" ) || Node.HasAttribute( "MotionEasing" ) )
		{
			Warn( Node, "MotionDuration and MotionEasing do nothing without HoverScale, PressedScale, FocusScale, HoverOffset, PressedOffset or FocusOffset" );
		}
		return true;
	}
	if ( CUIAnimator* Animator = FindAnimator( Node, "the state motion (HoverScale and so on)" ) )
	{
		Animator->SetStateMotion( Widget, Motion );
	}
	return true;
}

// <Animation Property="Opacity" From="0" To="1" Duration="0.3" Delay="0.1" Easing="CubicOut" Loop="Once" />
//   親の要素の部品を、読み込んだときに動かし始める。From を省くと、いまの値から。Rotation は、度で書く
bool FUIMarkupBuilder::SetupAnimation( const FUIXmlNode& Node, const TSharedPtr<CUIWidget>& Widget )
{
	static const char* Known[] = { "Property", "From", "To", "Duration", "Delay", "Easing", "Loop" };
	for ( int32 Index = 0; Index < Node.Attributes.Num(); ++Index )
	{
		bool bKnown = false;
		for ( const char* Name : Known )
		{
			bKnown = bKnown || Node.Attributes[Index].Name == Name;
		}
		if ( !bKnown )
		{
			Warn( Node, FString::Printf( "the attribute '%s' is not known and is ignored", *Node.Attributes[Index].Name ) );
		}
	}
	if ( !Node.Children.IsEmpty() )
	{
		return Fail( Node, "an <Animation> cannot have child elements" );
	}

	const FString* PropertyText = Node.FindAttribute( "Property" );
	if ( PropertyText == nullptr )
	{
		return Fail( Node, "the attribute Property is required (Opacity, TranslationX, TranslationY, Scale, ScaleX, ScaleY or Rotation)" );
	}
	static const char* PropertyNames[] = { "Opacity", "TranslationX", "TranslationY", "Scale", "ScaleX", "ScaleY", "Rotation" };
	static const EUIAnimatedProperty Properties[] = { EUIAnimatedProperty::Opacity, EUIAnimatedProperty::TranslationX, EUIAnimatedProperty::TranslationY, EUIAnimatedProperty::Scale,
													 EUIAnimatedProperty::ScaleX,  EUIAnimatedProperty::ScaleY,       EUIAnimatedProperty::Rotation };
	EUIAnimatedProperty Property = EUIAnimatedProperty::Opacity;
	bool bPropertyFound = false;
	const FString PropertyName = Trim( *PropertyText );
	for ( int32 Index = 0; Index < 7; ++Index )
	{
		if ( PropertyName == PropertyNames[Index] )
		{
			Property = Properties[Index];
			bPropertyFound = true;
		}
	}
	if ( !bPropertyFound )
	{
		return Fail( Node, FString::Printf( "the attribute Property=\"%s\" is not valid (Opacity, TranslationX, TranslationY, Scale, ScaleX, ScaleY or Rotation expected)", **PropertyText ) );
	}

	float32 From = 0.0f;
	float32 To = 0.0f;
	bool bHasFrom = false;
	bool bHasTo = false;
	bool bFound = false;
	if ( !ReadFloat( Node, "From", From, bHasFrom ) || !ReadFloat( Node, "To", To, bHasTo ) )
	{
		return false;
	}
	if ( !bHasTo )
	{
		return Fail( Node, "the attribute To is required" );
	}
	if ( Property == EUIAnimatedProperty::Rotation )
	{
		From = NMath::ToRadians( From );
		To = NMath::ToRadians( To );
	}

	FUIAnimationOptions Options;
	if ( !ReadFloat( Node, "Duration", Options.Duration, bFound ) || !ReadFloat( Node, "Delay", Options.Delay, bFound ) )
	{
		return false;
	}
	if ( Options.Duration < 0.0f || Options.Delay < 0.0f )
	{
		return Fail( Node, "the attributes Duration and Delay are not valid (numbers of 0 or more expected)" );
	}
	if ( !ReadEasing( Node, "Easing", Options.Easing ) )
	{
		return false;
	}
	if ( const FString* LoopText = Node.FindAttribute( "Loop" ) )
	{
		const FString LoopName = Trim( *LoopText );
		if ( LoopName == "Once" )
		{
			Options.Loop = EUIAnimationLoop::Once;
		}
		else if ( LoopName == "Loop" )
		{
			Options.Loop = EUIAnimationLoop::Loop;
		}
		else if ( LoopName == "PingPong" )
		{
			Options.Loop = EUIAnimationLoop::PingPong;
		}
		else
		{
			return Fail( Node, FString::Printf( "the attribute Loop=\"%s\" is not valid (Once, Loop or PingPong expected)", **LoopText ) );
		}
	}

	CUIAnimator* Animator = FindAnimator( Node, "the <Animation>" );
	if ( Animator == nullptr )
	{
		return true;
	}
	if ( bHasFrom )
	{
		Animator->Play( Widget, Property, From, To, Options );
	}
	else
	{
		Animator->PlayTo( Widget, Property, To, Options );
	}
	return true;
}
