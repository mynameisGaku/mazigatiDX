// SPDX-License-Identifier: MIT
#include "UITheme.h"

bool FUITheme::SetParent( const TSharedPtr<FUITheme>& InParent )
{
	// 輪（自分が、親の祖先にいる）になる親は、断る
	for ( const FUITheme* Theme = InParent.Get(); Theme != nullptr; Theme = Theme->Parent.Get() )
	{
		if ( Theme == this )
		{
			return false;
		}
	}
	Parent = InParent;
	return true;
}

FColor32 FUITheme::GetColor( const FString& TokenName, const FColor32& Default ) const
{
	FColor32 Color;
	return FindColor( TokenName, Color ) ? Color : Default;
}

float32 FUITheme::GetNumber( const FString& TokenName, float32 Default ) const
{
	float32 Value = 0.0f;
	return FindNumber( TokenName, Value ) ? Value : Default;
}
