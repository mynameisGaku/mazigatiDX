// SPDX-License-Identifier: MIT
#include "UIScreen.h"

#include <cmath>

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Input/ITextInput.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/UI/Core/UIInputBridge.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	constexpr float32 ScreenWidth = 1280.0f;
	constexpr float32 ScreenHeight = 720.0f;

	// アイコンの一覧に出す名前（EngineContent/Icons の中から）
	const char* IconNames[] = {
		"Home", "Settings", "Search", "Menu", "Plus", "Minus", "Check", "Close",
		"Play", "Pause", "Stop", "Volume", "Heart", "Star", "Coin", "Key",
		"Sword", "Shield", "Potion", "Bag", "Map", "Chest", "Gem", "Flame",
		"Eye", "EyeClosed", "Lock", "Unlock", "Bell", "Mail", "Trophy", "Gamepad",
	};
	constexpr int32 IconColumns = 4;
	constexpr int32 NumListItems = 10000;
	constexpr float32 IconCell = 56.0f;

	// Canvas の中に、位置と大きさを決めて置く
	void PlaceAt( CUICanvasPanel& Canvas, const TSharedPtr<CUIWidget>& Widget, float32 X, float32 Y, float32 Width, float32 Height )
	{
		FUILayoutProperties Layout = Widget->GetLayout();
		Layout.Offsets = FUIMargin( X, Y, Width, Height );
		Widget->SetLayout( Layout );
		Canvas.AddChild( Widget );
	}

	FUILayoutProperties FixedHeight( float32 Height )
	{
		FUILayoutProperties Layout;
		Layout.FixedHeight = Height;
		return Layout;
	}

	TSharedPtr<CUIStackPanel> MakeStack( EUIOrientation Orientation, float32 Spacing )
	{
		TSharedPtr<CUIStackPanel> Stack = MakeShared<CUIStackPanel>();
		Stack->SetOrientation( Orientation );
		Stack->SetSpacing( Spacing );
		return Stack;
	}
}

TSharedPtr<CUILabel> CUIScreen::MakeLabel( const TSharedPtr<CFont>& Font, const char* Text, const FColor32& Color )
{
	TSharedPtr<CUILabel> Label = MakeShared<CUILabel>();
	Label->SetFont( Font );
	Label->SetText( Text );
	Label->SetColor( Color );
	FUILayoutProperties Layout;
	Layout.HorizontalAlignment = EUIAlignment::Start;
	Layout.VerticalAlignment = EUIAlignment::Center;
	Label->SetLayout( Layout );
	return Label;
}

TSharedPtr<CUIButton> CUIScreen::MakeButton( const TSharedPtr<CFont>& Font, const char* Text, float32 Width )
{
	TSharedPtr<CUIButton> Button = MakeShared<CUIButton>();
	Button->SetStyle( Style.Button );
	FUILayoutProperties Layout;
	Layout.FixedWidth = Width;
	Layout.FixedHeight = 44.0f;
	Layout.VerticalAlignment = EUIAlignment::Center;
	Layout.HorizontalAlignment = EUIAlignment::Start;
	Button->SetLayout( Layout );
	TSharedPtr<CUILabel> Caption = MakeLabel( Font, Text );
	FUILayoutProperties CaptionLayout = Caption->GetLayout();
	CaptionLayout.HorizontalAlignment = EUIAlignment::Center;
	Caption->SetLayout( CaptionLayout );
	Caption->SetHitTestable( false );
	Button->AddChild( Caption );
	return Button;
}

void CUIScreen::AskReset()
{
	// 確認のダイアログ: Yes で、既定へ戻す
	FUIDialogStyle DialogStyle;
	DialogStyle.Dim = FUIBrush::MakeSolid( FColor32( 0, 0, 0, 150 ) );
	DialogStyle.Window = Style.Window;
	DialogStyle.Button = Style.Button;
	DialogStyle.TitleFont = TitleFont;
	DialogStyle.BodyFont = BodyFont;
	DialogStyle.WindowSize = FVector2( 480.0f, 230.0f );
	TArray<FUIDialogButton> Buttons;
	Buttons.Add( FUIDialogButton { "Yes", 1 } );
	Buttons.Add( FUIDialogButton { "No", 2 } );
	TSharedPtr<CUIDialog> Dialog = MakeShared<CUIDialog>( DialogStyle, "Reset?", "The name, the volume and the check boxes go back to their defaults.", Buttons, 2, 2 );
	Dialog->OnResult.Add( [this]( CUIWidget&, int32 Id )
	{
		if ( Id == 1 )
		{
			ViewModel.ResetToDefaults();   // ビューモデルを替えれば、結んだ部品が、すべて追従する
		}
		else
		{
			SetStatus( "Reset was cancelled" );
		}
	} );
	NUIDialog::Show( Screens, Dialog );
}

