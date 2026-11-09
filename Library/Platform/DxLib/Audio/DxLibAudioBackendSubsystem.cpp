#include "DxLibAudioBackendSubsystem.h"
#include "Library/Platform/DxLib/DxLibBackendSubsystem.h"


#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CDxLibAudioBackendSubsystem )

bool CDxLibAudioBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DxLib;
}

void CDxLibAudioBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogPlatform, Error, "No platform is available. Sounds will not be played." );
	}
}

bool CDxLibAudioBackendSubsystem::Play( int32 Voice, bool bLoop, bool bFromStart )
{
	return PlaySoundMem( Voice, bLoop ? DX_PLAYTYPE_LOOP : DX_PLAYTYPE_BACK, bFromStart ? TRUE : FALSE ) == 0;
}

void CDxLibAudioBackendSubsystem::Stop( int32 Voice )
{
	StopSoundMem( Voice );
}

bool CDxLibAudioBackendSubsystem::IsPlaying( int32 Voice )
{
	return CheckSoundMem( Voice ) == 1;
}

void CDxLibAudioBackendSubsystem::SetVolume( int32 Voice, float32 Volume )
{
	// DxLib の値は 0（無音）..10000（元の大きさ）で、100 分の 1 デシベル単位
	const int32 Pal = NMath::Clamp( 10000 + NAudio::VolumeToDecibel100( NMath::Min( Volume, 1.0f ) ), 0, 10000 );
	SetVolumeSoundMem( Pal, Voice );
}

void CDxLibAudioBackendSubsystem::SetPan( int32 Voice, float32 Pan )
{
	// 遠い側の音量を (1 - |Pan|) 倍にする。DxLib の値は、正で右寄り・負で左寄り（100 分の 1 デシベル単位）
	const float32 Far = NMath::Max( 1.0f - NMath::Abs( Pan ), 1.0e-5f );
	const int32 Attenuation = NMath::Min( 10000, static_cast<int32>( NMath::RoundToInt( -2000.0f * NMath::Log10( Far ) ) ) );
	SetPanSoundMem( Pan >= 0.0f ? Attenuation : -Attenuation, Voice );
}

void CDxLibAudioBackendSubsystem::SetPitch( int32 Voice, float32 Pitch )
{
	// 元の周波数を基準にする（前に変えた周波数を基準にしない）
	ResetFrequencySoundMem( Voice );
	if ( NMath::Abs( Pitch - 1.0f ) < 1.0e-4f )
	{
		return;
	}

	const int Base = GetFrequencySoundMem( Voice );
	if ( Base > 0 )
	{
		SetFrequencySoundMem( NMath::Clamp( static_cast<int>( NMath::RoundToInt( static_cast<float64>( Base ) * Pitch ) ), 100, 100000 ), Voice );
	}
}

float32 CDxLibAudioBackendSubsystem::GetPlayPosition( int32 Voice )
{
	return static_cast<float32>( GetSoundCurrentTime( Voice ) ) / 1000.0f;
}

bool CDxLibAudioBackendSubsystem::SetLoopRegion( int32 Voice, float32 StartSeconds, float32 EndSeconds )
{
	// DxLib のループ区間は、ミリ秒で始まりと終わりを別々に決める。関数の名前から想像するのと逆で、実測では次のとおり
	// （SetLoopTimePosSoundMem = 区間の始まり（終わりに届いたら戻る位置）、SetLoopStartTimePosSoundMem = 区間の終わり。呼ぶ順番は関係ない）。
	// 終わりが 0 以下なら、音の最後（全体の長さ）にする。
	// DxLib の制約: 区間の始まりは、再生を始める位置（SetPlayPosition で動かした位置）より後でないと効かない。頭から鳴らす BGM では問題にならない
	const LONGLONG Total = GetSoundTotalTime( Voice );
	if ( Total <= 0 )
	{
		return false;
	}
	const LONGLONG Start = static_cast<LONGLONG>( NMath::Max( StartSeconds, 0.0f ) * 1000.0f );
	const LONGLONG End = EndSeconds > 0.0f ? static_cast<LONGLONG>( EndSeconds * 1000.0f ) : Total;
	if ( SetLoopTimePosSoundMem( Start < Total ? Start : 0, Voice ) != 0 )
	{
		return false;
	}
	return SetLoopStartTimePosSoundMem( End < Total ? End : Total, Voice ) == 0;
}

void CDxLibAudioBackendSubsystem::SetPlayPosition( int32 Voice, float32 Seconds )
{
	SetSoundCurrentTime( static_cast<LONGLONG>( NMath::Max( Seconds, 0.0f ) * 1000.0f ), Voice );
}
