// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

//=============================================================================
// NWavFile
//   WAV ファイルのバイト列と、生の音（FPcmBuffer）の変換。状態を持たない変換の関数なので、名前空間（NMath や NFile と同じ扱い）。
//   バックエンドに依らない（どのバックエンドでも、PCM の保存と、WAV の読み込みに使える）。呼び出す役は CPcmSoundLoader。
//
//   TArray<uint8> Bytes;
//   NWavFile::Encode( Pcm, Bytes );                 // 16 ビット PCM の WAV にする
//   FPcmBuffer Read;
//   NWavFile::Decode( Bytes.GetData(), Bytes.Num(), Read );
//
//   - 読めるのは、整数の PCM（8 / 16 / 24 / 32 ビット）と、32 ビットの浮動小数（WAVE_FORMAT_EXTENSIBLE を含む）。圧縮された WAV は読めない
//   - 書くのは 16 ビット PCM（-1..1 を 32767 倍して丸め、範囲の外は切る。数でない値は無音）
//   - 1〜8 チャンネルを扱う
//=============================================================================
namespace NWavFile
{
	// Pcm を WAV にして、OutBytes に入れる（OutBytes は空にしてから）。Pcm が音として使えなければ false
	bool Encode( const FPcmBuffer& Pcm, TArray<uint8>& OutBytes );

	// WAV の中身を読む。読めなければ false（OutPcm は変えない）
	bool Decode( const uint8* Data, usize Size, FPcmBuffer& OutPcm );
}
