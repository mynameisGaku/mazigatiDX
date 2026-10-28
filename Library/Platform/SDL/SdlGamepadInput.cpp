#include "SdlGamepadInput.h"


#include "Library/Core/Interfaces/Input/IMouseInput.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/FileSystem.h"

#include <SDL3/SDL.h>
#pragma comment( lib, "SDL3.lib" )

DEFINE_LOG_CATEGORY_STATIC( LogGamepad, Log )

REGISTER_SUBSYSTEM( ASdlGamepadInput )

namespace
{
	constexpr uint16 VendorNintendo = 0x057Eu;
	constexpr uint16 ProductJoyCon2Left  = 0x2066u;
	constexpr uint16 ProductJoyCon2Right = 0x2067u;
	constexpr uint16 ProductJoyCon2Pair  = 0x2068u;
	constexpr uint16 ProductSwitch2Pro   = 0x2069u;

	SDL_SensorType ToSdlSensor( EGamepadSensor Sensor )
	{
		switch ( Sensor )
		{
		case EGamepadSensor::Accelerometer:      return SDL_SENSOR_ACCEL;
		case EGamepadSensor::Gyroscope:          return SDL_SENSOR_GYRO;
		case EGamepadSensor::AccelerometerLeft:  return SDL_SENSOR_ACCEL_L;
		case EGamepadSensor::GyroscopeLeft:      return SDL_SENSOR_GYRO_L;
		case EGamepadSensor::AccelerometerRight: return SDL_SENSOR_ACCEL_R;
		case EGamepadSensor::GyroscopeRight:     return SDL_SENSOR_GYRO_R;
		default:                                 return SDL_SENSOR_INVALID;
		}
	}

	bool IsSwitch2ControllerName( const FString& Name )
	{
		return Name.Contains( "Switch 2" ) || Name.Contains( "Joy-Con 2" );
	}

	EGamepadType DetermineGamepadType( SDL_Gamepad* Gamepad, uint16 Vendor, uint16 Product, const FString& Name )
	{
		if ( Gamepad == nullptr )
		{
			if ( Vendor == VendorNintendo )
			{
				if ( Product == ProductSwitch2Pro )   return EGamepadType::Switch2Pro;
				if ( Product == ProductJoyCon2Left )  return EGamepadType::JoyCon2Left;
				if ( Product == ProductJoyCon2Right ) return EGamepadType::JoyCon2Right;
				if ( Product == ProductJoyCon2Pair )  return EGamepadType::JoyCon2Pair;
			}
			return EGamepadType::Generic;
		}

		const bool bSwitch2ByIdOrName = ( Vendor == VendorNintendo && ( Product == ProductSwitch2Pro || Product == ProductJoyCon2Left || Product == ProductJoyCon2Right || Product == ProductJoyCon2Pair ) ) || IsSwitch2ControllerName( Name );

		switch ( SDL_GetGamepadType( Gamepad ) )
		{
		case SDL_GAMEPAD_TYPE_STANDARD:
			return EGamepadType::Standard;
		case SDL_GAMEPAD_TYPE_XBOX360:
			return EGamepadType::Xbox360;
		case SDL_GAMEPAD_TYPE_XBOXONE:
			return EGamepadType::XboxOne;
		case SDL_GAMEPAD_TYPE_PS3:
			return EGamepadType::PS3;
		case SDL_GAMEPAD_TYPE_PS4:
			return EGamepadType::PS4;
		case SDL_GAMEPAD_TYPE_PS5:
			return EGamepadType::PS5;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
			return ( ( Vendor == VendorNintendo && Product == ProductSwitch2Pro ) || bSwitch2ByIdOrName )
				? EGamepadType::Switch2Pro
				: EGamepadType::SwitchPro;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
			return ( ( Vendor == VendorNintendo && Product == ProductJoyCon2Left ) || bSwitch2ByIdOrName )
				? EGamepadType::JoyCon2Left
				: EGamepadType::JoyConLeft;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
			return ( ( Vendor == VendorNintendo && Product == ProductJoyCon2Right ) || bSwitch2ByIdOrName )
				? EGamepadType::JoyCon2Right
				: EGamepadType::JoyConRight;
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
			return bSwitch2ByIdOrName ? EGamepadType::JoyCon2Pair : EGamepadType::JoyConPair;
		case SDL_GAMEPAD_TYPE_GAMECUBE:
			return EGamepadType::GameCube;
		case SDL_GAMEPAD_TYPE_STEAM:
			return EGamepadType::Steam;
		default:
			return EGamepadType::Standard;
		}
	}

