// SPDX-License-Identifier: MIT
#include "UIThemeLoader.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"
#include "Library/Framework/UI/Markup/UIMarkupDocument.h"
#include "Library/Json/JsonObject.h"

namespace
{
	FString ToText( const NlohmannJson& Value )
	{
		return NJson::FromJsonString( Value.get_ref<const NJson::FJsonString&>() );
	}

	// オブジェクトの項目が、Allowed のどれかだけか。違う項目があれば、その名前を返す
	bool HasOnlyKeys( const NlohmannJson& Object, const char* const* Allowed, int32 NumAllowed, FString& OutUnknown )
	{
		for ( auto Iterator = Object.begin(); Iterator != Object.end(); ++Iterator )
		{
			const FString Key( Iterator.key().c_str() );
			bool bKnown = false;
			for ( int32 Index = 0; Index < NumAllowed; ++Index )
			{
				bKnown = bKnown || Key == Allowed[Index];
			}
			if ( !bKnown )
			{
				OutUnknown = Key;
				return false;
			}
		}
		return true;
	}

	struct FThemeBuilder
	{
		CAssetSubsystem* Assets = nullptr;
		FUITheme& Theme;
		const NlohmannJson* BrushTable = nullptr;   // ファイルの "Brushes"（無ければ null）
		TArray<FString> BuiltBrushes;               // このファイルから、もう作ったブラシの名前
		TArray<FString> ResolvingBrushes;           // いま作っている途中のブラシの名前（輪の検出）
		FString Error;

		FThemeBuilder( CAssetSubsystem* InAssets, FUITheme& InTheme ) : Assets( InAssets ), Theme( InTheme ) {}

		bool Fail( const FString& Message )
		{
			if ( Error.IsEmpty() )
			{
				Error = Message;
			}
			return false;
		}

		bool ReadNumber( const NlohmannJson& Value, const FString& Where, float32& Out )
		{
			if ( !Value.is_number() )
			{
				return Fail( FString::Printf( "%s must be a number", *Where ) );
			}
			Out = Value.get<float>();
			return true;
		}

		// "#RRGGBB" など。bAllowToken なら、"@名前"（テーマの色）も
		bool ReadColor( const NlohmannJson& Value, const FString& Where, bool bAllowToken, FColor32& Out )
		{
			if ( !Value.is_string() )
			{
				return Fail( FString::Printf( "%s must be a color like \"#RRGGBB\"", *Where ) );
			}
			const FString Text = ToText( Value ).TrimStartAndEnd();
			if ( bAllowToken && Text.Len() >= 2 && ( *Text )[0] == '@' )
			{
				if ( Theme.FindColor( Text.Mid( 1 ), Out ) )
				{
					return true;
				}
				return Fail( FString::Printf( "%s: the color '%s' is not defined", *Where, *Text ) );
			}
			if ( FColor32::TryParseHex( Text, Out ) )
			{
				return true;
			}
			return Fail( FString::Printf( "%s: \"%s\" is not a color (#RGB, #RRGGBB or #RRGGBBAA expected)", *Where, *Text ) );
		}

		// 数（四辺）か、[左, 上, 右, 下]
		bool ReadMargin( const NlohmannJson& Value, const FString& Where, FUIMargin& Out )
		{
			if ( Value.is_number() )
			{
				Out = FUIMargin( Value.get<float>() );
				return true;
			}
			if ( Value.is_array() && Value.size() == 4 && Value[0].is_number() && Value[1].is_number() && Value[2].is_number() && Value[3].is_number() )
			{
				Out = FUIMargin( Value[0].get<float>(), Value[1].get<float>(), Value[2].get<float>(), Value[3].get<float>() );
				return true;
			}
			return Fail( FString::Printf( "%s must be a number or [left, top, right, bottom]", *Where ) );
		}