void CUIScreen::SetStatus( const char* Text )
{
	ViewModel.Status.Set( Text );
}

TSharedPtr<CUIWidget> CUIScreen::BuildSettingsWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body, const TSharedPtr<CFont>& Title )
{
	// 設定の窓は、マークアップ（Assets/UI/Settings.gxui）に書いてある。使う名前だけ、ここで渡す
	MarkupContext.RegisterFont( "Body", Body );
	MarkupContext.RegisterFont( "Title", Title );
	MarkupContext.RegisterBrush( "Window", Style.Window );
	MarkupContext.RegisterBrush( "ProgressBack", Style.ProgressBack );
	MarkupContext.RegisterBrush( "ProgressFill", Style.ProgressFill );
	MarkupContext.RegisterButtonStyle( "Default", Style.Button );
	MarkupContext.RegisterCheckBoxStyle( "Default", Style.CheckBox );
	FUISliderStyle SliderStyle = Style.Slider;
	SliderStyle.TrackInset = FUIMargin( 0.0f, 8.0f, 0.0f, 8.0f );   // 溝を、矩形より細くする（矩形の高さ 28 に対して、溝は 12）
	MarkupContext.RegisterSliderStyle( "Thin", SliderStyle );
	FUITextBoxStyle NameStyle;
	NameStyle.Normal = Style.TextBox;
	NameStyle.Focused = Style.TextBoxFocused;
	NameStyle.Disabled = Style.TextBox;
	MarkupContext.RegisterTextBoxStyle( "Name", NameStyle );

	MarkupContext.RegisterHandler( "OkClicked", [this]() { SetStatus( "OK was clicked" ); } );
	MarkupContext.RegisterHandler( "CancelClicked", [this]() { SetStatus( "Cancel was clicked" ); } );
	MarkupContext.RegisterHandler( "NameSubmitted", [this]() { SetStatus( *FString::Printf( "Hello, %s!", *ViewModel.Name.Get() ) ); } );
	MarkupContext.RegisterHandler( "NameCancelled", [this]() { SetStatus( "Name edit was cancelled" ); } );

	MarkupContext.RegisterBool( "Music", ViewModel.Music );
	MarkupContext.RegisterBool( "Sound", ViewModel.Sound );
	MarkupContext.RegisterFloat( "Volume", ViewModel.Volume );
	MarkupContext.RegisterFloat( "VolumeRatio", ViewModel.VolumeRatio );
	MarkupContext.RegisterString( "VolumeText", ViewModel.VolumeText );
	MarkupContext.RegisterString( "Name", ViewModel.Name );
	MarkupContext.RegisterString( "Status", ViewModel.Status );
	MarkupContext.RegisterCommand( "Reset", ViewModel.Reset );

	if ( !SettingsDocument.Load( Assets, "UI/Settings.gxui", MarkupContext, SettingsBindings ) )
	{
		// 読めなかったら、理由を窓に出す
		TSharedPtr<CUIWidget> Failed = MakeShared<CUIWidget>();
		Failed->SetBackground( Style.Window );
		TSharedPtr<CUILabel> Message = MakeLabel( Body, *SettingsDocument.GetLastError(), FColor32( 255, 150, 150 ) );
		Message->SetMaxWidth( 480.0f );
		Failed->AddChild( Message );
		return Failed;
	}
	return SettingsDocument.GetRoot();
}

void CUIScreen::ReloadSettingsWindow()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr || !Root || !SettingsDocument.ReloadIfChanged( *Assets, MarkupContext, SettingsBindings ) )
	{
		return;
	}
	// 古い窓を外し、同じ場所へ、新しい窓を置く
	if ( SettingsWindow )
	{
		Root->RemoveChild( SettingsWindow.Get() );
	}
	SettingsWindow = SettingsDocument.GetRoot();
	PlaceAt( *Root, SettingsWindow, 60.0f, 90.0f, 560.0f, 530.0f );
}

