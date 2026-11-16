// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Asset/SoundBackend.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsSoundStoreSubsystem
//   音のハンドルと、デコード済みの音（FPcmBuffer）の対応を持つ（ISoundBackend）。描画 API も DxLib も知らない。
//   WAV は NWavFile、それ以外（mp3・m4a・flac など）は Media Foundation（NWindowsMediaDecoder）で読む。
//   読み込みは、同期で済ませる（bAsync でも、返ったときには読み終わっている）。Stream でも、全部をメモリに展開する。
//   2 チャンネルを超える音は、ステレオにして持つ。DuplicateSound は、同じデータを共有する別のハンドルを作る。
//   再生は CWindowsAudioBackendSubsystem（IAudioBackend）が、このハンドル（= ボイス）で行う。メインスレッドだけから使う。
//=============================================================================
class CWindowsSoundStoreSubsystem : public CAppSubsystem, public ISoundBackend
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Deinitialize() override;

	int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) override;
	int32 DuplicateSound( int32 Source ) override;
	void DeleteSound( int32 Sound ) override;
	float32 GetSoundLength( int32 Sound ) override;
	bool GetSoundInfo( int32 Sound, FSoundInfo& Out ) override;
	int32 CreateSoundFromPcm( const FPcmBuffer& Pcm ) override;
	bool DecodeSoundFile( const FString& Path, FPcmBuffer& Out ) override;
	TArray<FString> GetReadableSoundExtensions() const override;

	// 音のデータ。無いハンドルは空
	TSharedPtr<const FPcmBuffer> Find( int32 Sound ) const;
	// メモリに持っている、おおよそのバイト数。無いハンドルは 0
	int64 EstimateBytes( int32 Sound ) const;
	// 生きているハンドルの数
	int32 Num() const;

private:
	int32 Add( const TSharedPtr<const FPcmBuffer>& Data );

	TMap<int32, TSharedPtr<const FPcmBuffer>> Sounds;
	int32 NextHandle = 1;
};