	float32 NormalizeSignedAxis( Sint16 Raw )
	{
		return Raw < 0
			? static_cast<float32>( Raw ) / 32768.0f
			: static_cast<float32>( Raw ) / 32767.0f;
	}

	float32 NormalizeTriggerAxis( Sint16 Raw )
	{
		return Raw <= 0 ? 0.0f : static_cast<float32>( Raw ) / 32767.0f;
	}

	Uint16 ToRumbleIntensity( float32 Value )
	{
		const float32 Clamped = NMath::Clamp( Value, 0.0f, 1.0f );
		return static_cast<Uint16>( Clamped * 65535.0f + 0.5f );
	}

	bool ContainsJoystickId( const SDL_JoystickID* Ids, int32 Count, uint32 TargetId )
	{
		if ( Ids == nullptr ) return false;
		for ( int32 i = 0; i < Count; ++i )
		{
			if ( static_cast<uint32>( Ids[i] ) == TargetId ) return true;
		}
		return false;
	}
}

#pragma region Lifecycle

ASdlGamepadInput::~ASdlGamepadInput()
{
	Deinitialize();
}

void ASdlGamepadInput::Initialize( FSubsystemCollection& Collection )
{
	Collection.InitializeDependency<IPlatform>();
	FocusInput = Collection.InitializeDependency<IMouseInput>();

	// SDL ウィンドウを作らない構成でも入力が届くようにし、フォーカス判定はこのクラス側で行う
	SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS3, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS4, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS5, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1" );

	bool bSensorsAvailable = true;
	if ( !SDL_InitSubSystem( SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR ) )
	{
		bSensorsAvailable = false;
		LOG( LogGamepad, Warning, "SDL sensor subsystem could not start (%s). Gyro and accelerometer will be unavailable.", SDL_GetError() );
		if ( !SDL_InitSubSystem( SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK ) )
		{
			LOG( LogGamepad, Error, "SDL_InitSubSystem failed: %s", SDL_GetError() );
			bInitialized = false;
			return;
		}
	}

	// イベントキューを使わず毎フレームポーリングするので、内部キューへの蓄積を止める
	SDL_SetJoystickEventsEnabled( false );
	SDL_SetGamepadEventsEnabled( false );

	bInitialized = true;

	const int32 SdlVersion = SDL_GetVersion();
	LOG( LogGamepad, Log, "Initialized (SDL %d.%d.%d, sensors %s, background input %s)", SDL_VERSIONNUM_MAJOR( SdlVersion ),
		 SDL_VERSIONNUM_MINOR( SdlVersion ), SDL_VERSIONNUM_MICRO( SdlVersion ), bSensorsAvailable ? "available" : "unavailable",
		 bBackgroundInputEnabled ? "on" : "off" );

	if ( const TOptional<FString> MappingFile = NFileSystem::FindFile( "Config/gamecontrollerdb.txt" ) )
	{
		AddMappingsFromFile( MappingFile.GetValue() );
	}
	else
	{
		LOG( LogGamepad, Verbose, "Config/gamecontrollerdb.txt not found. Using SDL's built-in controller mappings only." );
	}
}

