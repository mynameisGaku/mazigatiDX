// SPDX-License-Identifier: MIT
#include "UIMarkupLoader.h"

#include "Library/Framework/UI/Markup/UIMarkupBuilder.h"

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
	FUIMarkupBuilder Builder( Context, Bindings, Result );
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
