// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_PLATFORM_SDLGAMEPADINPUT_H
#define FRAMEWORK_PLATFORM_SDLGAMEPADINPUT_H

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

class IMouseInput;
struct SDL_Gamepad;
struct SDL_Joystick;

// SDL の機器ハンドルを所有するアプリ入力。描画や SDL ウィンドウは作らない。
class ASdlGamepadInput : public CAppSubsystem, public IGamepadInput, public ITickable
{
public:
	// 開いた機器をすべて閉じる。
	~ASdlGamepadInput() override;
	// プラットフォームとフォーカス入力を先に初期化する。
	void Initialize( FSubsystemCollection& Collection ) override;
	// 振動を停止し、この実装が取得した SDL の利用権だけを解放する。
	void Deinitialize() override;
	// 抜き差しを確認し、一度だけ入力状態を取得する。
	void Tick( float32 DeltaTime ) override;
	// ゲーム処理より前に更新する。
	ETickGroup GetTickGroup() const override;
	// SDL 初期化の成否を返す。
	bool IsAvailable() const override;
	// 接続中の機器情報を返す。
	const TArray<FGamepadInfo>& GetDevices() const override;
	// 不明な ID は nullptr を返す。
	const FGamepadState* GetState( uint32 DeviceId ) const override;
	// この更新の接続一覧を返す。
	const TArray<uint32>& GetConnectedThisFrame() const override;
	// この更新の切断一覧を返す。
	const TArray<uint32>& GetDisconnectedThisFrame() const override;
	// 未対応の測定器は false を返す。
	bool EnableSensor( uint32 DeviceId, EGamepadSensor Sensor, bool bEnabled ) override;
	// 利用不可な本体振動は false を返す。
	bool Rumble( uint32 DeviceId, float32 Low, float32 High, uint32 Milliseconds ) override;
	// 利用不可なトリガー振動は false を返す。
	bool RumbleTriggers( uint32 DeviceId, float32 Left, float32 Right, uint32 Milliseconds ) override;
	// 利用不可なランプは false を返す。
	bool SetLed( uint32 DeviceId, uint8 Red, uint8 Green, uint8 Blue ) override;
	// 配置未登録の USB アダプターなどの割り当てを追加する。
	bool AddMappingsFromFile( const FString& Filename ) override;
	// 別ウィンドウを操作中にも入力するかを指定する。
	void SetBackgroundInputEnabled( bool bEnabled ) override;

private:
	// SDL ハンドルと同じ寿命で保持する機器ごとの記録。
	struct FDevice
	{
		// 列挙用の説明。
		FGamepadInfo Info;
		// 前回から今フレームへの変化を含む値。
		FGamepadState State;
		// 共通配置がある場合の所有ハンドル。
		SDL_Gamepad* Gamepad = nullptr;
		// Gamepad があれば借用、なければ直接所有するハンドル。
		SDL_Joystick* Joystick = nullptr;
	};
	// 未登録 ID は nullptr を返す。
	FDevice* FindDevice( uint32 DeviceId );
	// 開くことに失敗した場合は登録しない。
	void OpenDevice( uint32 DeviceId );
	// 振動停止後、所有方式に応じて閉じる。
	void CloseDevice( FDevice& Device );
	// 能力値と共通配置を機器から読み直す。
	void RefreshInfo( FDevice& Device );
	// 操作可能なら状態を取り込み、それ以外は解放する。
	void ReadState( FDevice& Device, bool bFocused );
	// 依存先が保持するメインウィンドウの操作状態。
	const IMouseInput* FocusInput = nullptr;
	// 終了処理まで所有する接続記録。切断記録は1更新だけ残す。
	TArray<FDevice> Devices;
	// 利用者へ公開する接続中の説明。
	TArray<FGamepadInfo> DeviceInfos;
	// 今回接続した ID。
	TArray<uint32> Connected;
	// 今回切断した ID。
	TArray<uint32> Disconnected;
	// この実装の SDL 初期化が成功したか。
	bool bInitialized = false;
	// ゲーム以外を操作中にも受け付けるか。
	bool bBackgroundInputEnabled = false;
};

using CSdlGamepadInput = ASdlGamepadInput;

#endif
