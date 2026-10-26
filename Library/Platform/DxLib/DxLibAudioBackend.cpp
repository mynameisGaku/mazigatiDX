#include "DxLibAudioBackend.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogAudioBackend, Log )

REGISTER_SUBSYSTEM( CDxLibAudioBackend )

void CDxLibAudioBackend::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogAudioBackend, Error, "No platform is available. Sounds will not be played." );
	}
}

bool CDxLibAudioBackend::Play( int32 Voice, bool bLoop, bool bFromStart )
{
	return PlaySoundMem( Voice, bLoop ? DX_PLAYTYPE_LOOP : DX_PLAYTYPE_BACK, bFromStart ? TRUE : FALSE ) == 0;
}

void CDxLibAudioBackend::Stop( int32 Voice )
{
	StopSoundMem( Voice );
}

bool CDxLibAudioBackend::IsPlaying( int32 Voice )
{
	return CheckSoundMem( Voice ) == 1;
}

void CDxLibAudioBackend::SetVolume( int32 Voice, float32 Volume )
{
	// DxLib の値は 0（無音）..10000（元の大きさ）で、100 分の 1 デシベル単位
	const int32 Pal = NMath::Clamp( 10000 + NAudio::VolumeToDecibel100( NMath::Min( Volume, 1.0f ) ), 0, 10000 );
	SetVolumeSoundMem( Pal, Voice );
}

void CDxLibAudioBackend::SetPan( int32 Voice, float32 Pan )
{
	// 遠い側の音量を (1 - |Pan|) 倍にする。DxLib の値は、正で右寄り・負で左寄り（100 分の 1 デシベル単位）
	const float32 Far = NMath::Max( 1.0f - NMath::Abs( Pan ), 1.0e-5f );
	const int32 Attenuation = NMath::Min( 10000, static_cast<int32>( std::lround( -2000.0f * std::log10( Far ) ) ) );
	SetPanSoundMem( Pan >= 0.0f ? Attenuation : -Attenuation, Voice );
}

void CDxLibAudioBackend::SetPitch( int32 Voice, float32 Pitch )
{
	// 元の周波数を基準にする（前に変えた周波数を基準にしない）
	ResetFrequencySoundMem( Voice );
	if ( NMath::Abs( Pitch - 1.0f ) < 1.0e-4f ) return;

	const int Base = GetFrequencySoundMem( Voice );
	if ( Base > 0 ) SetFrequencySoundMem( NMath::Clamp( static_cast<int>( std::lround( static_cast<float64>( Base ) * Pitch ) ), 100, 100000 ), Voice );
}

float32 CDxLibAudioBackend::GetPlayPosition( int32 Voice )
{
	return static_cast<float32>( GetSoundCurrentTime( Voice ) ) / 1000.0f;
}

void CDxLibAudioBackend::SetPlayPosition( int32 Voice, float32 Seconds )
{
	SetSoundCurrentTime( static_cast<LONGLONG>( NMath::Max( Seconds, 0.0f ) * 1000.0f ), Voice );
}
