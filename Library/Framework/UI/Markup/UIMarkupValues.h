// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Markup/UIMarkupLoader.h"

//=============================================================================
// NUIMarkupValue: マークアップの属性の文字を、値にする（数・色・余白・並べ方など）
//   読めたら true。読めなければ false（Out は、変わらないか、途中まで）。テーマの "@名前" は、ここでは扱わない（FUIMarkupBuilder が引く）。
//   マークアップの読み込み（UIMarkup*.cpp）の中だけで使う。
//=============================================================================
namespace NUIMarkupValue
{
	// ---- 値の読み取り -----------------------------------------------------
	FString Trim( const FString& Text );
	bool ParseFloat( const FString& Text, float32& Out );
	bool ParseInt( const FString& Text, int32& Out );
	bool ParseBool( const FString& Text, bool& Out );
	// "a, b, c" を、コンマで分けて、前後の空白を除く
	TArray<FString> SplitList( const FString& Text );
	bool ParseFloatList( const FString& Text, TArray<float32>& Out );
	// 余白: 1 個（四辺）・2 個（左右・上下）・4 個（左・上・右・下）
	bool ParseMargin( const FString& Text, FUIMargin& Out );
	bool ParseVector2( const FString& Text, FVector2& Out );
	// #RGB・#RRGGBB・#RRGGBBAA
	bool ParseColor( const FString& Text, FColor32& Out );
	// "{Binding 名前}" なら、名前を返す
	bool ParseBinding( const FString& Text, FString& OutName );
	// "{Material パス}" なら、パスを返す（例: {Material Engine:/Materials/UI/GlowButton.gxmaterial}）
	bool ParseMaterialReference( const FString& Text, FString& OutPath );
	bool ParseAlignment( const FString& Text, EUIAlignment& Out );
	bool ParseOrientation( const FString& Text, EUIOrientation& Out );
	bool ParseVisibility( const FString& Text, EUIVisibility& Out );
	bool ParseImageMode( const FString& Text, EUIImageMode& Out );
	// アンカーの名前（9 か所と、全体）
	bool ParseAnchor( const FString& Text, FVector2& OutMin, FVector2& OutMax, FVector2& OutPivot );
	// 列・行の指定: "100, Auto, *, 2*"（数 = 画素・Auto = 中身・* = 残りを 1・n* = 残りを n）
	bool ParseTracks( const FString& Text, TArray<FUITrack>& Out );
}