void ASdlGamepadInput::Deinitialize()
{
	if ( bInitialized ) LOG( LogGamepad, Log, "Shutting down (%d device(s) open)", Devices.Num() );

	for ( FDevice& Device : Devices )
	{
		CloseDevice( Device );
	}
	Devices.Empty();
	DeviceInfos.Empty();
	Connected.Empty();
	Disconnected.Empty();

	if ( bInitialized )
	{
		const SDL_InitFlags ActiveFlags = SDL_WasInit( SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR );
		if ( ActiveFlags != 0 )
		{
			SDL_QuitSubSystem( ActiveFlags );
		}
		bInitialized = false;
	}
}

#pragma endregion

#pragma region Tick

void ASdlGamepadInput::Tick( float32 DeltaTime )
{
	Connected.Reset();
	Disconnected.Reset();

	if ( !bInitialized ) return;

	// 前フレームで切断状態として返した記録を取り除く
	Devices.RemoveAll( []( const FDevice& Device ) { return !Device.State.bConnected; } );

	SDL_UpdateGamepads();
	SDL_UpdateJoysticks();
	SDL_UpdateSensors();

	const bool bFocused = bBackgroundInputEnabled || ( FocusInput != nullptr && FocusInput->GetMouseState().bFocused );

	int RawCount = 0;
	SDL_JoystickID* RawIds = SDL_GetJoysticks( &RawCount );

	// 切断された機器を検出し、このフレームだけ解放状態を残す
	for ( FDevice& Device : Devices )
	{
		const bool bStillPresent = ContainsJoystickId( RawIds, RawCount, Device.Info.Id )
			&& ( Device.Gamepad != nullptr ? SDL_GamepadConnected( Device.Gamepad ) : SDL_JoystickConnected( Device.Joystick ) );

		if ( bStillPresent ) continue;

		LOG( LogGamepad, Log, "Gamepad disconnected: #%u (%s)", Device.Info.Id, *Device.Info.Name );
		Disconnected.Add( Device.Info.Id );
		CloseDevice( Device );

		FGamepadState& State = Device.State;
		State.bConnected = false;
		State.bFocused = bFocused;

		for ( int32 i = 0; i < static_cast<int32>( EGamepadButton::Count ); ++i )
		{
			State.ReleasedButtons[i] = State.Buttons[i];
			State.Buttons[i] = false;
			State.PressedButtons[i] = false;
		}
		for ( int32 i = 0; i < static_cast<int32>( EGamepadAxis::Count ); ++i )
		{
			State.Axes[i] = 0.0f;
		}
		for ( int32 i = 0; i < State.RawButtons.Num(); ++i )
		{
			State.RawReleasedButtons[i] = State.RawButtons[i];
			State.RawButtons[i] = 0u;
			State.RawPressedButtons[i] = 0u;
		}
		for ( int32 i = 0; i < State.RawAxes.Num(); ++i )
		{
			State.RawAxes[i] = 0.0f;
		}
		for ( int32 i = 0; i < State.RawHats.Num(); ++i )
		{
			State.RawHats[i] = 0u;
		}
		for ( int32 i = 0; i < static_cast<int32>( EGamepadSensor::Count ); ++i )
		{
			State.Sensors[i].Values[0] = 0.0f;
			State.Sensors[i].Values[1] = 0.0f;
			State.Sensors[i].Values[2] = 0.0f;
			State.Sensors[i].bValid = false;
		}
	}

	// 新たに接続された機器を開く
	if ( RawIds != nullptr )
	{
		for ( int32 i = 0; i < RawCount; ++i )
		{
			const uint32 Id = static_cast<uint32>( RawIds[i] );
			if ( Id != 0u && FindDevice( Id ) == nullptr )
			{
				OpenDevice( Id );
			}
		}
		SDL_free( RawIds );
	}

	// 接続中の機器の入力状態を更新し、公開用の機器一覧を詰め直す
	DeviceInfos.Reset();
	for ( FDevice& Device : Devices )
	{
		if ( !Device.State.bConnected ) continue;

		// 後からマッピングが追加されて標準配置に対応した場合は昇格させる
		if ( Device.Gamepad == nullptr && SDL_IsGamepad( Device.Info.Id ) )
		{
			if ( SDL_Gamepad* OpenedGamepad = SDL_OpenGamepad( Device.Info.Id ) )
			{
				if ( Device.Joystick != nullptr )
				{
					SDL_CloseJoystick( Device.Joystick );
				}
				Device.Gamepad = OpenedGamepad;
				Device.Joystick = SDL_GetGamepadJoystick( OpenedGamepad );
				RefreshInfo( Device );
			}
		}

		ReadState( Device, bFocused );
		DeviceInfos.Add( Device.Info );
	}
}