TSharedPtr<CUIWidget> CUIScreen::BuildViewWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body )
{
	TSharedPtr<CUIWidget> Window = MakeShared<CUIWidget>();
	Window->SetBackground( Style.Panel );
	FUILayoutProperties WindowLayout;
	WindowLayout.Padding = FUIMargin( 16.0f );
	Window->SetLayout( WindowLayout );

	TSharedPtr<CUIStackPanel> Column = MakeStack( EUIOrientation::Vertical, 10.0f );
	Window->AddChild( Column );
	Column->AddChild( MakeLabel( Body, "Render target in a widget (click it)", FColor32( 255, 226, 150 ) ) );

	MinimapView = MakeShared<CUIRenderTargetView>();
	CAssetSubsystem* AssetsPtr = &Assets;
	MinimapView->SetTargetFactory( [this, AssetsPtr]( int32 Width, int32 Height ) -> TSharedPtr<IImage>
	{
		FRenderTargetDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		return AssetsPtr->CreateRenderTarget( FString::Printf( "UISample.View%d", CreatedTargets++ ), Desc );
	} );
	MinimapView->SetRenderFunction( [this]( FDrawContext& CommandList, int32 Width, int32 Height )
	{
		// 描画先の中の絵: 格子・周回する点・クリックした位置の印
		CommandList.Clear( FColor32( 18, 26, 42 ) );
		for ( int32 X = 0; X < Width; X += 40 )
		{
			CommandList.Box( X, 0, X, Height, FColor32( 40, 56, 84 ), false, 1 );
		}
		for ( int32 Y = 0; Y < Height; Y += 40 )
		{
			CommandList.Box( 0, Y, Width, Y, FColor32( 40, 56, 84 ), false, 1 );
		}
		const float32 CenterX = static_cast<float32>( Width ) * 0.5f;
		const float32 CenterY = static_cast<float32>( Height ) * 0.5f;
		const float32 Radius = static_cast<float32>( ( Width < Height ? Width : Height ) ) * 0.35f;
		CommandList.Circle( static_cast<int32>( CenterX ), static_cast<int32>( CenterY ), static_cast<int32>( Radius ), FColor32( 70, 110, 170 ), false, 2 );
		CommandList.Circle( static_cast<int32>( CenterX + std::cos( ElapsedTime ) * Radius ), static_cast<int32>( CenterY + std::sin( ElapsedTime ) * Radius ), 9, FColor32( 255, 190, 80 ), true );
		if ( Marker.x >= 0.0f )
		{
			CommandList.Circle( static_cast<int32>( Marker.x ), static_cast<int32>( Marker.y ), 12, FColor32( 120, 255, 160 ), false, 2 );
			CommandList.Circle( static_cast<int32>( Marker.x ), static_cast<int32>( Marker.y ), 3, FColor32( 120, 255, 160 ), true );
		}
	} );
	MinimapView->SetInteractive( true );
	MinimapView->OnViewPressed.Add( [this]( CUIWidget&, const FVector2& Position )
	{
		Marker = Position;
		SetStatus( *FString::Printf( "View clicked at (%d, %d) in target pixels", static_cast<int32>( Position.x ), static_cast<int32>( Position.y ) ) );
	} );
	MinimapView->OnViewDragged.Add( [this]( CUIWidget&, const FVector2& Position ) { Marker = Position; } );
	FUILayoutProperties ViewLayout;
	ViewLayout.FixedHeight = 230.0f;
	MinimapView->SetLayout( ViewLayout );
	Column->AddChild( MinimapView );
	return Window;
}

