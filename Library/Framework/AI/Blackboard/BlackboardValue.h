// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// ブラックボードの値の型
//=============================================================================

// キーの型
enum class EBlackboardKeyType : uint8
{
	Bool,
	Int,
	Float,
	Vector3,
	Name,      // 状態名・タグ（文字列）
	Object,    // CGameObject への参照。ポインターではなく GetId() の値で持つ
	Enum,      // 名前つきの選択肢（定義側に一覧を持つ）。内部は選択肢の番号
};

namespace NBlackboard
{
	const char* ToString( EBlackboardKeyType Type );
	bool TryParseKeyType( const FString& Text, EBlackboardKeyType& OutType );
}

// キーを指す値（名前を引いた結果。毎フレームの文字列比較をしないための番号）
struct FBlackboardKey
{
	int32 Index = -1;

	constexpr FBlackboardKey() = default;

	explicit constexpr FBlackboardKey( int32 InIndex )
		: Index( InIndex )
	{
	}

	constexpr bool IsValid() const
	{
		return Index >= 0;
	}

	friend constexpr bool operator==( const FBlackboardKey& A, const FBlackboardKey& B )
	{
		return A.Index == B.Index;
	}

	friend constexpr bool operator!=( const FBlackboardKey& A, const FBlackboardKey& B )
	{
		return A.Index != B.Index;
	}
};

// 値 1 つ。どの欄を使うかはキーの型で決まる（Bool / Int / Enum は Integer、Float は Real、Vector3 は Vector、Name は Text、Object は ObjectId）
struct FBlackboardValue
{
	int64 Integer = 0;
	float32 Real = 0.0f;
	FVector3 Vector = FVector3::Zero;
	FString Text;
	uint64 ObjectId = 0;

	static FBlackboardValue MakeBool( bool bValue );
	static FBlackboardValue MakeInt( int32 Value );
	static FBlackboardValue MakeFloat( float32 Value );
	static FBlackboardValue MakeVector( const FVector3& Value );
	static FBlackboardValue MakeName( const FString& Value );
	static FBlackboardValue MakeObject( uint64 Id );
	static FBlackboardValue MakeEnum( int32 Index );

	// 型 Type の値として、同じか
	bool IsEqual( EBlackboardKeyType Type, const FBlackboardValue& Other ) const;
};