#pragma endregion

#pragma region DeviceManagement

ASdlGamepadInput::FDevice* ASdlGamepadInput::FindDevice( uint32 DeviceId )
{
	return Devices.FindByPredicate( [DeviceId]( const FDevice& Device )
	{
		return Device.Info.Id == DeviceId;
	} );
}

void ASdlGamepadInput::OpenDevice( uint32 DeviceId )
{
	SDL_Gamepad* Gamepad = SDL_IsGamepad( DeviceId ) ? SDL_OpenGamepad( DeviceId ) : nullptr;
	SDL_Joystick* Joystick = ( Gamepad != nullptr ) ? SDL_GetGamepadJoystick( Gamepad ) : SDL_OpenJoystick( DeviceId );

	if ( Joystick == nullptr )
	{
		if ( Gamepad != nullptr )
		{
			SDL_CloseGamepad( Gamepad );
		}
		LOG( LogGamepad, Warning, "Failed to open controller #%u: %s", DeviceId, SDL_GetError() );
		return;
	}

	FDevice& Device = Devices.EmplaceGetRef();
	Device.Info.Id = DeviceId;
	Device.Gamepad = Gamepad;
	Device.Joystick = Joystick;
	Device.State.bConnected = true;

	RefreshInfo( Device );

	Device.State.RawButtons.SetNum( Device.Info.NumButtons, 0u );
	Device.State.RawPressedButtons.SetNum( Device.Info.NumButtons, 0u );
	Device.State.RawReleasedButtons.SetNum( Device.Info.NumButtons, 0u );
	Device.State.RawAxes.SetNum( Device.Info.NumAxes, 0.0f );
	Device.State.RawHats.SetNum( Device.Info.NumHats, 0u );

	Connected.Add( DeviceId );
	LOG( LogGamepad, Log, "Gamepad connected: #%u %s (VID:0x%04x PID:0x%04x Mapped:%d)",
	     DeviceId, *Device.Info.Name, Device.Info.Vendor, Device.Info.Product, Device.Info.bMapped ? 1 : 0 );
}

void ASdlGamepadInput::CloseDevice( FDevice& Device )
{
	if ( Device.Gamepad != nullptr )
	{
		SDL_RumbleGamepad( Device.Gamepad, 0u, 0u, 0u );
		SDL_RumbleGamepadTriggers( Device.Gamepad, 0u, 0u, 0u );
		SDL_CloseGamepad( Device.Gamepad );
		Device.Gamepad = nullptr;
		Device.Joystick = nullptr;
	}
	else if ( Device.Joystick != nullptr )
	{
		SDL_RumbleJoystick( Device.Joystick, 0u, 0u, 0u );
		SDL_RumbleJoystickTriggers( Device.Joystick, 0u, 0u, 0u );
		SDL_CloseJoystick( Device.Joystick );
		Device.Joystick = nullptr;
	}
}