		// 名前のブラシ: このファイルの "Brushes"（まだ作っていなければ、先に作る）→ テーマ（親も）
		bool ResolveNamedBrush( const FString& Name, const FString& Where, FUIBrush& Out )
		{
			const NJson::FJsonString Key = NJson::ToJsonString( Name );
			if ( BrushTable != nullptr && BrushTable->contains( Key ) && !BuiltBrushes.Contains( Name ) )
			{
				if ( ResolvingBrushes.Contains( Name ) )
				{
					return Fail( FString::Printf( "%s: the brush '%s' refers to itself", *Where, *Name ) );
				}
				ResolvingBrushes.Add( Name );
				FUIBrush Built;
				const bool bOk = ReadBrush( ( *BrushTable )[Key], FString::Printf( "Brushes.%s", *Name ), Built );
				ResolvingBrushes.Remove( Name );
				if ( !bOk )
				{
					return false;
				}
				Theme.SetBrush( Name, Built );
				BuiltBrushes.Add( Name );
			}
			if ( Theme.FindBrush( Name, Out ) )
			{
				return true;
			}
			return Fail( FString::Printf( "%s: the brush '%s' is not defined", *Where, *Name ) );
		}

		bool ReadImageBrush( const NlohmannJson& Value, const FString& Where, FUIBrush& Out )
		{
			static const char* const Allowed[] = { "Image", "Mode", "Margin", "Tint" };
			FString Unknown;
			if ( !HasOnlyKeys( Value, Allowed, 4, Unknown ) )
			{
				return Fail( FString::Printf( "%s: '%s' is not known for an image brush (Image, Mode, Margin, Tint)", *Where, *Unknown ) );
			}
			if ( !Value["Image"].is_string() )
			{
				return Fail( FString::Printf( "%s.Image must be a path", *Where ) );
			}
			const FString Path = ToText( Value["Image"] );
			EUIImageMode Mode = EUIImageMode::Stretch;
			if ( Value.contains( "Mode" ) )
			{
				static const char* const ModeNames[] = { "Stretch", "Fit", "Fill", "Tile", "NineSlice" };
				static const EUIImageMode Modes[] = { EUIImageMode::Stretch, EUIImageMode::Fit, EUIImageMode::Fill, EUIImageMode::Tile, EUIImageMode::NineSlice };
				const FString ModeText = Value["Mode"].is_string() ? ToText( Value["Mode"] ) : FString();
				bool bFound = false;
				for ( int32 Index = 0; Index < 5; ++Index )
				{
					if ( ModeText == ModeNames[Index] )
					{
						Mode = Modes[Index];
						bFound = true;
					}
				}
				if ( !bFound )
				{
					return Fail( FString::Printf( "%s.Mode must be Stretch, Fit, Fill, Tile or NineSlice", *Where ) );
				}
			}
			FColor32 Tint( 255, 255, 255, 255 );
			if ( Value.contains( "Tint" ) && !ReadColor( Value["Tint"], Where + ".Tint", true, Tint ) )
			{
				return false;
			}
			FUIMargin Margin;
			const bool bHasMargin = Value.contains( "Margin" );
			if ( bHasMargin && !ReadMargin( Value["Margin"], Where + ".Margin", Margin ) )
			{
				return false;
			}
			if ( bHasMargin && Mode != EUIImageMode::NineSlice )
			{
				return Fail( FString::Printf( "%s.Margin is only for \"Mode\": \"NineSlice\"", *Where ) );
			}
			const TSharedPtr<CTexture> Texture = Assets != nullptr ? Assets->LoadTexture( Path ) : TSharedPtr<CTexture>();
			if ( !Texture )
			{
				return Fail( FString::Printf( "%s: the image '%s' could not be loaded", *Where, *Path ) );
			}
			const FVector2 Size( static_cast<float32>( Texture->GetWidth() ), static_cast<float32>( Texture->GetHeight() ) );
			if ( Mode == EUIImageMode::NineSlice )
			{
				if ( !bHasMargin )
				{
					Margin = FUIMargin( Size.x * 0.25f, Size.y * 0.25f, Size.x * 0.25f, Size.y * 0.25f );
				}
				Out = FUIBrush::MakeNineSlice( Texture, Size, Margin, Tint );
			}
			else
			{
				Out = FUIBrush::MakeImage( Texture, Size, Mode, Tint );
			}
			return true;
		}

