// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/PcmStreamBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibPcmStreamBackendSubsystem
//   IPcmStreamBackend の DxLib 実装。アプリで 1 つ、自動で生成される。
//   ゲーム側は直接使わず、CAudioSubsystem::CreatePcmStream で作った CPcmStream を通して使う。
//
//   作る -> MakeSoftSoundPlayerCustom( チャンネル数, 16 ビット, サンプルレート )
//   足す -> AddDirectDataSoftSoundPlayer（-1..1 の浮動小数を 16 ビットの整数に直して渡す）
//   残り -> GetStockDataLengthSoftSoundPlayer（再生用の領域に移す前の分 + 領域に入っている分）
//   Start / Stop / Reset / 再生中か -> StartSoftSoundPlayer / StopSoftSoundPlayer / ResetSoftSoundPlayer / CheckStartSoftSoundPlayer
//
//   DxLib の制約: チャンネル数は 1 か 2 だけ（それ以外は作れず、ログに出る）
//   DxLib の癖: 作った直後と Reset の直後は、再生用の領域が無音で満たされた状態（足した波形ではない）に見える。
//   その量を覚えておき、残りには数えない（鳴らし始めると、無音はすぐに消え、足した波形の端数だけが領域に移る）
//=============================================================================
class CDxLibPcmStreamBackendSubsystem : public CAppSubsystem, public IPcmStreamBackend
{
public:
	// --backend=null のときは作らない（Library/Platform/Null の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;

	int32 CreatePcmStream( int32 SampleRate, int32 Channels ) override;
	void DeletePcmStream( int32 Stream ) override;
	bool QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount ) override;
	int32 GetPcmStreamQueuedFrames( int32 Stream ) override;
	bool StartPcmStream( int32 Stream ) override;
	void StopPcmStream( int32 Stream ) override;
	void ResetPcmStream( int32 Stream ) override;
	bool IsPcmStreamPlaying( int32 Stream ) override;

private:
	// 作った直後 / Reset の直後に、再生用の領域に入っている無音の量（ストリームごと）
	int32 ReadSilentFill( int32 Stream ) const;

	TMap<int32, int32> SilentFill;
};
