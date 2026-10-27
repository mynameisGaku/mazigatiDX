// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_INPUT_IGAMEPADINPUT_H
#define FRAMEWORK_INPUT_IGAMEPADINPUT_H

#include "Library/Core/Interfaces/Input/GamepadInfo.h"
#include "Library/Core/Interfaces/Input/GamepadState.h"

// アプリの寿命で機器を管理する入口。すべてメインスレッドから利用する。
class IGamepadInput
{
public:
	// 派生実装の終了処理を呼ぶ。
	virtual ~IGamepadInput() = default;
	// 入力基盤が初期化できたかを返す。
	virtual bool IsAvailable() const = 0;
	// 接続中の機器を返す。参照は次の入力更新まで有効。
	virtual const TArray<FGamepadInfo>& GetDevices() const = 0;
	// ID が不明なら nullptr。切断したフレームには解放状態を返す。
	virtual const FGamepadState* GetState( uint32 DeviceId ) const = 0;
	// この更新で接続した ID 一覧。参照は次の入力更新まで有効。
	virtual const TArray<uint32>& GetConnectedThisFrame() const = 0;
	// この更新で切断した ID 一覧。参照は次の入力更新まで有効。
	virtual const TArray<uint32>& GetDisconnectedThisFrame() const = 0;
	// 有効化は次の更新から反映。未対応・不明 ID なら false。
	virtual bool EnableSensor( uint32 DeviceId, EGamepadSensor Sensor, bool bEnabled ) = 0;
	// 強度は 0～1 に収める。未対応・非操作中・非数なら false。
	virtual bool Rumble( uint32 DeviceId, float32 Low, float32 High, uint32 Milliseconds ) = 0;
	// 左右トリガーの強度は 0～1。利用不可なら false。
	virtual bool RumbleTriggers( uint32 DeviceId, float32 Left, float32 Right, uint32 Milliseconds ) = 0;
	// 色を 0～255 で指定。利用不可なら false。
	virtual bool SetLed( uint32 DeviceId, uint8 Red, uint8 Green, uint8 Blue ) = 0;
	// SDL 形式の配置ファイルを追加し、次の更新で反映。読込失敗なら false。
	virtual bool AddMappingsFromFile( const FString& Filename ) = 0;
	// 非操作中にも入力を受けるかを指定。既定は false。
	virtual void SetBackgroundInputEnabled( bool bEnabled ) = 0;
};

#endif
