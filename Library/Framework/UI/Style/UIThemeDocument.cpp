// SPDX-License-Identifier: MIT
#include "UIThemeDocument.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/UI/Markup/UIMarkupDocument.h"

bool CUIThemeDocument::Load( CAssetSubsystem& Assets, const FString& InPath, const TSharedPtr<FUITheme>& Parent )
{
	FString Text;
	if ( !CUIMarkupDocument::ReadText( Assets, InPath, Text ) )
	{
		LastError = FString::Printf( "the theme file '%s' could not be read", *InPath );
		return false;
	}
	FString Error;
	const TSharedPtr<FUITheme> Loaded = CUIThemeLoader::LoadFromText( Text, &Assets, Parent, Error );
	if ( !Loaded )
	{
		LastError = FString::Printf( "%s: %s", *InPath, *Error );
		return false;
	}
	Path = InPath;
	LoadedText = Text;
	Theme = Loaded;
	LastError.Empty();
	return true;
}

bool CUIThemeDocument::ReloadIfChanged( CAssetSubsystem& Assets )
{
	FString Text;
	if ( !Theme || Path.IsEmpty() || !CUIMarkupDocument::ReadText( Assets, Path, Text ) || Text == LoadedText )
	{
		return false;
	}
	// 壊れた中身でも覚えておき、同じ失敗を毎回やり直さない（直されたら、中身が変わるので、また試す）
	LoadedText = Text;
	FString Error;
	const TSharedPtr<FUITheme> Loaded = CUIThemeLoader::LoadFromText( Text, &Assets, Theme->GetParent(), Error );
	if ( !Loaded )
	{
		LastError = FString::Printf( "%s: %s", *Path, *Error );
		return false;
	}
	// 同じテーマのまま、中身だけを入れ替える（このテーマを持っている文脈や、子のテーマが、そのまま新しい中身を見る）
	*Theme = *Loaded;
	LastError.Empty();
	return true;
}
