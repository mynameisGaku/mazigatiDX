// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/AudioBackend.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

struct IXAudio2SourceVoice;
class CWindowsAudioDeviceSubsystem;
class CWindowsSoundStoreSubsystem;

//=============================================================================
// CWindowsAudioBackendSubsystem
//   IAudioBackend の、Windows（XAudio2）の実装。DxLib も描画 API も知らない。
//   ボイスの番号は、CWindowsSoundStoreSubsystem の音のハンドル。ボイスごとに、再生のとき XAudio2 の「ソースボイス」を 1 つ作り、
//   音のデータ（FPcmBuffer）をそのまま渡して鳴らす（コピーしない。再生が終わるまで、データは共有で生かす）。
//   - 音量・パン・ピッチは、鳴らす前に決めたものも覚えておき、ソースボイスができたときに反映する。パンは、反対側の音を弱める（-1..1）
//   - 位置は、再生位置の計算で出す（止めたときの位置を覚えて、Play( ..., false ) で続きから）。ループ区間は、XAudio2 のループで鳴らす
//   - 音が無い・出力の機器が無いときは、Play が false。メインスレッドだけから使う
//=============================================================================
class CWindowsAudioBackendSubsystem : public CAppSubsystem, public IAudioBackend, public ITickable
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	// 音の出力と音の登録を借りる（Initialize が呼ぶ。検査が、自分で作ったものを渡すのにも使う）
	void Bind( CWindowsAudioDeviceSubsystem* InDevice, CWindowsSoundStoreSubsystem* InSounds );
	void Deinitialize() override;
	// 音が消されたボイスの、ソースボイスを片づける
	void Tick( float32 DeltaTime ) override;

	bool Play( int32 Voice, bool bLoop, bool bFromStart ) override;
	void Stop( int32 Voice ) override;
	bool IsPlaying( int32 Voice ) override;
	void SetVolume( int32 Voice, float32 Volume ) override;
	void SetPan( int32 Voice, float32 Pan ) override;
	void SetPitch( int32 Voice, float32 Pitch ) override;
	float32 GetPlayPosition( int32 Voice ) override;
	void SetPlayPosition( int32 Voice, float32 Seconds ) override;
	bool SetLoopRegion( int32 Voice, float32 StartSeconds, float32 EndSeconds ) override;

private:
	struct FVoice
	{
		IXAudio2SourceVoice* Source = nullptr;
		TSharedPtr<const FPcmBuffer> Data; // 鳴らしている間、生かす
		bool bPlaying = false; // Play してから、Stop・終了までの間
		bool bLoop = false;
		float32 Volume = 1.0f;
		float32 Pan = 0.0f;
		float32 Pitch = 1.0f;
		int64 PositionFrames = 0; // 止まっているときの位置（フレーム）
		int64 StartFrames = 0; // 鳴らし始めた位置
		uint64 BaseSamplesPlayed = 0; // 鳴らし始めたときの、ソースボイスの再生済みサンプル数
		int64 LoopStart = 0;
		int64 LoopEnd = 0; // 0 以下は、音の最後まで
		bool bLoopRegionSet = false;
	};

	FVoice* FindOrCreate( int32 Voice );
	void DestroySource( FVoice& State );
	// 今の位置（フレーム）。鳴らしていれば、ソースボイスの再生済みから計算する
	int64 ComputePosition( const FVoice& State ) const;
	// StartFrames から鳴らし直す（ソースボイスを作り直さず、キューを空にして積み直す）
	bool Submit( FVoice& State, int64 StartFrame );
	void ApplyMix( const FVoice& State ) const;

	CWindowsAudioDeviceSubsystem* Device = nullptr;
	CWindowsSoundStoreSubsystem* Sounds = nullptr;
	TMap<int32, FVoice> Voices;
};
