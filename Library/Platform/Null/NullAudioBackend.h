// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/AudioBackend.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullAudioBackend
//   IAudioBackend の、DxLib を使わない実装。音は出さず、ボイスごとの状態（再生中か・音量・パン・ピッチ・位置）を覚えるだけ。
//   --backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   - 時間は進まない: Play したボイスは、Stop するまで再生中のまま。ループしない音も、終わらない
//   - どのボイスの番号（0 以上）も受け付ける。負の番号は、再生できない（Play は false）
//   - 音量・パン・ピッチは、IAudioBackend が決めた範囲に収めずに、受け取った値のまま覚える
//=============================================================================
class CNullAudioBackend : public CAppSubsystem, public IAudioBackend
{
public:
	// ボイス 1 つの状態
	struct FVoiceState
	{
		bool bPlaying = false;
		bool bLoop = false;
		float32 Volume = 1.0f;
		float32 Pan = 0.0f;
		float32 Pitch = 1.0f;
		float32 Position = 0.0f;
		// SetLoopRegion で決めた区間（終わりが 0 以下は音の最後まで）
		float32 LoopStart = 0.0f;
		float32 LoopEnd = 0.0f;
		bool bLoopRegionSet = false;
	};

	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	bool Play( int32 Voice, bool bLoop, bool bFromStart ) override;
	void Stop( int32 Voice ) override;
	bool IsPlaying( int32 Voice ) override;

	void SetVolume( int32 Voice, float32 Volume ) override;
	void SetPan( int32 Voice, float32 Pan ) override;
	void SetPitch( int32 Voice, float32 Pitch ) override;

	float32 GetPlayPosition( int32 Voice ) override;
	void SetPlayPosition( int32 Voice, float32 Seconds ) override;
	bool SetLoopRegion( int32 Voice, float32 StartSeconds, float32 EndSeconds ) override;

	//-------------------------------------------------------------------------
	// Null 固有（確認用）
	//-------------------------------------------------------------------------
	// ボイスの状態。使われていない番号は、初めの状態
	FVoiceState GetVoiceState( int32 Voice ) const;
	// Play が成功した回数
	int32 GetNumPlayCalls() const;

private:
	TMap<int32, FVoiceState> Voices;
	int32 NumPlayCalls = 0;
};
