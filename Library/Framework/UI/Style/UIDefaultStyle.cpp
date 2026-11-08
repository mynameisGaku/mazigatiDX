// SPDX-License-Identifier: MIT
#include "UIDefaultStyle.h"

#include "Library/Core/File/File.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Json/JsonObject.h"

namespace
{
	const char* UIPrefix = "Engine:/UI/";

	// manifest.json から読んだ、画像ごとの 9 分割の余白
	struct FManifestEntry
	{
		FString Name;
		FUIMargin Margin;
	};

	TArray<FManifestEntry> LoadManifest( CAssetSubsystem& Assets )
	{
		TArray<FManifestEntry> Entries;
		const TOptional<FString> Path = Assets.ResolvePath( FString( UIPrefix ) + "manifest.json" );
		TArray<uint8> Bytes;
		if ( !Path.IsSet() || !NFile::ReadAllBytes( Path.Get( FString() ), Bytes, true ) )
		{
			return Entries;
		}
		const FString Text( reinterpret_cast<const char*>( Bytes.GetData() ), Bytes.Num() );
		const NlohmannJson Document = NlohmannJson::parse( NJson::ToJsonString( Text ), nullptr, false, true );
		if ( Document.is_discarded() || !Document.contains( "Images" ) || !Document["Images"].is_object() )
		{
			return Entries;
		}
		for ( auto Iterator = Document["Images"].begin(); Iterator != Document["Images"].end(); ++Iterator )
		{
			FManifestEntry Entry;
			Entry.Name = FString( Iterator.key().c_str() );
			if ( Iterator.value().is_object() && Iterator.value().contains( "NineSlice" ) && Iterator.value()["NineSlice"].is_array() && Iterator.value()["NineSlice"].size() == 4 )
			{
				const NlohmannJson& Values = Iterator.value()["NineSlice"];
				Entry.Margin = FUIMargin( Values[0].get<float>(), Values[1].get<float>(), Values[2].get<float>(), Values[3].get<float>() );
			}
			Entries.Add( Entry );
		}
		return Entries;
	}

	class FImageLoader
	{
	public:
		explicit FImageLoader( CAssetSubsystem& InAssets )
			: Assets( InAssets )
			, Manifest( LoadManifest( InAssets ) )
		{
		}

		bool bAllLoaded = true;

		// 9 分割の画像。余白は manifest から（無ければ、画像の大きさの 1/4）
		FUIBrush NineSlice( const char* Name, const FColor32& Tint = FColor32( 255, 255, 255, 255 ) )
		{
			const TSharedPtr<CTexture> Texture = Assets.LoadTexture( FString( UIPrefix ) + Name );
			if ( !Texture )
			{
				bAllLoaded = false;
				return FUIBrush();
			}
			const FVector2 Size( static_cast<float32>( Texture->GetWidth() ), static_cast<float32>( Texture->GetHeight() ) );
			FUIMargin Margin( Size.x * 0.25f, Size.y * 0.25f, Size.x * 0.25f, Size.y * 0.25f );
			for ( int32 Index = 0; Index < Manifest.Num(); ++Index )
			{
				if ( Manifest[Index].Name == Name )
				{
					Margin = Manifest[Index].Margin;
					break;
				}
			}
			return FUIBrush::MakeNineSlice( Texture, Size, Margin, Tint );
		}

		// 引き伸ばして貼る画像（チェック・つまみなど）
		FUIBrush Stretched( const char* Name, const FColor32& Tint = FColor32( 255, 255, 255, 255 ) )
		{
			const TSharedPtr<CTexture> Texture = Assets.LoadTexture( FString( UIPrefix ) + Name );
			if ( !Texture )
			{
				bAllLoaded = false;
				return FUIBrush();
			}
			return FUIBrush::MakeImage( Texture, FVector2( static_cast<float32>( Texture->GetWidth() ), static_cast<float32>( Texture->GetHeight() ) ), EUIImageMode::Stretch, Tint );
		}

	private:
		CAssetSubsystem& Assets;
		TArray<FManifestEntry> Manifest;
	};
}

