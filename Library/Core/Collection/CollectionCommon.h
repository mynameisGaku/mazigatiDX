#pragma once
#include <cassert>

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// コンテナ共通定義
//=============================================================================

// 検索で見つからなかったときのインデックス
inline constexpr int32 INDEX_NONE = -1;

// 文字列検索の大文字・小文字の扱い
enum class ESearchCase : uint8
{
	CaseSensitive,  // 区別する
	IgnoreCase,     // 区別しない（ASCII のみ）
};

// 範囲外アクセスなどのチェック（Debug のみ）
#define COLLECTION_CHECK( Expr, Message ) assert( ( Expr ) && Message )