		bool ReadMaterialBrush( const NlohmannJson& Value, const FString& Where, FUIBrush& Out )
		{
			static const char* const Allowed[] = { "Material", "Tint" };
			FString Unknown;
			if ( !HasOnlyKeys( Value, Allowed, 2, Unknown ) )
			{
				return Fail( FString::Printf( "%s: '%s' is not known for a material brush (Material, Tint)", *Where, *Unknown ) );
			}
			if ( !Value["Material"].is_string() )
			{
				return Fail( FString::Printf( "%s.Material must be a path", *Where ) );
			}
			const FString Path = ToText( Value["Material"] );
			FColor32 Tint( 255, 255, 255, 255 );
			if ( Value.contains( "Tint" ) && !ReadColor( Value["Tint"], Where + ".Tint", true, Tint ) )
			{
				return false;
			}
			CMaterialSubsystem* Materials = GetAppSubsystem<CMaterialSubsystem>();
			const TSharedPtr<CMaterial> Material = Materials != nullptr ? Materials->LoadMaterial( Path ) : TSharedPtr<CMaterial>();
			if ( !Material.IsValid() || !Material->IsValid() )
			{
				return Fail( FString::Printf( "%s: the material '%s' could not be loaded", *Where, *Path ) );
			}
			Out = FUIBrush::MakeMaterial( Material, Tint );
			return true;
		}

		bool ReadSolidBrush( const NlohmannJson& Value, const FString& Where, FUIBrush& Out )
		{
			static const char* const Allowed[] = { "Color", "CornerRadius", "OutlineColor", "OutlineThickness" };
			FString Unknown;
			if ( !HasOnlyKeys( Value, Allowed, 4, Unknown ) )
			{
				return Fail( FString::Printf( "%s: '%s' is not known for a brush (Color, CornerRadius, OutlineColor, OutlineThickness, or Image / Material / Layers)", *Where, *Unknown ) );
			}
			if ( !Value.contains( "Color" ) )
			{
				return Fail( FString::Printf( "%s needs one of Color, Image, Material or Layers", *Where ) );
			}
			FColor32 Color;
			float32 CornerRadius = 0.0f;
			FColor32 OutlineColor( 255, 255, 255, 0 );
			float32 OutlineThickness = 0.0f;
			if ( !ReadColor( Value["Color"], Where + ".Color", true, Color ) )
			{
				return false;
			}
			if ( Value.contains( "CornerRadius" ) && !ReadNumber( Value["CornerRadius"], Where + ".CornerRadius", CornerRadius ) )
			{
				return false;
			}
			if ( Value.contains( "OutlineColor" ) && !ReadColor( Value["OutlineColor"], Where + ".OutlineColor", true, OutlineColor ) )
			{
				return false;
			}
			if ( Value.contains( "OutlineThickness" ) && !ReadNumber( Value["OutlineThickness"], Where + ".OutlineThickness", OutlineThickness ) )
			{
				return false;
			}
			if ( CornerRadius < 0.0f || OutlineThickness < 0.0f )
			{
				return Fail( FString::Printf( "%s: CornerRadius and OutlineThickness must be 0 or more", *Where ) );
			}
			Out = FUIBrush::MakeSolidWithOutline( Color, CornerRadius, OutlineColor, OutlineThickness );
			return true;
		}

