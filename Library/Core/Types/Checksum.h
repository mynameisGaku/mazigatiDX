// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NChecksum
//   データの壊れを見つけるためのチェックサム。状態を持たない計算の関数なので、名前空間（NMath や NFile と同じ扱い）。
//
//   uint32 Crc = NChecksum::Crc32( Bytes.GetData(), Bytes.Num() );
//   // 続きを足すとき: Crc = NChecksum::Crc32( More, MoreSize, Crc );
//
//   - CRC-32（IEEE 802.3。zip・PNG・イーサネットと同じ）。"123456789" のチェックサムは 0xCBF43926
//   - 暗号の強さは無い（改ざんの検出には使えない。保存の途中で切れた・書き換わった、を見つける用途）
//=============================================================================
namespace NChecksum
{
	// Data の Size バイトの CRC-32。Previous は、前の続きから計算するときの、前までの結果（最初は 0）
	uint32 Crc32( const void* Data, usize Size, uint32 Previous = 0 );
}