bool FUIDefaultStyle::Load( CAssetSubsystem& Assets, FUIDefaultStyle& OutStyle )
{
	FImageLoader Loader( Assets );
	const FColor32 FocusTint( 255, 215, 110, 255 );

	OutStyle.Window = Loader.NineSlice( "Window.png" );
	OutStyle.Panel = Loader.NineSlice( "Panel.png" );
	OutStyle.Frame = Loader.NineSlice( "Frame.png" );
	OutStyle.Tooltip = Loader.NineSlice( "Tooltip.png" );
	OutStyle.TextBox = Loader.NineSlice( "TextBox.png" );
	OutStyle.TextBoxFocused = Loader.NineSlice( "TextBox_Focused.png" );
	OutStyle.Shadow = Loader.NineSlice( "Shadow.png" );
	OutStyle.ProgressBack = Loader.NineSlice( "ProgressBar_Back.png" );
	OutStyle.ProgressFill = Loader.NineSlice( "ProgressBar_Fill.png" );

	OutStyle.Button.Normal = Loader.NineSlice( "Button_Normal.png" );
	OutStyle.Button.Hover = Loader.NineSlice( "Button_Hover.png" );
	OutStyle.Button.Pressed = Loader.NineSlice( "Button_Pressed.png" );
	OutStyle.Button.Disabled = Loader.NineSlice( "Button_Disabled.png" );
	OutStyle.Button.FocusOverlay = Loader.NineSlice( "Frame.png", FocusTint );

	OutStyle.CheckBox.Off = Loader.Stretched( "CheckBox_Off.png" );
	OutStyle.CheckBox.On = Loader.Stretched( "CheckBox_On.png" );
	OutStyle.CheckBox.HoverOverlay = Loader.NineSlice( "RoundedWhite.png", FColor32( 255, 255, 255, 36 ) );
	OutStyle.CheckBox.DisabledOverlay = Loader.NineSlice( "RoundedWhite.png", FColor32( 0, 0, 0, 120 ) );
	OutStyle.CheckBox.FocusOverlay = Loader.NineSlice( "Frame.png", FocusTint );

	OutStyle.Slider.Track = Loader.NineSlice( "Slider_Track.png" );
	OutStyle.Slider.Fill = Loader.NineSlice( "Slider_Fill.png" );
	OutStyle.Slider.Handle = Loader.Stretched( "Slider_Handle.png" );
	OutStyle.Slider.HandleHover = Loader.Stretched( "Slider_Handle.png", FColor32( 255, 235, 190, 255 ) );
	OutStyle.Slider.HandleSize = FVector2( 20.0f, 20.0f );
	// 中身の画像は、溝の画像と同じ大きさで、枠の内側に収まる形なので、溝と同じ矩形に重ねる（縮めない）
	OutStyle.Slider.FillInset = FUIMargin( 0.0f );

	return Loader.bAllLoaded;
}

TSharedPtr<FUITheme> FUIDefaultStyle::MakeTheme( const TSharedPtr<CFont>& Body, const TSharedPtr<CFont>& Title ) const
{
	TSharedPtr<FUITheme> Theme = MakeShared<FUITheme>();
	Theme->SetName( "Default" );
	Theme->SetBrush( "Window", Window );
	Theme->SetBrush( "Panel", Panel );
	Theme->SetBrush( "Frame", Frame );
	Theme->SetBrush( "Tooltip", Tooltip );
	Theme->SetBrush( "TextBox", TextBox );
	Theme->SetBrush( "TextBoxFocused", TextBoxFocused );
	Theme->SetBrush( "Shadow", Shadow );
	Theme->SetBrush( "ProgressBack", ProgressBack );
	Theme->SetBrush( "ProgressFill", ProgressFill );

	Theme->SetButtonStyle( "Default", Button );
	Theme->SetCheckBoxStyle( "Default", CheckBox );
	Theme->SetSliderStyle( "Default", Slider );
	FUITextBoxStyle TextBoxStyle;
	TextBoxStyle.Normal = TextBox;
	TextBoxStyle.Focused = TextBoxFocused;
	TextBoxStyle.Disabled = TextBox;
	Theme->SetTextBoxStyle( "Default", TextBoxStyle );
	FUIDropdownStyle DropdownStyle;
	DropdownStyle.Button = Button;
	// 一覧は、後ろが透けないよう、不透明にする
	DropdownStyle.PopupBackground = FUIBrush::MakeSolidWithOutline( FColor32( 26, 31, 46, 255 ), 6.0f, FColor32( 120, 136, 184, 255 ), 1.5f );
	DropdownStyle.ItemSelected = FUIBrush::MakeSolid( FColor32( 60, 100, 170, 220 ), 4.0f );
	Theme->SetDropdownStyle( "Default", DropdownStyle );

	Theme->SetColor( "Text", FColor32( 235, 238, 245, 255 ) );
	Theme->SetColor( "TextMuted", FColor32( 150, 160, 180, 255 ) );
	Theme->SetColor( "Accent", FColor32( 255, 226, 150, 255 ) );
	Theme->SetColor( "Value", FColor32( 180, 220, 255, 255 ) );
	Theme->SetColor( "Error", FColor32( 255, 150, 150, 255 ) );
	Theme->SetNumber( "Gap", 14.0f );
	Theme->SetNumber( "Spacing", 18.0f );
	if ( Body )
	{
		Theme->SetFont( "Body", Body );
	}
	if ( Title )
	{
		Theme->SetFont( "Title", Title );
	}
	return Theme;
}