		bool ReadBrush( const NlohmannJson& Value, const FString& Where, FUIBrush& Out )
		{
			if ( Value.is_string() )
			{
				const FString Text = ToText( Value ).TrimStartAndEnd();
				if ( Text.Len() >= 1 && ( ( *Text )[0] == '#' || ( *Text )[0] == '@' ) )
				{
					FColor32 Color;
					if ( !ReadColor( Value, Where, true, Color ) )
					{
						return false;
					}
					Out = FUIBrush::MakeSolid( Color );
					return true;
				}
				if ( Text.IsEmpty() )
				{
					return Fail( FString::Printf( "%s must not be empty", *Where ) );
				}
				return ResolveNamedBrush( Text, Where, Out );
			}
			if ( !Value.is_object() )
			{
				return Fail( FString::Printf( "%s must be a color, a brush name or an object", *Where ) );
			}
			if ( Value.contains( "Image" ) )
			{
				return ReadImageBrush( Value, Where, Out );
			}
			if ( Value.contains( "Material" ) )
			{
				return ReadMaterialBrush( Value, Where, Out );
			}
			if ( Value.contains( "Layers" ) )
			{
				if ( Value.size() != 1 || !Value["Layers"].is_array() )
				{
					return Fail( FString::Printf( "%s: a layered brush has only \"Layers\": [ brush, ... ]", *Where ) );
				}
				TArray<FUIBrush> Layers;
				int32 LayerIndex = 0;
				for ( auto Iterator = Value["Layers"].begin(); Iterator != Value["Layers"].end(); ++Iterator, ++LayerIndex )
				{
					FUIBrush Layer;
					if ( !ReadBrush( *Iterator, FString::Printf( "%s.Layers[%d]", *Where, LayerIndex ), Layer ) )
					{
						return false;
					}
					Layers.Add( Layer );
				}
				Out = FUIBrush::MakeLayered( Layers );
				return true;
			}
			return ReadSolidBrush( Value, Where, Out );
		}

		// スタイル 1 つ: 項目（ブラシ）を、名前の順に Targets へ読む。書かなかった項目は、そのまま
		bool ReadStyleBrushes( const NlohmannJson& Value, const FString& Where, const char* const* FieldNames, FUIBrush* const* Targets, int32 NumFields )
		{
			if ( !Value.is_object() )
			{
				return Fail( FString::Printf( "%s must be an object", *Where ) );
			}
			FString Unknown;
			if ( !HasOnlyKeys( Value, FieldNames, NumFields, Unknown ) )
			{
				return Fail( FString::Printf( "%s: '%s' is not known", *Where, *Unknown ) );
			}
			for ( int32 Index = 0; Index < NumFields; ++Index )
			{
				if ( Value.contains( FieldNames[Index] ) && !ReadBrush( Value[FieldNames[Index]], Where + "." + FieldNames[Index], *Targets[Index] ) )
				{
					return false;
				}
			}
			return true;
		}

