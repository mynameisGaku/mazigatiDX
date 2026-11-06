// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

class IPcmStreamBackend;

//=============================================================================
// CPcmStream
//   ゲームが実行時に作った波形を、順に流して鳴らす（手続き的な音・シンセ・独自のデコーダーの出力など）。
//   CAudioSubsystem::CreatePcmStream で作る。ゲームスレッドから使う。
//
//   TSharedPtr<CPcmStream> Stream = Audio->CreatePcmStream( 44100, 1 );
//   Stream->Submit( Samples, FrameCount );     // 波形を足す（-1..1。チャンネルは交互）
//   Stream->Start();
//   // 毎フレーム: 足りなくならないように、残りを見て足す
//   if ( Stream->GetQueuedSeconds() < 0.2f ) { Stream->Submit( ... ); }
//
//   - 足りなくなると無音になる。止まらずに待つので、あとで足せば続きから鳴る
//   - 音量・パン・ピッチはない。波形を作る側で決める（流す前に掛ける）。ミキサーのバスにも入らない
//   - 鳴らす窓口（バックエンド）が対応しない形（チャンネル数など）は、作れない（ログに出る）
//   - CAudioSubsystem が終わるときに、残っているストリームは使えなくなる（IsValid が false）
//=============================================================================
class CPcmStream
{
public:
	// 作れなければ nullptr。普通は CAudioSubsystem::CreatePcmStream を使う
	static TSharedPtr<CPcmStream> Create( IPcmStreamBackend& Backend, int32 SampleRate, int32 Channels );

	~CPcmStream();
	CPcmStream( const CPcmStream& ) = delete;
	CPcmStream& operator=( const CPcmStream& ) = delete;

	// 使える状態か（バックエンドが無くなったら false）
	bool IsValid() const;

	int32 GetSampleRate() const { return SampleRate; }
	int32 GetChannels() const { return Channels; }

	// 波形を末尾に足す。FrameCount は 1 チャンネルあたりのフレーム数。-1..1 を超えた分は切る。足せなければ false
	bool Submit( const float32* Samples, int32 FrameCount );
	// 生の音（FPcmBuffer）を足す。サンプルレートとチャンネル数が、このストリームと同じでなければ足さない
	bool Submit( const FPcmBuffer& Pcm );

	// 鳴らし始める / 止める（足した波形は残る）/ 足した波形を捨てて止める
	bool Start();
	void Stop();
	void Reset();
	bool IsPlaying() const;

	// まだ鳴らしていない波形の量（フレーム数 / 秒）
	int32 GetQueuedFrames() const;
	float32 GetQueuedSeconds() const;

	// CAudioSubsystem が、終わるときに呼ぶ。以降は使えなくなる
	void Detach();

private:
	CPcmStream( IPcmStreamBackend& InBackend, int32 InHandle, int32 InSampleRate, int32 InChannels );

	IPcmStreamBackend* Backend = nullptr;
	int32 Handle = -1;
	int32 SampleRate = 0;
	int32 Channels = 0;
};
