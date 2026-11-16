// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

//=============================================================================
// NWindowsMediaDecoder
//   Windows の Media Foundation で、音のファイル（mp3・m4a・aac・wma・flac など、OS が読める形式）を、生の音（FPcmBuffer）に読み出す。
//   チャンネルは最大 2（それより多い音は、OS の変換でステレオにする）。WAV は、フレームワークが自分で読む（NWavFile）のでここでは扱わない。
//   描画 API を知らない。どのスレッドからでも呼べる（COM を、呼んだスレッドで初期化する）。
//=============================================================================
namespace NWindowsMediaDecoder
{
	// Path（絶対パス）を読む。読めなければ false（OutPcm は変えない）
	bool Decode( const FString& Path, FPcmBuffer& OutPcm );

	// Decode で読める拡張子（".mp3" のように、ドットつき・小文字。WAV は含まない）
	TArray<FString> GetReadableExtensions();
}
