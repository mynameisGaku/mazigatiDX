// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Playback/PixelStockActor.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Canvas/PixelMask.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 12 x 19 の旅人（k: 輪郭、h: 髪、s: 肌、S: 肌の影、e: 目、t: 服、T: 服の影、b: 帯、p: ズボン、o: 靴）
	const char* const TravelerRows[] = {
		"....kkkkk...",
		"..kkhhhhhk..",
		".khhhhhhhhk.",
		".khhhhhhhhhk",
		".khhssssshhk",
		".khsessesshk",
		".khsessesshk",
		"..ksssssssk.",
		"...kkSSSkk..",
		"..kttttttk..",
		".kttTttTttk.",
		".ksttTTttsk.",
		".kstttttttk.",
		"..kbbbbbbk..",
		"..kppkkppk..",
		"..kppk.kppk.",
		"..kppk.kppk.",
		"..koook.kook",
		"..kkkkk.kkkk",
	};

	FColor32 ToColor( char Key )
	{
		switch ( Key )
		{
		case 'k':
		case 'e':
			return NPixelColor::FromHex( "#17121f" );
		case 'h':
			return NPixelColor::FromHex( "#7a4128" );
		case 's':
			return NPixelColor::FromHex( "#f3c49d" );
		case 'S':
			return NPixelColor::FromHex( "#c98b6c" );
		case 't':
			return NPixelColor::FromHex( "#5f6f94" );
		case 'T':
			return NPixelColor::FromHex( "#46536f" );
		case 'b':
			return NPixelColor::FromHex( "#8a5a32" );
		case 'p':
			return NPixelColor::FromHex( "#38334a" );
		case 'o':
			return NPixelColor::FromHex( "#6a4030" );
		default:
			return FColor32( 0, 0, 0, 0 );
		}
	}
}

FPixelActor NPixelStockActor::Make( int32 Scale )
{
	const int32 Factor = NMath::Clamp( Scale, 1, 2 );
	FPixelActor Actor;
	Actor.Mask = NPixelMask::FromRows( TravelerRows, Factor );
	Actor.Width = Actor.Mask.Width;
	Actor.Height = Actor.Mask.Height;
	Actor.Pixels.SetNum( Actor.Width * Actor.Height, FColor32( 0, 0, 0, 0 ) );
	const int32 Columns = Actor.Width / Factor;
	for ( int32 Y = 0; Y < Actor.Height; ++Y )
	{
		for ( int32 X = 0; X < Actor.Width; ++X )
		{
			const char Key = TravelerRows[Y / Factor][X / Factor];
			if ( X / Factor < Columns )
			{
				Actor.Pixels[Y * Actor.Width + X] = ToColor( Key );
			}
		}
	}
	Actor.Head = FVector2( 0.0f, -static_cast<float32>( Actor.Height ) );
	Actor.Chest = FVector2( 0.0f, -static_cast<float32>( Actor.Height ) * 0.42f );
	return Actor;
}