TSharedPtr<CUIWidget> CUIScreen::BuildIconWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body )
{
	TSharedPtr<CUIWidget> Window = MakeShared<CUIWidget>();
	Window->SetBackground( Style.Panel );
	FUILayoutProperties WindowLayout;
	WindowLayout.Padding = FUIMargin( 16.0f );
	Window->SetLayout( WindowLayout );

	TSharedPtr<CUIStackPanel> Column = MakeStack( EUIOrientation::Vertical, 10.0f );
	Window->AddChild( Column );
	Column->AddChild( MakeLabel( Body, "Icons (scroll)            10000-row list", FColor32( 255, 226, 150 ) ) );

	// 左: スクロール枠に入れたアイコン。右: 1 万行のリスト
	TSharedPtr<CUIStackPanel> Row = MakeStack( EUIOrientation::Horizontal, 16.0f );
	FUILayoutProperties RowLayout;
	RowLayout.FixedHeight = 214.0f;
	Row->SetLayout( RowLayout );
	Column->AddChild( Row );

	const FUIBrush BarTrack = FUIBrush::MakeSolid( FColor32( 255, 255, 255, 24 ), 4.0f );
	const FUIBrush BarThumb = FUIBrush::MakeSolid( FColor32( 170, 185, 220, 210 ), 4.0f );

	TSharedPtr<CUIScrollBox> Scroll = MakeShared<CUIScrollBox>();
	Scroll->SetScrollBar( BarTrack, BarThumb, 8.0f );
	FUILayoutProperties ScrollLayout;
	ScrollLayout.FixedWidth = 244.0f;
	Scroll->SetLayout( ScrollLayout );
	Row->AddChild( Scroll );

	TSharedPtr<CUIGridPanel> Grid = MakeShared<CUIGridPanel>();
	TArray<FUITrack> Columns;
	for ( int32 Index = 0; Index < IconColumns; ++Index )
	{
		Columns.Add( FUITrack::MakeFixed( IconCell ) );
	}
	TArray<FUITrack> Rows;
	const int32 NumIcons = static_cast<int32>( sizeof( IconNames ) / sizeof( IconNames[0] ) );
	for ( int32 Index = 0; Index < ( NumIcons + IconColumns - 1 ) / IconColumns; ++Index )
	{
		Rows.Add( FUITrack::MakeFixed( IconCell ) );
	}
	Grid->SetColumns( Columns );
	Grid->SetRows( Rows );
	for ( int32 Index = 0; Index < NumIcons; ++Index )
	{
		const TSharedPtr<CTexture> Texture = Assets.LoadTexture( FString( "Engine:/Icons/" ) + IconNames[Index] + ".png" );
		TSharedPtr<CUIImage> Icon = MakeShared<CUIImage>();
		if ( Texture )
		{
			Icon->SetImage( Texture, FVector2( static_cast<float32>( Texture->GetWidth() ), static_cast<float32>( Texture->GetHeight() ) ), EUIImageMode::Fit );
		}
		// 色は、ブラシの色で変えられる（アイコンは白一色）
		Icon->SetTint( FColor32( static_cast<uint8>( 150 + ( Index * 37 ) % 105 ), static_cast<uint8>( 170 + ( Index * 53 ) % 85 ), static_cast<uint8>( 200 + ( Index * 29 ) % 55 ) ) );
		FUILayoutProperties Layout;
		Layout.Column = Index % IconColumns;
		Layout.Row = Index / IconColumns;
		Layout.Margin = FUIMargin( 6.0f );
		Icon->SetLayout( Layout );
		Grid->AddChild( Icon );
	}
	Scroll->AddChild( Grid );

	NameList = MakeShared<CUIListView>();
	NameList->SetItemHeight( 28.0f );
	NameList->SetScrollBar( BarTrack, BarThumb, 8.0f );
	NameList->SetItemFactory(
		[Body, this]() -> TSharedPtr<CUIWidget >
		{
			TSharedPtr<CUILabel> Label = MakeLabel( Body, "" );
			FUILayoutProperties LabelLayout = Label->GetLayout();
			LabelLayout.Padding = FUIMargin( 10.0f, 0.0f );
			Label->SetLayout( LabelLayout );
			return Label;
		},
		[]( CUIWidget& Item, int32 Index, bool bSelected )
		{
			CUILabel& Label = static_cast<CUILabel&>( Item );
			Label.SetText( FString::Printf( "Item %d", Index + 1 ) );
			Label.SetColor( bSelected ? FColor32( 255, 255, 255 ) : FColor32( 190, 200, 220 ) );
			Label.SetBackground( bSelected ? FUIBrush::MakeSolid( FColor32( 60, 100, 170, 220 ), 4.0f ) : FUIBrush() );
		} );
	NameList->SetItemCount( NumListItems );
	NameList->OnItemClicked.Add( [this]( CUIWidget&, int32 Index ) { SetStatus( *FString::Printf( "Item %d was clicked", Index + 1 ) ); } );
	FUILayoutProperties ListLayout;
	ListLayout.FixedWidth = 244.0f;
	NameList->SetLayout( ListLayout );
	Row->AddChild( NameList );
	return Window;
}

