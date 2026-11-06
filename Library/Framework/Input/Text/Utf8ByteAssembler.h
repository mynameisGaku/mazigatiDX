// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FUtf8ByteAssembler
//   1 バイトずつ届く UTF-8 を、1 文字（コードポイント）ずつにまとめる。
//   バックエンドによっては、全角の文字が 2 回以上に分かれて届くので、その吸収に使う（どのバックエンドでも同じ）。
//
//   FUtf8ByteAssembler Assembler;
//   Assembler.Push( Byte, Text );   // 1 文字が完成したときだけ Text の末尾に足される
//
//   - 制御文字（0x20 未満と 0x7F）と、不正なバイト列は捨てる。途中で別の先頭バイトが来たら、途中のものは捨てて数え直す
//   - 短すぎる表現（C0 / C1 など）、サロゲート、U+10FFFF より大きい値は不正として捨てる
//=============================================================================
class FUtf8ByteAssembler
{
public:
	// 1 バイトを渡す。1 文字が完成して OutText に足したら true
	bool Push( uint8 Byte, FString& OutText );

	// 途中のバイトを捨てる
	void Reset();

	// 1 文字の途中か
	bool IsPending() const
	{
		return PendingCount > 0;
	}

private:
	uint8 Pending[4] = {};
	int32 PendingCount = 0;
	int32 ExpectedCount = 0;
};