		bool Build( const NlohmannJson& Document )
		{
			if ( !Document.is_object() )
			{
				return Fail( "the theme must be a JSON object" );
			}
			static const char* const Allowed[] = { "Name", "Colors", "Numbers", "Brushes", "ButtonStyles", "CheckBoxStyles" };
			FString Unknown;
			if ( !HasOnlyKeys( Document, Allowed, 6, Unknown ) )
			{
				return Fail( FString::Printf( "'%s' is not known (Name, Colors, Numbers, Brushes, ButtonStyles, CheckBoxStyles)", *Unknown ) );
			}
			for ( int32 Index = 1; Index < 6; ++Index )
			{
				if ( Document.contains( Allowed[Index] ) && !Document[Allowed[Index]].is_object() )
				{
					return Fail( FString::Printf( "%s must be an object", Allowed[Index] ) );
				}
			}
			if ( Document.contains( "Name" ) )
			{
				if ( !Document["Name"].is_string() )
				{
					return Fail( "Name must be a string" );
				}
				Theme.SetName( ToText( Document["Name"] ) );
			}
			if ( Document.contains( "Colors" ) )
			{
				for ( auto Iterator = Document["Colors"].begin(); Iterator != Document["Colors"].end(); ++Iterator )
				{
					const FString Key( Iterator.key().c_str() );
					FColor32 Color;
					if ( !ReadColor( Iterator.value(), "Colors." + Key, false, Color ) )
					{
						return false;
					}
					Theme.SetColor( Key, Color );
				}
			}
			if ( Document.contains( "Numbers" ) )
			{
				for ( auto Iterator = Document["Numbers"].begin(); Iterator != Document["Numbers"].end(); ++Iterator )
				{
					const FString Key( Iterator.key().c_str() );
					float32 Number = 0.0f;
					if ( !ReadNumber( Iterator.value(), "Numbers." + Key, Number ) )
					{
						return false;
					}
					Theme.SetNumber( Key, Number );
				}
			}
			if ( Document.contains( "Brushes" ) )
			{
				BrushTable = &Document["Brushes"];
				for ( auto Iterator = BrushTable->begin(); Iterator != BrushTable->end(); ++Iterator )
				{
					FUIBrush Brush;
					if ( !ResolveNamedBrush( FString( Iterator.key().c_str() ), "Brushes", Brush ) )
					{
						return false;
					}
				}
			}
			if ( Document.contains( "ButtonStyles" ) )
			{
				for ( auto Iterator = Document["ButtonStyles"].begin(); Iterator != Document["ButtonStyles"].end(); ++Iterator )
				{
					const FString Key( Iterator.key().c_str() );
					FUIButtonStyle Style;
					static const char* const FieldNames[] = { "Normal", "Hover", "Pressed", "Disabled", "FocusOverlay" };
					FUIBrush* const Targets[] = { &Style.Normal, &Style.Hover, &Style.Pressed, &Style.Disabled, &Style.FocusOverlay };
					if ( !ReadStyleBrushes( Iterator.value(), "ButtonStyles." + Key, FieldNames, Targets, 5 ) )
					{
						return false;
					}
					Theme.SetButtonStyle( Key, Style );
				}
			}
			if ( Document.contains( "CheckBoxStyles" ) )
			{
				for ( auto Iterator = Document["CheckBoxStyles"].begin(); Iterator != Document["CheckBoxStyles"].end(); ++Iterator )
				{
					const FString Key( Iterator.key().c_str() );
					FUICheckBoxStyle Style;
					static const char* const FieldNames[] = { "Off", "On", "HoverOverlay", "DisabledOverlay", "FocusOverlay" };
					FUIBrush* const Targets[] = { &Style.Off, &Style.On, &Style.HoverOverlay, &Style.DisabledOverlay, &Style.FocusOverlay };
					if ( !ReadStyleBrushes( Iterator.value(), "CheckBoxStyles." + Key, FieldNames, Targets, 5 ) )
					{
						return false;
					}
					Theme.SetCheckBoxStyle( Key, Style );
				}
			}
			return true;
		}
	};
}

TSharedPtr<FUITheme> CUIThemeLoader::LoadFromText( const FString& Json, CAssetSubsystem* Assets, const TSharedPtr<FUITheme>& Parent, FString& OutError )
{
	OutError.Empty();
	// コメント（// と /* */）を許す
	const NlohmannJson Document = NlohmannJson::parse( NJson::ToJsonString( Json ), nullptr, false, true );
	if ( Document.is_discarded() )
	{
		OutError = "the theme is not valid JSON";
		return TSharedPtr<FUITheme>();
	}
	TSharedPtr<FUITheme> Theme = MakeShared<FUITheme>();
	Theme->SetParent( Parent );
	FThemeBuilder Builder( Assets, *Theme );
	if ( !Builder.Build( Document ) )
	{
		OutError = Builder.Error;
		return TSharedPtr<FUITheme>();
	}
	return Theme;
}

TSharedPtr<FUITheme> CUIThemeLoader::Load( CAssetSubsystem& Assets, const FString& Path, const TSharedPtr<FUITheme>& Parent, FString& OutError )
{
	FString Text;
	if ( !CUIMarkupDocument::ReadText( Assets, Path, Text ) )
	{
		OutError = FString::Printf( "the theme file '%s' could not be read", *Path );
		return TSharedPtr<FUITheme>();
	}
	TSharedPtr<FUITheme> Theme = LoadFromText( Text, &Assets, Parent, OutError );
	if ( !Theme )
	{
		OutError = FString::Printf( "%s: %s", *Path, *OutError );
	}
	return Theme;
}
