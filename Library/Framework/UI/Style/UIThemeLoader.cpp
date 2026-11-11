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

	// スタイルの項目の種類
	enum class EStyleFieldType
	{
		Brush,    // ブラシ
		Color,    // 色（"#RRGGBB" か "@名前"）
		Number,   // 0 以上の数
		Count,    // 1 以上の整数
		Margin,   // 数・[左右, 上下]・[左, 上, 右, 下]
		Size      // 数（縦横とも）か [幅, 高さ]
	};

	// スタイルの項目 1 つ（名前・種類・読んだ値の入れ先）
	struct FStyleField
	{
		const char* Name;
		EStyleFieldType Type;
		void* Target;
	};

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

		// 数（四辺）か、[左, 上, 右, 下]。bAllowPair なら、[左右, 上下] も
		bool ReadMargin( const NlohmannJson& Value, const FString& Where, FUIMargin& Out, bool bAllowPair = false )
		{
			if ( Value.is_number() )
			{
				Out = FUIMargin( Value.get<float>() );
				return true;
			}
			if ( bAllowPair && Value.is_array() && Value.size() == 2 && Value[0].is_number() && Value[1].is_number() )
			{
				Out = FUIMargin( Value[0].get<float>(), Value[1].get<float>() );
				return true;
			}
			if ( Value.is_array() && Value.size() == 4 && Value[0].is_number() && Value[1].is_number() && Value[2].is_number() && Value[3].is_number() )
			{
				Out = FUIMargin( Value[0].get<float>(), Value[1].get<float>(), Value[2].get<float>(), Value[3].get<float>() );
				return true;
			}
			return Fail( FString::Printf( bAllowPair ? "%s must be a number, [horizontal, vertical] or [left, top, right, bottom]" : "%s must be a number or [left, top, right, bottom]", *Where ) );
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

		// スタイル 1 つ: 書いてある項目を、それぞれの入れ先へ読む。書かなかった項目は、そのまま。
		// "Extends"（元にするスタイルの名前）は、呼ぶ側が先に処理するので、ここでは読み飛ばす。ExtraKey は、呼ぶ側が自分で読む項目（無ければ null）
		bool ReadStyleFields( const NlohmannJson& Value, const FString& Where, const FStyleField* Fields, int32 NumFields, const char* ExtraKey = nullptr )
		{
			if ( !Value.is_object() )
			{
				return Fail( FString::Printf( "%s must be an object", *Where ) );
			}
			for ( auto Iterator = Value.begin(); Iterator != Value.end(); ++Iterator )
			{
				const FString Key( Iterator.key().c_str() );
				bool bKnown = Key == "Extends" || ( ExtraKey != nullptr && Key == ExtraKey );
				for ( int32 Index = 0; Index < NumFields; ++Index )
				{
					bKnown = bKnown || Key == Fields[Index].Name;
				}
				if ( !bKnown )
				{
					return Fail( FString::Printf( "%s: '%s' is not known", *Where, *Key ) );
				}
			}
			for ( int32 Index = 0; Index < NumFields; ++Index )
			{
				const FStyleField& Field = Fields[Index];
				if ( !Value.contains( Field.Name ) )
				{
					continue;
				}
				const NlohmannJson& Item = Value[Field.Name];
				const FString ItemWhere = Where + "." + Field.Name;
				switch ( Field.Type )
				{
					case EStyleFieldType::Brush:
						if ( !ReadBrush( Item, ItemWhere, *static_cast<FUIBrush*>( Field.Target ) ) )
						{
							return false;
						}
						break;
					case EStyleFieldType::Color:
						if ( !ReadColor( Item, ItemWhere, true, *static_cast<FColor32*>( Field.Target ) ) )
						{
							return false;
						}
						break;
					case EStyleFieldType::Number:
					{
						float32 Number = 0.0f;
						if ( !ReadNumber( Item, ItemWhere, Number ) )
						{
							return false;
						}
						if ( Number < 0.0f )
						{
							return Fail( FString::Printf( "%s must be 0 or more", *ItemWhere ) );
						}
						*static_cast<float32*>( Field.Target ) = Number;
						break;
					}
					case EStyleFieldType::Count:
						if ( !Item.is_number_integer() || Item.get<int64>() < 1 || Item.get<int64>() > 100000 )
						{
							return Fail( FString::Printf( "%s must be a whole number of 1 or more", *ItemWhere ) );
						}
						*static_cast<int32*>( Field.Target ) = static_cast<int32>( Item.get<int64>() );
						break;
					case EStyleFieldType::Margin:
						if ( !ReadMargin( Item, ItemWhere, *static_cast<FUIMargin*>( Field.Target ), true ) )
						{
							return false;
						}
						break;
					case EStyleFieldType::Size:
					{
						FVector2 Size;
						if ( Item.is_number() )
						{
							Size = FVector2( Item.get<float>(), Item.get<float>() );
						}
						else if ( Item.is_array() && Item.size() == 2 && Item[0].is_number() && Item[1].is_number() )
						{
							Size = FVector2( Item[0].get<float>(), Item[1].get<float>() );
						}
						else
						{
							return Fail( FString::Printf( "%s must be a number or [width, height]", *ItemWhere ) );
						}
						if ( Size.x < 0.0f || Size.y < 0.0f )
						{
							return Fail( FString::Printf( "%s must be 0 or more", *ItemWhere ) );
						}
						*static_cast<FVector2*>( Field.Target ) = Size;
						break;
					}
				}
			}
			return true;
		}

		bool ReadButtonStyle( const NlohmannJson& Value, const FString& Where, FUIButtonStyle& Style )
		{
			const FStyleField Fields[] = {
				{ "Normal", EStyleFieldType::Brush, &Style.Normal },
				{ "Hover", EStyleFieldType::Brush, &Style.Hover },
				{ "Pressed", EStyleFieldType::Brush, &Style.Pressed },
				{ "Disabled", EStyleFieldType::Brush, &Style.Disabled },
				{ "FocusOverlay", EStyleFieldType::Brush, &Style.FocusOverlay },
			};
			return ReadStyleFields( Value, Where, Fields, 5 );
		}

		bool ReadCheckBoxStyle( const NlohmannJson& Value, const FString& Where, FUICheckBoxStyle& Style )
		{
			const FStyleField Fields[] = {
				{ "Off", EStyleFieldType::Brush, &Style.Off },
				{ "On", EStyleFieldType::Brush, &Style.On },
				{ "HoverOverlay", EStyleFieldType::Brush, &Style.HoverOverlay },
				{ "DisabledOverlay", EStyleFieldType::Brush, &Style.DisabledOverlay },
				{ "FocusOverlay", EStyleFieldType::Brush, &Style.FocusOverlay },
			};
			return ReadStyleFields( Value, Where, Fields, 5 );
		}

		bool ReadSliderStyle( const NlohmannJson& Value, const FString& Where, FUISliderStyle& Style )
		{
			const FStyleField Fields[] = {
				{ "Track", EStyleFieldType::Brush, &Style.Track },
				{ "Fill", EStyleFieldType::Brush, &Style.Fill },
				{ "Handle", EStyleFieldType::Brush, &Style.Handle },
				{ "HandleHover", EStyleFieldType::Brush, &Style.HandleHover },
				{ "HandleSize", EStyleFieldType::Size, &Style.HandleSize },
				{ "TrackInset", EStyleFieldType::Margin, &Style.TrackInset },
				{ "FillInset", EStyleFieldType::Margin, &Style.FillInset },
			};
			return ReadStyleFields( Value, Where, Fields, 7 );
		}

		bool ReadTextBoxStyle( const NlohmannJson& Value, const FString& Where, FUITextBoxStyle& Style )
		{
			const FStyleField Fields[] = {
				{ "Normal", EStyleFieldType::Brush, &Style.Normal },
				{ "Focused", EStyleFieldType::Brush, &Style.Focused },
				{ "Disabled", EStyleFieldType::Brush, &Style.Disabled },
				{ "TextColor", EStyleFieldType::Color, &Style.TextColor },
				{ "PlaceholderColor", EStyleFieldType::Color, &Style.PlaceholderColor },
				{ "DisabledTextColor", EStyleFieldType::Color, &Style.DisabledTextColor },
				{ "CaretColor", EStyleFieldType::Color, &Style.CaretColor },
				{ "SelectionColor", EStyleFieldType::Color, &Style.SelectionColor },
				{ "TextPadding", EStyleFieldType::Margin, &Style.TextPadding },
			};
			return ReadStyleFields( Value, Where, Fields, 9 );
		}

		bool ReadDropdownStyle( const NlohmannJson& Value, const FString& Where, FUIDropdownStyle& Style )
		{
			const FStyleField Fields[] = {
				{ "Arrow", EStyleFieldType::Brush, &Style.Arrow },
				{ "ArrowSize", EStyleFieldType::Size, &Style.ArrowSize },
				{ "TextPadding", EStyleFieldType::Margin, &Style.TextPadding },
				{ "TextColor", EStyleFieldType::Color, &Style.TextColor },
				{ "PlaceholderColor", EStyleFieldType::Color, &Style.PlaceholderColor },
				{ "PopupBackground", EStyleFieldType::Brush, &Style.PopupBackground },
				{ "PopupPadding", EStyleFieldType::Margin, &Style.PopupPadding },
				{ "ItemSelected", EStyleFieldType::Brush, &Style.ItemSelected },
				{ "ItemTextColor", EStyleFieldType::Color, &Style.ItemTextColor },
				{ "ItemPadding", EStyleFieldType::Margin, &Style.ItemPadding },
				{ "ItemHeight", EStyleFieldType::Number, &Style.ItemHeight },
				{ "MaxVisibleItems", EStyleFieldType::Count, &Style.MaxVisibleItems },
				{ "ScrollTrack", EStyleFieldType::Brush, &Style.ScrollTrack },
				{ "ScrollThumb", EStyleFieldType::Brush, &Style.ScrollThumb },
				{ "ScrollBarWidth", EStyleFieldType::Number, &Style.ScrollBarWidth },
			};
			if ( !ReadStyleFields( Value, Where, Fields, 15, "Button" ) )
			{
				return false;
			}
			if ( !Value.contains( "Button" ) )
			{
				return true;
			}
			// 閉じているときの見た目: ボタンのスタイルの名前か、ボタンのスタイルと同じ書き方のオブジェクト
			const NlohmannJson& Button = Value["Button"];
			const FString ButtonWhere = Where + ".Button";
			if ( Button.is_string() )
			{
				const FString Name = ToText( Button );
				if ( !Theme.FindButtonStyle( Name, Style.Button ) )
				{
					return Fail( FString::Printf( "%s: the button style '%s' is not defined", *ButtonWhere, *Name ) );
				}
				return true;
			}
			if ( Button.is_object() && Button.contains( "Extends" ) )
			{
				const FString Name = Button["Extends"].is_string() ? ToText( Button["Extends"] ) : FString();
				if ( !Theme.FindButtonStyle( Name, Style.Button ) )
				{
					return Fail( FString::Printf( "%s.Extends: the button style '%s' is not defined", *ButtonWhere, *Name ) );
				}
			}
			return ReadButtonStyle( Button, ButtonWhere, Style.Button );
		}

		// スタイルの表 1 つ（"ButtonStyles" など）を読んで、テーマへ入れる。
		// "Extends" で、同じ種類のスタイルを元にできる（同じファイルの中なら、書く順に関係なく。無ければ、親のテーマから）。輪になる "Extends" は、失敗
		template<typename TStyle, typename TReadFunction>
		bool BuildStyles( const NlohmannJson& Document, const char* TableName, bool ( FUITheme::*FindStyle )( const FString&, TStyle& ) const, void ( FUITheme::*SetStyle )( const FString&, const TStyle& ), TReadFunction Read )
		{
			if ( !Document.contains( TableName ) )
			{
				return true;
			}
			const NlohmannJson& Table = Document[TableName];
			TArray<FString> Pending;
			for ( auto Iterator = Table.begin(); Iterator != Table.end(); ++Iterator )
			{
				Pending.Add( FString( Iterator.key().c_str() ) );
			}
			while ( !Pending.IsEmpty() )
			{
				bool bProgress = false;
				for ( int32 Index = 0; Index < Pending.Num(); )
				{
					const FString Key = Pending[Index];
					const FString Where = FString::Printf( "%s.%s", TableName, *Key );
					const NlohmannJson& Value = Table[NJson::ToJsonString( Key )];
					if ( !Value.is_object() )
					{
						return Fail( FString::Printf( "%s must be an object", *Where ) );
					}
					TStyle Style;
					if ( Value.contains( "Extends" ) )
					{
						if ( !Value["Extends"].is_string() )
						{
							return Fail( FString::Printf( "%s.Extends must be the name of a style", *Where ) );
						}
						const FString BaseName = ToText( Value["Extends"] );
						// 自分と同じ名前は、親のテーマの、同じ名前のスタイル（親のものを元に、上書きする）
						if ( BaseName != Key && Pending.Contains( BaseName ) )
						{
							// 元にするスタイルが、このファイルの、まだ作っていないもの: 後で（輪になっていれば、進まないので、下で失敗になる）
							++Index;
							continue;
						}
						if ( !( Theme.*FindStyle )( BaseName, Style ) )
						{
							return Fail( FString::Printf( "%s.Extends: the style '%s' is not defined", *Where, *BaseName ) );
						}
					}
					if ( !Read( Value, Where, Style ) )
					{
						return false;
					}
					( Theme.*SetStyle )( Key, Style );
					Pending.RemoveAt( Index );
					bProgress = true;
				}
				if ( !bProgress )
				{
					return Fail( FString::Printf( "%s.%s.Extends: the styles extend each other in a circle", TableName, *Pending[0] ) );
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
			static const char* const Allowed[] = { "Name", "Colors", "Numbers", "Brushes", "ButtonStyles", "CheckBoxStyles", "SliderStyles", "TextBoxStyles", "DropdownStyles" };
			const int32 NumAllowed = 9;
			FString Unknown;
			if ( !HasOnlyKeys( Document, Allowed, NumAllowed, Unknown ) )
			{
				return Fail( FString::Printf( "'%s' is not known (Name, Colors, Numbers, Brushes, ButtonStyles, CheckBoxStyles, SliderStyles, TextBoxStyles, DropdownStyles)", *Unknown ) );
			}
			for ( int32 Index = 1; Index < NumAllowed; ++Index )
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
			// ボタンのスタイルは、ドロップダウンのスタイルが名前で使うので、先に作る
			return BuildStyles<FUIButtonStyle>( Document, "ButtonStyles", &FUITheme::FindButtonStyle, &FUITheme::SetButtonStyle, [this]( const NlohmannJson& Value, const FString& Where, FUIButtonStyle& Style ) { return ReadButtonStyle( Value, Where, Style ); } )
				   && BuildStyles<FUICheckBoxStyle>( Document, "CheckBoxStyles", &FUITheme::FindCheckBoxStyle, &FUITheme::SetCheckBoxStyle, [this]( const NlohmannJson& Value, const FString& Where, FUICheckBoxStyle& Style ) { return ReadCheckBoxStyle( Value, Where, Style ); } )
				   && BuildStyles<FUISliderStyle>( Document, "SliderStyles", &FUITheme::FindSliderStyle, &FUITheme::SetSliderStyle, [this]( const NlohmannJson& Value, const FString& Where, FUISliderStyle& Style ) { return ReadSliderStyle( Value, Where, Style ); } )
				   && BuildStyles<FUITextBoxStyle>( Document, "TextBoxStyles", &FUITheme::FindTextBoxStyle, &FUITheme::SetTextBoxStyle, [this]( const NlohmannJson& Value, const FString& Where, FUITextBoxStyle& Style ) { return ReadTextBoxStyle( Value, Where, Style ); } )
				   && BuildStyles<FUIDropdownStyle>( Document, "DropdownStyles", &FUITheme::FindDropdownStyle, &FUITheme::SetDropdownStyle, [this]( const NlohmannJson& Value, const FString& Where, FUIDropdownStyle& Style ) { return ReadDropdownStyle( Value, Where, Style ); } );
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