void ASdlGamepadInput::RefreshInfo( FDevice& Device )
{
	const char* RawName = ( Device.Gamepad != nullptr )
		? SDL_GetGamepadName( Device.Gamepad )
		: SDL_GetJoystickName( Device.Joystick );
	Device.Info.Name = ( RawName != nullptr && RawName[0] != '\0' )
		? FString( RawName )
		: FString( "Generic Controller" );

	char GuidBuffer[64] = {};
	SDL_GUIDToString( SDL_GetJoystickGUID( Device.Joystick ), GuidBuffer, sizeof( GuidBuffer ) );
	Device.Info.Guid = GuidBuffer;

	Device.Info.Vendor = ( Device.Gamepad != nullptr )
		? SDL_GetGamepadVendor( Device.Gamepad )
		: SDL_GetJoystickVendor( Device.Joystick );
	Device.Info.Product = ( Device.Gamepad != nullptr )
		? SDL_GetGamepadProduct( Device.Gamepad )
		: SDL_GetJoystickProduct( Device.Joystick );
	Device.Info.bMapped = ( Device.Gamepad != nullptr );
	Device.Info.Type = DetermineGamepadType( Device.Gamepad, Device.Info.Vendor, Device.Info.Product, Device.Info.Name );

	const SDL_PropertiesID Props = ( Device.Gamepad != nullptr )
		? SDL_GetGamepadProperties( Device.Gamepad )
		: SDL_GetJoystickProperties( Device.Joystick );
	Device.Info.bRumble = SDL_GetBooleanProperty( Props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false );
	Device.Info.bTriggerRumble = SDL_GetBooleanProperty( Props, SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, false );
	Device.Info.bLed = SDL_GetBooleanProperty( Props, SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, false )
		|| SDL_GetBooleanProperty( Props, SDL_PROP_JOYSTICK_CAP_MONO_LED_BOOLEAN, false );

	const int32 NumButtons = SDL_GetNumJoystickButtons( Device.Joystick );
	const int32 NumAxes    = SDL_GetNumJoystickAxes( Device.Joystick );
	const int32 NumHats    = SDL_GetNumJoystickHats( Device.Joystick );
	Device.Info.NumButtons = NumButtons > 0 ? NumButtons : 0;
	Device.Info.NumAxes    = NumAxes > 0 ? NumAxes : 0;
	Device.Info.NumHats    = NumHats > 0 ? NumHats : 0;

	for ( int32 i = 0; i < static_cast<int32>( EGamepadSensor::Count ); ++i )
	{
		const SDL_SensorType SdlSensor = ToSdlSensor( static_cast<EGamepadSensor>( i ) );
		FGamepadSensorState& Sensor = Device.State.Sensors[i];
		Sensor.bAvailable = ( Device.Gamepad != nullptr ) && SDL_GamepadHasSensor( Device.Gamepad, SdlSensor );
		Sensor.bEnabled   = Sensor.bAvailable && SDL_GamepadSensorEnabled( Device.Gamepad, SdlSensor );
	}
}

