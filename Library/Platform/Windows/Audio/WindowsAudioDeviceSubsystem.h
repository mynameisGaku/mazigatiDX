// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Subsystem/AppSubsystem.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;

//=============================================================================
// CWindowsAudioDeviceSubsystem
//   Windows の音の出力（XAudio2 のエンジンと、出力先の「マスタリングボイス」）を 1 つだけ持つ。音の再生（CWindowsAudioBackendSubsystem）と
//   波形の流し込み（CWindowsPcmStreamBackendSubsystem）が、これを借りる。描画 API も DxLib も知らない。
//   音の出る機器が無い（聞こえない環境・検査の機械）ときは、エンジンが作れず IsAvailable() が false になる。借りる側は、何もせず失敗を返す。
//=============================================================================
class CWindowsAudioDeviceSubsystem : public CAppSubsystem
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	// 音の出力を始める（Initialize が呼ぶ。検査が、サブシステムの集まりなしで始めるのにも使う）
	void Start();
	void Deinitialize() override;

	// 出力できる状態か
	bool IsAvailable() const;
	IXAudio2* GetEngine() const;
	// 出力先のチャンネル数（使えなければ 0）
	int32 GetOutputChannels() const;

private:
	IXAudio2* Engine = nullptr;
	IXAudio2MasteringVoice* Master = nullptr;
	int32 OutputChannels = 0;
	bool bComInitialized = false;
};
