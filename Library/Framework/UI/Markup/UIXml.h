// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"

//=============================================================================
// UI のマークアップ（.gxui）のための、小さな XML の読み取り
//   要素・属性・文字・自己閉じのタグ・コメント・CDATA・文字参照（&lt; &gt; &amp; &quot; &apos; &#65; &#x41;）を読む。
//   名前空間・DTD・処理命令（<?xml ... ?> は読み飛ばす）は扱わない。壊れた入力は、行番号つきの理由を返す（クラッシュしない）。
//
//   FUIXmlResult Result = NUIXml::Parse( Text );
//   if ( Result.bSuccess ) { const FUIXmlNode& Root = *Result.Root; ... }
//=============================================================================
struct FUIXmlAttribute
{
	FString Name;
	FString Value;
};

struct FUIXmlNode
{
	FString Name;
	int32 Line = 0;                                     // 開きタグの行（1 から）
	TArray<FUIXmlAttribute> Attributes;
	TArray<TSharedPtr<FUIXmlNode>> Children;
	FString Text;                                       // 直下の文字（子の間の文字は、つなげる。前後の空白は除く）

	// 属性の値（無ければ null）
	const FString* FindAttribute( const char* AttributeName ) const;
	bool HasAttribute( const char* AttributeName ) const { return FindAttribute( AttributeName ) != nullptr; }
	// 属性の値（無ければ Default）
	FString GetAttribute( const char* AttributeName, const FString& Default = FString() ) const;
};

struct FUIXmlResult
{
	bool bSuccess = false;
	TSharedPtr<FUIXmlNode> Root;
	FString Error;     // 失敗の理由（英語。行番号を含む）
	int32 ErrorLine = 0;
};

namespace NUIXml
{
	// XML の文字列を読む。ルートの要素は 1 つだけ。入れ子は、100 段まで
	FUIXmlResult Parse( const FString& Text );
}