void ASdlGamepadInput::ReadState( FDevice& Device, bool bFocused )
{
	FGamepadState& State = Device.State;
	State.bConnected = true;
	State.bFocused   = bFocused;

	// 標準配置のボタン
	for ( int32 i = 0; i < static_cast<int32>( EGamepadButton::Count ); ++i )
	{
		const bool bDown = bFocused && ( Device.Gamepad != nullptr )
			&& SDL_GetGamepadButton( Device.Gamepad, static_cast<SDL_GamepadButton>( i ) );
		const bool bPrev = State.Buttons[i];
		State.PressedButtons[i]  = bDown && !bPrev;
		State.ReleasedButtons[i] = !bDown && bPrev;
		State.Buttons[i]         = bDown;
	}

	// 標準配置の軸
	if ( bFocused && Device.Gamepad != nullptr )
	{
		State.Axes[static_cast<int32>( EGamepadAxis::LeftX )]        = NormalizeSignedAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_LEFTX ) );
		State.Axes[static_cast<int32>( EGamepadAxis::LeftY )]        = NormalizeSignedAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_LEFTY ) );
		State.Axes[static_cast<int32>( EGamepadAxis::RightX )]       = NormalizeSignedAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_RIGHTX ) );
		State.Axes[static_cast<int32>( EGamepadAxis::RightY )]       = NormalizeSignedAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_RIGHTY ) );
		State.Axes[static_cast<int32>( EGamepadAxis::LeftTrigger )]  = NormalizeTriggerAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER ) );
		State.Axes[static_cast<int32>( EGamepadAxis::RightTrigger )] = NormalizeTriggerAxis( SDL_GetGamepadAxis( Device.Gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ) );
	}
	else
	{
		for ( int32 i = 0; i < static_cast<int32>( EGamepadAxis::Count ); ++i )
		{
			State.Axes[i] = 0.0f;
		}
	}

	// 生のボタン・軸・ハット（PS2 USB 変換アダプターなどの未登録機器でもそのまま読める）
	State.RawButtons.SetNum( Device.Info.NumButtons, 0u );
	State.RawPressedButtons.SetNum( Device.Info.NumButtons, 0u );
	State.RawReleasedButtons.SetNum( Device.Info.NumButtons, 0u );
	for ( int32 i = 0; i < Device.Info.NumButtons; ++i )
	{
		const uint8 bDown = ( bFocused && SDL_GetJoystickButton( Device.Joystick, i ) ) ? 1u : 0u;
		const uint8 bPrev = State.RawButtons[i];
		State.RawPressedButtons[i]  = ( bDown != 0u && bPrev == 0u ) ? 1u : 0u;
		State.RawReleasedButtons[i] = ( bDown == 0u && bPrev != 0u ) ? 1u : 0u;
		State.RawButtons[i]         = bDown;
	}

	State.RawAxes.SetNum( Device.Info.NumAxes, 0.0f );
	for ( int32 i = 0; i < Device.Info.NumAxes; ++i )
	{
		State.RawAxes[i] = bFocused
			? NormalizeSignedAxis( SDL_GetJoystickAxis( Device.Joystick, i ) )
			: 0.0f;
	}

	State.RawHats.SetNum( Device.Info.NumHats, 0u );
	for ( int32 i = 0; i < Device.Info.NumHats; ++i )
	{
		State.RawHats[i] = bFocused ? SDL_GetJoystickHat( Device.Joystick, i ) : 0u;
	}

	// センサー（ジャイロ・加速度）
	for ( int32 i = 0; i < static_cast<int32>( EGamepadSensor::Count ); ++i )
	{
		const SDL_SensorType SdlSensor = ToSdlSensor( static_cast<EGamepadSensor>( i ) );
		FGamepadSensorState& Sensor = State.Sensors[i];
		Sensor.bAvailable = ( Device.Gamepad != nullptr ) && SDL_GamepadHasSensor( Device.Gamepad, SdlSensor );
		Sensor.bEnabled   = Sensor.bAvailable && SDL_GamepadSensorEnabled( Device.Gamepad, SdlSensor );

		if ( bFocused && Sensor.bEnabled && SDL_GetGamepadSensorData( Device.Gamepad, SdlSensor, Sensor.Values, 3 ) )
		{
			Sensor.bValid = true;
		}
		else
		{
			Sensor.Values[0] = 0.0f;
			Sensor.Values[1] = 0.0f;
			Sensor.Values[2] = 0.0f;
			Sensor.bValid = false;
		}
	}
}

#pragma endregion

#pragma region Commands

ETickGroup ASdlGamepadInput::GetTickGroup() const
{
	return ETickGroup::PreUpdate;
}

bool ASdlGamepadInput::IsAvailable() const
{
	return bInitialized;
}

const TArray<FGamepadInfo>& ASdlGamepadInput::GetDevices() const
{
	return DeviceInfos;
}

const FGamepadState* ASdlGamepadInput::GetState( uint32 DeviceId ) const
{
	const FDevice* Device = Devices.FindByPredicate( [DeviceId]( const FDevice& Item )
	{
		return Item.Info.Id == DeviceId;
	} );
	return Device != nullptr ? &Device->State : nullptr;
}

const TArray<uint32>& ASdlGamepadInput::GetConnectedThisFrame() const
{
	return Connected;
}

