// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"

class CAssetSubsystem;

//=============================================================================
// ステートマシン / ビヘイビアツリーのコンポーネントが共通で使う、ブラックボードの準備と表示
//=============================================================================
namespace NAI
{
	// ブラックボードのアセット（.gxblackboard）を読み込んで、インスタンスを作る。パスが空・読めないときは、空のブラックボード（警告を出す）。
	// Owner は、警告に出す持ち主の名前（ステートマシン / ビヘイビアツリーのパス）
	TSharedPtr<CBlackboard> CreateBlackboard( CAssetSubsystem& Assets, const FString& BlackboardPath, const FString& Owner );

	// ブラックボードの全キーの値を、1 行ずつ「  名前 = 値」の形にして、Prefix を行頭に付けて返す
	FString DescribeBlackboard( const CBlackboard& Blackboard, const char* Prefix );
}
