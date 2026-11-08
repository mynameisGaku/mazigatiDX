// SPDX-License-Identifier: MIT
#include "UIMarkupDocument.h"

#include "Library/Core/File/File.h"

bool CUIMarkupDocument::ReadText( CAssetSubsystem& Assets, const FString& Path, FString& OutText )
{
	OutText.Empty();
	const TOptional<FString> Resolved = Assets.ResolvePath( Path );
	if ( !Resolved.IsSet() )
	{
		return false;
	}
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Resolved.GetValue(), Bytes ) )
	{
		return false;
	}
	int32 Start = 0;
	if ( Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF )
	{
		Start = 3;
	}
	// 文字列は、終端の 0 を持つので、途中の 0 を含むファイルは、そこで切れる（XML として壊れて、失敗になる）
	TArray<char> Chars;
	for ( int32 Index = Start; Index < Bytes.Num(); ++Index )
	{
		Chars.Add( static_cast<char>( Bytes[Index] ) );
	}
	Chars.Add( '\0' );
	OutText = FString( Chars.GetData() );
	return true;
}

bool CUIMarkupDocument::Build( const FString& Text, FUIMarkupContext& Context, CUIBindingSet& Bindings )
{
	// 新しい木は、いったん別の組に結ぶ。成功したときだけ、古い結び付けと入れ替える
	CUIBindingSet Fresh;
	const FUIMarkupResult Result = CUIMarkupLoader::Load( Text, Context, Fresh );
	if ( !Result.bSuccess )
	{
		LastError = Result.Error;
		return false;
	}
	Bindings.Clear();
	Bindings.Absorb( Fresh );
	Root = Result.Root;
	Warnings = Result.Warnings;
	LastError.Empty();
	LoadedText = Text;
	return true;
}

bool CUIMarkupDocument::Load( CAssetSubsystem& Assets, const FString& InPath, FUIMarkupContext& Context, CUIBindingSet& Bindings )
{
	Path = InPath;
	FString Text;
	if ( !ReadText( Assets, Path, Text ) )
	{
		LastError = FString::Printf( "the markup file '%s' could not be read", *Path );
		return false;
	}
	return Build( Text, Context, Bindings );
}

bool CUIMarkupDocument::ReloadIfChanged( CAssetSubsystem& Assets, FUIMarkupContext& Context, CUIBindingSet& Bindings )
{
	FString Text;
	if ( Path.IsEmpty() || !ReadText( Assets, Path, Text ) || Text == LoadedText )
	{
		return false;
	}
	if ( !Build( Text, Context, Bindings ) )
	{
		// 壊れた中身を覚えておき、同じ失敗を毎回やり直さない（直されたら、中身が変わるので、また試す）
		LoadedText = Text;
		return false;
	}
	return true;
}