const TArray<uint32>& ASdlGamepadInput::GetDisconnectedThisFrame() const
{
	return Disconnected;
}

bool ASdlGamepadInput::EnableSensor( uint32 DeviceId, EGamepadSensor Sensor, bool bEnabled )
{
	FDevice* Device = FindDevice( DeviceId );
	if ( Device == nullptr || !Device->State.bConnected || Device->Gamepad == nullptr )
	{
		LOG( LogGamepad, Warning, "EnableSensor: gamepad #%u is not connected (or has no gamepad mapping)", DeviceId );
		return false;
	}

	const SDL_SensorType SdlSensor = ToSdlSensor( Sensor );
	if ( SdlSensor == SDL_SENSOR_INVALID || !SDL_GamepadHasSensor( Device->Gamepad, SdlSensor ) )
	{
		LOG( LogGamepad, Warning, "EnableSensor: gamepad #%u (%s) does not have sensor %d", DeviceId, *Device->Info.Name, static_cast<int32>( Sensor ) );
		return false;
	}

	if ( !SDL_SetGamepadSensorEnabled( Device->Gamepad, SdlSensor, bEnabled ) )
	{
		LOG( LogGamepad, Warning, "EnableSensor: SDL failed for gamepad #%u: %s", DeviceId, SDL_GetError() );
		return false;
	}
	LOG( LogGamepad, Verbose, "Sensor %d %s on gamepad #%u (%s)", static_cast<int32>( Sensor ), bEnabled ? "enabled" : "disabled", DeviceId, *Device->Info.Name );

	FGamepadSensorState& SensorState = Device->State.Sensors[static_cast<int32>( Sensor )];
	SensorState.bEnabled = bEnabled;
	if ( !bEnabled )
	{
		SensorState.Values[0] = 0.0f;
		SensorState.Values[1] = 0.0f;
		SensorState.Values[2] = 0.0f;
		SensorState.bValid    = false;
	}
	return true;
}

bool ASdlGamepadInput::Rumble( uint32 DeviceId, float32 Low, float32 High, uint32 Milliseconds )
{
	if ( NMath::IsNaN( Low ) || NMath::IsNaN( High ) )
	{
		LOG( LogGamepad, Warning, "Rumble: intensity is NaN (gamepad #%u)", DeviceId );
		return false;
	}

	// 毎フレーム呼ばれることがあるので、想定内の失敗（フォーカスなし・未接続・非対応）は Verbose にする
	const bool bFocused = bBackgroundInputEnabled || ( FocusInput != nullptr && FocusInput->GetMouseState().bFocused );
	if ( !bFocused && ( Low > 0.0f || High > 0.0f ) )
	{
		LOG( LogGamepad, Verbose, "Rumble ignored: the window is not focused (gamepad #%u)", DeviceId );
		return false;
	}

	FDevice* Device = FindDevice( DeviceId );
	if ( Device == nullptr || !Device->State.bConnected || !Device->Info.bRumble )
	{
		LOG( LogGamepad, Verbose, "Rumble ignored: gamepad #%u is not connected or has no rumble", DeviceId );
		return false;
	}

	const Uint16 Low16  = ToRumbleIntensity( Low );
	const Uint16 High16 = ToRumbleIntensity( High );
	const bool bOk = ( Device->Gamepad != nullptr )
		? SDL_RumbleGamepad( Device->Gamepad, Low16, High16, Milliseconds )
		: SDL_RumbleJoystick( Device->Joystick, Low16, High16, Milliseconds );
	if ( !bOk ) LOG( LogGamepad, Warning, "Rumble: SDL failed for gamepad #%u: %s", DeviceId, SDL_GetError() );
	else LOG( LogGamepad, VeryVerbose, "Rumble #%u low=%.2f high=%.2f for %u ms", DeviceId, Low, High, Milliseconds );
	return bOk;
}

