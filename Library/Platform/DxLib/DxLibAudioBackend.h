#pragma once
#include "Library/Core/Interfaces/Audio/AudioBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibAudioBackend
//   IAudioBackend の DxLib 実装。アプリで 1 つ、自動で生成される。
//   ゲーム側は直接使わず、CAudioSystem を通して鳴らす。
//
//   Play -> PlaySoundMem / Stop -> StopSoundMem / IsPlaying -> CheckSoundMem
//   音量 -> SetVolumeSoundMem（デシベル。0..1 の振幅を 20 * log10 で換算）
//   パン -> SetPanSoundMem（遠い側の音量を (1 - |パン|) 倍にする）
//   ピッチ -> SetFrequencySoundMem（元の周波数 x 倍率）
//   再生位置 -> GetSoundCurrentTime / SetSoundCurrentTime
//=============================================================================
class CDxLibAudioBackend : public CAppSubsystem, public IAudioBackend
{
public:
	void Initialize( FSubsystemCollection& Collection ) override;

	bool Play( int32 Voice, bool bLoop, bool bFromStart ) override;
	void Stop( int32 Voice ) override;
	bool IsPlaying( int32 Voice ) override;

	void SetVolume( int32 Voice, float32 Volume ) override;
	void SetPan( int32 Voice, float32 Pan ) override;
	void SetPitch( int32 Voice, float32 Pitch ) override;

	float32 GetPlayPosition( int32 Voice ) override;
	void SetPlayPosition( int32 Voice, float32 Seconds ) override;
};
