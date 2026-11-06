// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"

#include "Library/Core/Types/CString.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	int32 ParseHexDigit( char Ch )
	{
		if ( Ch >= '0' && Ch <= '9' )
		{
			return static_cast<int32>( Ch - '0' );
		}
		if ( Ch >= 'a' && Ch <= 'f' )
		{
			return 10 + static_cast<int32>( Ch - 'a' );
		}
		if ( Ch >= 'A' && Ch <= 'F' )
		{
			return 10 + static_cast<int32>( Ch - 'A' );
		}
		return 0;
	}

	uint8 ClampChannel( float Value )
	{
		const int32 Rounded = static_cast<int32>( Value );
		if ( Rounded <= 0 )
		{
			return 0;
		}
		if ( Rounded >= 255 )
		{
			return 255;
		}
		return static_cast<uint8>( Rounded );
	}
}

namespace NPixelColor
{
	FColor32 FromHex( const char* HexText )
	{
		if ( HexText == nullptr )
		{
			return FColor32( 0, 0, 0, 255 );
		}
		const char* Cursor = HexText;
		if ( *Cursor == '#' )
		{
			++Cursor;
		}
		const int32 Len = static_cast<int32>( NCString::Length( Cursor ) );
		if ( Len == 3 )
		{
			const int32 R = ParseHexDigit( Cursor[0] ) * 17;
			const int32 G = ParseHexDigit( Cursor[1] ) * 17;
			const int32 B = ParseHexDigit( Cursor[2] ) * 17;
			return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), 255 );
		}
		if ( Len >= 6 )
		{
			const int32 R = ParseHexDigit( Cursor[0] ) * 16 + ParseHexDigit( Cursor[1] );
			const int32 G = ParseHexDigit( Cursor[2] ) * 16 + ParseHexDigit( Cursor[3] );
			const int32 B = ParseHexDigit( Cursor[4] ) * 16 + ParseHexDigit( Cursor[5] );
			return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), 255 );
		}
		return FColor32( 0, 0, 0, 255 );
	}

	FColor32 FromHex( const FString& HexText )
	{
		return FromHex( HexText.GetData() );
	}

	FString ToHex( const FColor32& Color )
	{
		char Buffer[8] = {};
		NCString::Format( Buffer, sizeof( Buffer ), "#%02x%02x%02x", static_cast<int32>( Color.r ), static_cast<int32>( Color.g ), static_cast<int32>( Color.b ) );
		return FString( Buffer );
	}

	FColor32 Mix( const FColor32& ColorA, const FColor32& ColorB, float Alpha )
	{
		const float Clamped = NMath::Clamp( Alpha, 0.0f, 1.0f );
		const float R = static_cast<float>( ColorA.r ) + ( static_cast<float>( ColorB.r ) - static_cast<float>( ColorA.r ) ) * Clamped;
		const float G = static_cast<float>( ColorA.g ) + ( static_cast<float>( ColorB.g ) - static_cast<float>( ColorA.g ) ) * Clamped;
		const float B = static_cast<float>( ColorA.b ) + ( static_cast<float>( ColorB.b ) - static_cast<float>( ColorA.b ) ) * Clamped;
		return FColor32( ClampChannel( R ), ClampChannel( G ), ClampChannel( B ), 255 );
	}

	FColor32 ScaleBrightness( const FColor32& Color, float Factor )
	{
		const float SafeFactor = NMath::Max( 0.0f, Factor );
		const float R = NMath::Min( 255.0f, static_cast<float>( Color.r ) * SafeFactor );
		const float G = NMath::Min( 255.0f, static_cast<float>( Color.g ) * SafeFactor );
		const float B = NMath::Min( 255.0f, static_cast<float>( Color.b ) * SafeFactor );
		return FColor32( ClampChannel( R ), ClampChannel( G ), ClampChannel( B ), 255 );
	}

	float GetLuminance( const FColor32& Color )
	{
		return ( 0.299f * static_cast<float>( Color.r ) + 0.587f * static_cast<float>( Color.g ) + 0.114f * static_cast<float>( Color.b ) ) / 255.0f;
	}

	TArray<FColor32> MakeRamp( const char* const* HexStrings, int32 Count )
	{
		TArray<FColor32> Result;
		if ( HexStrings == nullptr || Count <= 0 )
		{
			return Result;
		}
		Result.Reserve( Count );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Result.Add( FromHex( HexStrings[Index] ) );
		}
		return Result;
	}

	TArray<FColor32> MakeRamp( const TArray<FString>& HexStrings )
	{
		TArray<FColor32> Result;
		Result.Reserve( HexStrings.Num() );
		for ( int32 Index = 0; Index < HexStrings.Num(); ++Index )
		{
			Result.Add( FromHex( HexStrings[Index] ) );
		}
		return Result;
	}

	TArray<FColor32> FitRamp( const TArray<FColor32>& Ramp, int32 StepCount )
	{
		TArray<FColor32> Result;
		if ( StepCount <= 0 || Ramp.IsEmpty() )
		{
			return Result;
		}
		if ( Ramp.Num() == StepCount )
		{
			return Ramp;
		}
		Result.Reserve( StepCount );
		const int32 LastSource = Ramp.Num() - 1;
		const int32 Denom = NMath::Max( 1, StepCount - 1 );
		for ( int32 Index = 0; Index < StepCount; ++Index )
		{
			const float Ratio = static_cast<float>( Index * LastSource ) / static_cast<float>( Denom );
			const int32 SourceIndex = NMath::Clamp( NMath::RoundToInt( Ratio ), 0, LastSource );
			Result.Add( Ramp[SourceIndex] );
		}
		return Result;
	}

	FColor32 SampleStep( const TArray<FColor32>& Ramp, float Normalized )
	{
		if ( Ramp.IsEmpty() )
		{
			return FColor32( 0, 0, 0, 255 );
		}
		const float Scaled = NMath::Floor( Normalized * static_cast<float>( Ramp.Num() ) );
		const int32 Index = NMath::Clamp( static_cast<int32>( Scaled ), 0, Ramp.Num() - 1 );
		return Ramp[Index];
	}

	FColor32 SampleBlend( const TArray<FColor32>& Ramp, float Normalized )
	{
		if ( Ramp.IsEmpty() )
		{
			return FColor32( 0, 0, 0, 255 );
		}
		if ( Ramp.Num() == 1 )
		{
			return Ramp[0];
		}
		const float Clamped = NMath::Clamp( Normalized, 0.0f, 1.0f );
		const float Position = Clamped * static_cast<float>( Ramp.Num() - 1 );
		const int32 Index = NMath::Clamp( NPixelMath::FloorToInt( Position ), 0, Ramp.Num() - 2 );
		const float Fraction = Position - static_cast<float>( Index );
		return Mix( Ramp[Index], Ramp[Index + 1], Fraction );
	}

	TArray<FColor32> GetPalette( EPixelPalette Palette )
	{
		static const char* const FireHex[] = { "#1f0b10", "#4a1118", "#8c1d1c", "#cc3a1a", "#f2701f", "#ffae35", "#ffe27a", "#fff8d6" };
		static const char* const EmberHex[] = { "#1a0a0e", "#3d1012", "#701a14", "#a8301a", "#e0561e", "#ff8a2a", "#ffc46a" };
		static const char* const LavaHex[] = { "#14060a", "#380a10", "#6e1210", "#ac2410", "#e8501a", "#ff8c2a", "#ffd25a" };
		static const char* const SmokeHex[] = { "#0e0c12", "#1d1a22", "#2e2a35", "#433e4b", "#5c5664", "#7b7484", "#a39cab" };
		static const char* const DustHex[] = { "#1c1610", "#33281c", "#4f3f2c", "#6e5a40", "#927a58", "#b89f78", "#dcc8a2" };
		static const char* const IceHex[] = { "#0c1430", "#162c5c", "#1f4f94", "#2f7fcc", "#58b4ec", "#9bdcff", "#d8f4ff", "#ffffff" };
		static const char* const WaterHex[] = { "#081830", "#0e2e5a", "#14508c", "#1c76bc", "#36a2e0", "#78ccf4", "#c4ecff", "#ffffff" };
		static const char* const ElectricHex[] = { "#0a0f2e", "#172a78", "#2550d0", "#3f8cff", "#7cc8ff", "#c8f0ff", "#ffffff" };
		static const char* const VoltHex[] = { "#24160a", "#5e3a0c", "#a8680f", "#e8a218", "#ffd23c", "#fff08a", "#ffffff" };
		static const char* const PoisonHex[] = { "#0d1a0c", "#173a14", "#265e18", "#3f8a1e", "#6cb82a", "#a8e04a", "#e0ff8a" };
		static const char* const HolyHex[] = { "#2a1c08", "#5a3e10", "#9a6c18", "#d8a428", "#ffd84a", "#fff09a", "#fffbe6", "#ffffff" };
		static const char* const HealHex[] = { "#08201a", "#0f3f2c", "#17663e", "#24924e", "#3ec466", "#7ee88a", "#c8ffc4", "#ffffff" };
		static const char* const ArcaneHex[] = { "#140a26", "#2b1252", "#481c88", "#6b2cc0", "#9a48ec", "#c684ff", "#ecc8ff", "#ffffff" };
		static const char* const ShadowHex[] = { "#050308", "#0f0a18", "#1c1230", "#2c1a4a", "#432468", "#5e3290", "#8048b8", "#a970e0" };
		static const char* const BloodHex[] = { "#14040a", "#3a0a14", "#6a101c", "#a01824", "#d42a30", "#ff5a50", "#ff9a8a" };
		static const char* const EarthHex[] = { "#140e0a", "#2a1e14", "#47321e", "#69492a", "#8e6638", "#b88a52", "#dcb47c" };
		static const char* const StoneHex[] = { "#0e0e12", "#1c1c24", "#2c2c38", "#42424f", "#5c5c6b", "#7d7d8c", "#a4a4b2", "#d0d0da" };
		static const char* const GoldHex[] = { "#1e1206", "#4a2c08", "#86520c", "#c48412", "#f0b424", "#ffe066", "#fff6c4", "#ffffff" };
		static const char* const LeafHex[] = { "#0c1a0e", "#163418", "#225224", "#357530", "#4f9a3a", "#7cc048", "#b4e070", "#e4ffb0" };
		static const char* const WoodHex[] = { "#160c06", "#2e1a0c", "#4a2c14", "#6a421e", "#8e5c2c", "#b47c40", "#d8a464" };
		static const char* const MetalHex[] = { "#0c0e14", "#1c2230", "#2e3648", "#4a5468", "#6e7a90", "#9aa6ba", "#d0d8e6", "#ffffff" };
		static const char* const BoneHex[] = { "#1a1814", "#3a352c", "#5e5648", "#8a806c", "#b8ad96", "#e4dcc8", "#fffaf0" };
		static const char* const NeonHex[] = { "#1a0516", "#3d0a33", "#701560", "#b0228f", "#f53cd1", "#ff86e6", "#ffd2f6", "#ffffff" };
		static const char* const CyberHex[] = { "#041418", "#08303a", "#0c5260", "#108088", "#18b4b0", "#4ce8d8", "#b4fff0", "#ffffff" };
		static const char* const MatrixHex[] = { "#020a04", "#05200c", "#0a3c16", "#106624", "#1a9a34", "#3cd650", "#a4ffa8", "#e8ffe8" };
		static const char* const CandyHex[] = { "#ff4a5a", "#ff9a2a", "#ffe03c", "#5ad84a", "#3cc8ff", "#7a6cff", "#f53cd1" };
		static const char* const NightHex[] = { "#07060d", "#0c0a18", "#131026", "#1b1734", "#241f44", "#2f2956" };

		#define MAKE_PALETTE_ARRAY( Arr ) MakeRamp( Arr, static_cast<int32>( sizeof( Arr ) / sizeof( Arr[0] ) ) )
		switch ( Palette )
		{
		case EPixelPalette::Fire:
			return MAKE_PALETTE_ARRAY( FireHex );
		case EPixelPalette::Ember:
			return MAKE_PALETTE_ARRAY( EmberHex );
		case EPixelPalette::Lava:
			return MAKE_PALETTE_ARRAY( LavaHex );
		case EPixelPalette::Smoke:
			return MAKE_PALETTE_ARRAY( SmokeHex );
		case EPixelPalette::Dust:
			return MAKE_PALETTE_ARRAY( DustHex );
		case EPixelPalette::Ice:
			return MAKE_PALETTE_ARRAY( IceHex );
		case EPixelPalette::Water:
			return MAKE_PALETTE_ARRAY( WaterHex );
		case EPixelPalette::Electric:
			return MAKE_PALETTE_ARRAY( ElectricHex );
		case EPixelPalette::Volt:
			return MAKE_PALETTE_ARRAY( VoltHex );
		case EPixelPalette::Poison:
			return MAKE_PALETTE_ARRAY( PoisonHex );
		case EPixelPalette::Holy:
			return MAKE_PALETTE_ARRAY( HolyHex );
		case EPixelPalette::Heal:
			return MAKE_PALETTE_ARRAY( HealHex );
		case EPixelPalette::Arcane:
			return MAKE_PALETTE_ARRAY( ArcaneHex );
		case EPixelPalette::Shadow:
			return MAKE_PALETTE_ARRAY( ShadowHex );
		case EPixelPalette::Blood:
			return MAKE_PALETTE_ARRAY( BloodHex );
		case EPixelPalette::Earth:
			return MAKE_PALETTE_ARRAY( EarthHex );
		case EPixelPalette::Stone:
			return MAKE_PALETTE_ARRAY( StoneHex );
		case EPixelPalette::Gold:
			return MAKE_PALETTE_ARRAY( GoldHex );
		case EPixelPalette::Leaf:
			return MAKE_PALETTE_ARRAY( LeafHex );
		case EPixelPalette::Wood:
			return MAKE_PALETTE_ARRAY( WoodHex );
		case EPixelPalette::Metal:
			return MAKE_PALETTE_ARRAY( MetalHex );
		case EPixelPalette::Bone:
			return MAKE_PALETTE_ARRAY( BoneHex );
		case EPixelPalette::Neon:
			return MAKE_PALETTE_ARRAY( NeonHex );
		case EPixelPalette::Cyber:
			return MAKE_PALETTE_ARRAY( CyberHex );
		case EPixelPalette::Matrix:
			return MAKE_PALETTE_ARRAY( MatrixHex );
		case EPixelPalette::Candy:
			return MAKE_PALETTE_ARRAY( CandyHex );
		case EPixelPalette::Night:
			return MAKE_PALETTE_ARRAY( NightHex );
		default:
			return MAKE_PALETTE_ARRAY( FireHex );
		}
		#undef MAKE_PALETTE_ARRAY
	}

	TArray<FColor32> GetPaletteByName( const FString& Name )
	{
		struct FPaletteNameEntry
		{
			const char* Name;
			EPixelPalette Palette;
		};
		static const FPaletteNameEntry Entries[] =
		{
			{ "fire", EPixelPalette::Fire },
			{ "ember", EPixelPalette::Ember },
			{ "lava", EPixelPalette::Lava },
			{ "smoke", EPixelPalette::Smoke },
			{ "dust", EPixelPalette::Dust },
			{ "ice", EPixelPalette::Ice },
			{ "water", EPixelPalette::Water },
			{ "electric", EPixelPalette::Electric },
			{ "volt", EPixelPalette::Volt },
			{ "poison", EPixelPalette::Poison },
			{ "holy", EPixelPalette::Holy },
			{ "heal", EPixelPalette::Heal },
			{ "arcane", EPixelPalette::Arcane },
			{ "shadow", EPixelPalette::Shadow },
			{ "blood", EPixelPalette::Blood },
			{ "earth", EPixelPalette::Earth },
			{ "stone", EPixelPalette::Stone },
			{ "gold", EPixelPalette::Gold },
			{ "leaf", EPixelPalette::Leaf },
			{ "wood", EPixelPalette::Wood },
			{ "metal", EPixelPalette::Metal },
			{ "bone", EPixelPalette::Bone },
			{ "neon", EPixelPalette::Neon },
			{ "cyber", EPixelPalette::Cyber },
			{ "matrix", EPixelPalette::Matrix },
			{ "candy", EPixelPalette::Candy },
			{ "night", EPixelPalette::Night },
		};
		for ( int32 Index = 0; Index < static_cast<int32>( sizeof( Entries ) / sizeof( Entries[0] ) ); ++Index )
		{
			if ( NCString::CompareIgnoreCase( Name.GetData(), Entries[Index].Name ) == 0 )
			{
				return GetPalette( Entries[Index].Palette );
			}
		}
		return GetPalette( EPixelPalette::Fire );
	}
}