bool ASdlGamepadInput::RumbleTriggers( uint32 DeviceId, float32 Left, float32 Right, uint32 Milliseconds )
{
	if ( NMath::IsNaN( Left ) || NMath::IsNaN( Right ) )
	{
		LOG( LogGamepad, Warning, "RumbleTriggers: intensity is NaN (gamepad #%u)", DeviceId );
		return false;
	}

	const bool bFocused = bBackgroundInputEnabled || ( FocusInput != nullptr && FocusInput->GetMouseState().bFocused );
	if ( !bFocused && ( Left > 0.0f || Right > 0.0f ) )
	{
		LOG( LogGamepad, Verbose, "Trigger rumble ignored: the window is not focused (gamepad #%u)", DeviceId );
		return false;
	}

	FDevice* Device = FindDevice( DeviceId );
	if ( Device == nullptr || !Device->State.bConnected || !Device->Info.bTriggerRumble )
	{
		LOG( LogGamepad, Verbose, "Trigger rumble ignored: gamepad #%u is not connected or has no trigger rumble", DeviceId );
		return false;
	}

	const Uint16 Left16  = ToRumbleIntensity( Left );
	const Uint16 Right16 = ToRumbleIntensity( Right );
	const bool bOk = ( Device->Gamepad != nullptr )
		? SDL_RumbleGamepadTriggers( Device->Gamepad, Left16, Right16, Milliseconds )
		: SDL_RumbleJoystickTriggers( Device->Joystick, Left16, Right16, Milliseconds );
	if ( !bOk ) LOG( LogGamepad, Warning, "RumbleTriggers: SDL failed for gamepad #%u: %s", DeviceId, SDL_GetError() );
	else LOG( LogGamepad, VeryVerbose, "Trigger rumble #%u left=%.2f right=%.2f for %u ms", DeviceId, Left, Right, Milliseconds );
	return bOk;
}

bool ASdlGamepadInput::SetLed( uint32 DeviceId, uint8 Red, uint8 Green, uint8 Blue )
{
	FDevice* Device = FindDevice( DeviceId );
	if ( Device == nullptr || !Device->State.bConnected || !Device->Info.bLed )
	{
		LOG( LogGamepad, Verbose, "SetLed ignored: gamepad #%u is not connected or has no LED", DeviceId );
		return false;
	}

	const bool bOk = ( Device->Gamepad != nullptr )
		? SDL_SetGamepadLED( Device->Gamepad, Red, Green, Blue )
		: SDL_SetJoystickLED( Device->Joystick, Red, Green, Blue );
	if ( !bOk ) LOG( LogGamepad, Warning, "SetLed: SDL failed for gamepad #%u: %s", DeviceId, SDL_GetError() );
	else LOG( LogGamepad, Verbose, "LED of gamepad #%u set to (%u, %u, %u)", DeviceId, Red, Green, Blue );
	return bOk;
}

bool ASdlGamepadInput::AddMappingsFromFile( const FString& Filename )
{
	if ( !bInitialized || Filename.IsEmpty() )
	{
		LOG( LogGamepad, Warning, "AddMappingsFromFile: %s", Filename.IsEmpty() ? "no file name was given" : "gamepad input is not initialized" );
		return false;
	}

	const FString Resolved = NFileSystem::FindFile( Filename ).Get( Filename );
	const int32 Added = SDL_AddGamepadMappingsFromFile( *Resolved );
	if ( Added < 0 )
	{
		LOG( LogGamepad, Warning, "Failed to load gamepad mappings from %s: %s", *Resolved, SDL_GetError() );
		return false;
	}

	LOG( LogGamepad, Log, "Loaded %d gamepad mapping(s) from %s", Added, *Resolved );
	return true;
}

void ASdlGamepadInput::SetBackgroundInputEnabled( bool bEnabled )
{
	if ( bBackgroundInputEnabled == bEnabled ) return;
	bBackgroundInputEnabled = bEnabled;
	LOG( LogGamepad, Verbose, "Background input %s", bEnabled ? "enabled" : "disabled" );
}

#pragma endregion
