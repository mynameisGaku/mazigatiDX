// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

class CSound;

//=============================================================================
// NToneMaker
//   このサンプルで鳴らす音を、計算で作る（音のファイルが要らない）。サイン波の、モノラル。
//   計算した波形（FPcmBuffer）を、鳴らせる音（CSound）にするのは、CPcmSoundLoader。
//=============================================================================
namespace NToneMaker
{
	constexpr int32 SampleRate = 44100;

	// 短い音（鍵盤の音）: 立ち上がりと減衰をつけた、サイン波。Frequency は Hz、Seconds は長さ、Volume は 0..1
	FPcmBuffer MakeNote( float32 Frequency, float32 Seconds, float32 Volume );

	// 繰り返し用の音: ちょうど整数回の波で終わるので、つなぎ目で音が途切れない（ループさせても、プチッと鳴らない）
	FPcmBuffer MakeLoopTone( float32 Frequency, float32 Volume );

	// 波形から、鳴らせる音を作る。作れなければ nullptr（音の裏側の部分が無いとき）
	TSharedPtr<CSound> MakeSound( const char* Name, const FPcmBuffer& Pcm );
}
