// SPDX-License-Identifier: MIT
#include "UIMarkupValues.h"

#include "Library/Core/Types/Numeric.h"

namespace NUIMarkupValue
{
	// ---- 値の読み取り -----------------------------------------------------
	FString Trim( const FString& Text )
	{
		return Text.TrimStartAndEnd();
	}

	bool ParseFloat( const FString& Text, float32& Out )
	{
		// 前後の空白を除いて、全体が数のときだけ（FString::TryParseFloat）
		const TOptional<float64> Value = Text.TryParseFloat();
		if ( !Value.IsSet() )
		{
			return false;
		}
		Out = static_cast<float32>( Value.GetValue() );
		return true;
	}

	bool ParseInt( const FString& Text, int32& Out )
	{
		float32 Value = 0.0f;
		if ( !ParseFloat( Text, Value ) || Value != static_cast<float32>( static_cast<int32>( Value ) ) )
		{
			return false;
		}
		Out = static_cast<int32>( Value );
		return true;
	}

	bool ParseBool( const FString& Text, bool& Out )
	{
		const FString Trimmed = Trim( Text ).ToLower();
		if ( Trimmed == "true" || Trimmed == "1" )
		{
			Out = true;
			return true;
		}
		if ( Trimmed == "false" || Trimmed == "0" )
		{
			Out = false;
			return true;
		}
		return false;
	}

	// "a, b, c" を、コンマで分けて、前後の空白を除く
	TArray<FString> SplitList( const FString& Text )
	{
		TArray<FString> Parts;
		FString Current;
		for ( int32 Index = 0; Index < Text.Len(); ++Index )
		{
			const char Ch = ( *Text )[Index];
			if ( Ch == ',' )
			{
				Parts.Add( Trim( Current ) );
				Current.Empty();
			}
			else
			{
				Current += Ch;
			}
		}
		Parts.Add( Trim( Current ) );
		return Parts;
	}

	bool ParseFloatList( const FString& Text, TArray<float32>& Out )
	{
		Out.Empty();
		const TArray<FString> Parts = SplitList( Text );
		for ( int32 Index = 0; Index < Parts.Num(); ++Index )
		{
			float32 Value = 0.0f;
			if ( !ParseFloat( Parts[Index], Value ) )
			{
				return false;
			}
			Out.Add( Value );
		}
		return true;
	}

	// 余白: 1 個（四辺）・2 個（左右・上下）・4 個（左・上・右・下）
	bool ParseMargin( const FString& Text, FUIMargin& Out )
	{
		TArray<float32> Values;
		if ( !ParseFloatList( Text, Values ) )
		{
			return false;
		}
		if ( Values.Num() == 1 )
		{
			Out = FUIMargin( Values[0] );
			return true;
		}
		if ( Values.Num() == 2 )
		{
			Out = FUIMargin( Values[0], Values[1] );
			return true;
		}
		if ( Values.Num() == 4 )
		{
			Out = FUIMargin( Values[0], Values[1], Values[2], Values[3] );
			return true;
		}
		return false;
	}

	bool ParseVector2( const FString& Text, FVector2& Out )
	{
		TArray<float32> Values;
		if ( !ParseFloatList( Text, Values ) || Values.Num() != 2 )
		{
			return false;
		}
		Out = FVector2( Values[0], Values[1] );
		return true;
	}

	// #RGB・#RRGGBB・#RRGGBBAA
	bool ParseColor( const FString& Text, FColor32& Out )
	{
		return FColor32::TryParseHex( Text, Out );
	}

	// "{Binding 名前}" なら、名前を返す
	bool ParseBinding( const FString& Text, FString& OutName )
	{
		const FString Trimmed = Trim( Text );
		if ( Trimmed.Len() < 3 || ( *Trimmed )[0] != '{' || ( *Trimmed )[Trimmed.Len() - 1] != '}' )
		{
			return false;
		}
		const FString Inner = Trim( Trimmed.Mid( 1, Trimmed.Len() - 2 ) );
		if ( !Inner.StartsWith( "Binding" ) )
		{
			return false;
		}
		OutName = Trim( Inner.Mid( 7 ) );
		return !OutName.IsEmpty();
	}

	// "{Material パス}" なら、パスを返す（例: {Material Engine:/Materials/UI/GlowButton.gxmaterial}）
	bool ParseMaterialReference( const FString& Text, FString& OutPath )
	{
		const FString Trimmed = Trim( Text );
		if ( Trimmed.Len() < 3 || ( *Trimmed )[0] != '{' || ( *Trimmed )[Trimmed.Len() - 1] != '}' )
		{
			return false;
		}
		const FString Inner = Trim( Trimmed.Mid( 1, Trimmed.Len() - 2 ) );
		if ( !Inner.StartsWith( "Material " ) )
		{
			return false;
		}
		OutPath = Trim( Inner.Mid( 9 ) );
		return !OutPath.IsEmpty();
	}