void CUIScreen::Build()
{
	bBuilt = true;
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return;
	}
	FUIDefaultStyle::Load( *Assets, Style );
	BodyFont = Assets->GetDefaultFont( 22 );
	TitleFont = Assets->GetDefaultFont( 30 );
	const TSharedPtr<CFont> Body = BodyFont;
	const TSharedPtr<CFont> Title = TitleFont;

	Root = MakeShared<CUICanvasPanel>();
	Root->SetHitTestable( false );

	// 窓の影（窓より四方に大きく、先に描く）
	TSharedPtr<CUIWidget> Shadow = MakeShared<CUIWidget>();
	Shadow->SetBackground( Style.Shadow );
	Shadow->SetHitTestable( false );
	PlaceAt( *Root, Shadow, 40.0f, 74.0f, 600.0f, 590.0f );

	SettingsWindow = BuildSettingsWindow( *Assets, Body, Title );
	PlaceAt( *Root, SettingsWindow, 60.0f, 90.0f, 560.0f, 530.0f );
	PlaceAt( *Root, BuildViewWindow( *Assets, Body ), 680.0f, 90.0f, 560.0f, 300.0f );
	PlaceAt( *Root, BuildIconWindow( *Assets, Body ), 680.0f, 400.0f, 560.0f, 290.0f );

	// Reset の操作は、確認のダイアログを出す（ビューモデルは、UI を知らないので、出す仕事は、ここで渡す）
	ViewModel.OnResetRequested = [this]() { AskReset(); };

	// 画面のスタック: 下は、この画面（モーダルではなく、ゲームの入力も止めない）。ダイアログが、上に積まれる
	Screens.SetArea( FUIRect( 0.0f, 0.0f, ScreenWidth, ScreenHeight ) );
	FUIScreenOptions BaseOptions;
	BaseOptions.bModal = false;
	BaseOptions.bBlocksGameInput = false;
	BaseOptions.bPopOnCancel = false;
	const int32 BaseId = Screens.Push( Root, BaseOptions );
	if ( CUIContext* BaseContext = Screens.FindContext( BaseId ) )
	{
		BaseContext->OnCancel.Add( [this]() { SetStatus( "Cancel (Escape / B) was pressed" ); } );
	}
	// 画面の撮影・確認用: 起動の引数 --ask-reset で、最初から確認のダイアログを出す
	if ( NCommandLine::HasFlag( "ask-reset" ) )
	{
		AskReset();
	}
}

void CUIScreen::Tick( float32 DeltaTime )
{
	if ( !bBuilt )
	{
		Build();
	}
	if ( !Root )
	{
		return;
	}
	ElapsedTime += DeltaTime;
	// 設定の窓のマークアップが保存し直されたら、作り直す（半秒ごとに調べる）
	ReloadTimer += DeltaTime;
	if ( ReloadTimer >= 0.5f )
	{
		ReloadTimer = 0.0f;
		ReloadSettingsWindow();
	}
	// ミニマップは、動く内容なので、毎回描き直す（既定の更新の頻度）
	const IInput* Input = GetAppSubsystem<IInput>();
	const IGamepadInput* Gamepad = GetAppSubsystem<IGamepadInput>();
	ITextInput* TextInput = GetAppSubsystem<ITextInput>();
	const FUIInputFrame Frame = NUIInput::MakeFrame( Input, Input, Gamepad, FUIInputMapping(), TextInput );
	const FUIScreenInputResult Result = Screens.Update( Frame );
	// 入力欄にフォーカスがあるときだけ、文字入力（と、かな漢字変換）を受ける
	if ( TextInput != nullptr )
	{
		TextInput->SetTextInputActive( Result.bWantsTextInput );
	}
}

void CUIScreen::Draw( FDrawContext& CommandList )
{
	if ( Root )
	{
		Screens.Paint( CommandList );
	}
}
