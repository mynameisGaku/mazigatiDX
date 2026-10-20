#include "ScreenConfig.h"

#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogScreenConfig, Log )

FScreenConfig::FScreenConfig( const FString& Filename )
{
	// 失敗時は LoadFromFile がログを出す。全項目 0 / 空のままなので ToWindowSettings は Base を使う
	if ( !LoadFromFile( Filename ) )
	{
		LOG( LogScreenConfig, Warning, "Could not load %s. The default window settings will be used.", *Filename );
		return;
	}

	Width = GetValue<int32>( "Viewport.Size.Width" );
	Height = GetValue<int32>( "Viewport.Size.Height" );
	OffsetX = GetValue<int32>( "Viewport.Offset.X" );
	OffsetY = GetValue<int32>( "Viewport.Offset.Y" );
	Title = GetValue<FString>( "Viewport.Title" );
	const FString WindowModeText = GetValue<FString>( "Viewport.WindowMode" );
	WindowMode = WindowModeFromString( WindowModeText );
	ExtendRate = GetValue<float64>( "Viewport.ExtendRate" );

	if ( !WindowModeText.IsEmpty() && !WindowMode.IsSet() )
	{
		LOG( LogScreenConfig, Warning, "Unknown Viewport.WindowMode '%s' (expected Windowed / Borderless / Fullscreen). Using the default.", *WindowModeText );
	}
	if ( Width <= 0 || Height <= 0 )
	{
		LOG( LogScreenConfig, Warning, "Viewport.Size is missing or invalid (%d x %d). Using the default size.", Width, Height );
	}

	LOG( LogScreenConfig, Log, "Loaded %s: %d x %d, offset (%d, %d), mode %s, scale %.2f, title '%s'", *Filename, Width, Height, OffsetX, OffsetY,
		 WindowMode.IsSet() ? WindowModeToString( WindowMode.GetValue() ) : "(default)", ExtendRate, *Title );
}

FWindowSettings FScreenConfig::ToWindowSettings( const FWindowSettings& Base ) const
{
	FWindowSettings Settings = Base;
	if ( Width > 0 ) Settings.Width = Width;
	if ( Height > 0 ) Settings.Height = Height;
	if ( !Title.IsEmpty() ) Settings.Title = Title;
	if ( ExtendRate > 0.0 ) Settings.WindowExtendRate = ExtendRate;
	if ( WindowMode.IsSet() ) Settings.WindowMode = WindowMode.GetValue();
	if ( OffsetX != 0 || OffsetY != 0 )  // 未指定 (0, 0) なら中央のまま
	{
		Settings.PositionX = OffsetX;
		Settings.PositionY = OffsetY;
	}
	return Settings;
}