	bool ParseAlignment( const FString& Text, EUIAlignment& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "start" || Value == "left" || Value == "top" )
		{
			Out = EUIAlignment::Start;
		}
		else if ( Value == "center" )
		{
			Out = EUIAlignment::Center;
		}
		else if ( Value == "end" || Value == "right" || Value == "bottom" )
		{
			Out = EUIAlignment::End;
		}
		else if ( Value == "stretch" )
		{
			Out = EUIAlignment::Stretch;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseOrientation( const FString& Text, EUIOrientation& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "horizontal" )
		{
			Out = EUIOrientation::Horizontal;
		}
		else if ( Value == "vertical" )
		{
			Out = EUIOrientation::Vertical;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseVisibility( const FString& Text, EUIVisibility& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "visible" )
		{
			Out = EUIVisibility::Visible;
		}
		else if ( Value == "hidden" )
		{
			Out = EUIVisibility::Hidden;
		}
		else if ( Value == "collapsed" )
		{
			Out = EUIVisibility::Collapsed;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool ParseImageMode( const FString& Text, EUIImageMode& Out )
	{
		const FString Value = Trim( Text ).ToLower();
		if ( Value == "stretch" )
		{
			Out = EUIImageMode::Stretch;
		}
		else if ( Value == "fit" )
		{
			Out = EUIImageMode::Fit;
		}
		else if ( Value == "fill" )
		{
			Out = EUIImageMode::Fill;
		}
		else if ( Value == "tile" )
		{
			Out = EUIImageMode::Tile;
		}
		else if ( Value == "nineslice" )
		{
			Out = EUIImageMode::NineSlice;
		}
		else
		{
			return false;
		}
		return true;
	}

	// アンカーの名前（9 か所と、全体）
	bool ParseAnchor( const FString& Text, FVector2& OutMin, FVector2& OutMax, FVector2& OutPivot )
	{
		const FString Value = Trim( Text ).ToLower();
		struct FPreset
		{
			const char* Name;
			float32 X;
			float32 Y;
		};
		static const FPreset Presets[] = { { "topleft", 0.0f, 0.0f }, { "top", 0.5f, 0.0f }, { "topright", 1.0f, 0.0f }, { "left", 0.0f, 0.5f }, { "center", 0.5f, 0.5f },
										   { "right", 1.0f, 0.5f }, { "bottomleft", 0.0f, 1.0f }, { "bottom", 0.5f, 1.0f }, { "bottomright", 1.0f, 1.0f } };
		for ( const FPreset& Preset : Presets )
		{
			if ( Value == Preset.Name )
			{
				OutMin = OutMax = OutPivot = FVector2( Preset.X, Preset.Y );
				return true;
			}
		}
		if ( Value == "stretch" )
		{
			OutMin = FVector2( 0.0f, 0.0f );
			OutMax = FVector2( 1.0f, 1.0f );
			OutPivot = FVector2( 0.0f, 0.0f );
			return true;
		}
		return false;
	}

	// 列・行の指定: "100, Auto, *, 2*"（数 = 画素・Auto = 中身・* = 残りを 1・n* = 残りを n）
	bool ParseTracks( const FString& Text, TArray<FUITrack>& Out )
	{
		Out.Empty();
		const TArray<FString> Parts = SplitList( Text );
		for ( int32 Index = 0; Index < Parts.Num(); ++Index )
		{
			const FString Part = Parts[Index].ToLower();
			if ( Part == "auto" )
			{
				Out.Add( FUITrack::MakeAuto() );
			}
			else if ( Part == "*" )
			{
				Out.Add( FUITrack::MakeWeight( 1.0f ) );
			}
			else if ( Part.Len() >= 2 && ( *Part )[Part.Len() - 1] == '*' )
			{
				float32 Weight = 0.0f;
				if ( !ParseFloat( Part.Left( Part.Len() - 1 ), Weight ) || Weight <= 0.0f )
				{
					return false;
				}
				Out.Add( FUITrack::MakeWeight( Weight ) );
			}
			else
			{
				float32 Pixels = 0.0f;
				if ( !ParseFloat( Part, Pixels ) || Pixels < 0.0f )
				{
					return false;
				}
				Out.Add( FUITrack::MakeFixed( Pixels ) );
			}
		}
		return true;
	}
}
