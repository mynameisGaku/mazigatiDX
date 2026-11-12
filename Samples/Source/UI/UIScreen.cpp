// SPDX-License-Identifier: MIT
#include "UIScreen.h"

#include <cmath>

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Input/ITextInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/UI/Core/UIInputBridge.h"
#include "Library/Platform/System/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

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
	constexpr float32 SettingsTop = 84.0f;
	constexpr float32 SettingsHeight = 620.0f;

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
	// 少し小さいところから広がって出て、縮んで消える
	FUIScreenTransition Transition;
	Transition.Kind = EUIScreenTransitionKind::Scale;
	Transition.Duration = 0.18f;
	Transition.Easing = EEasing::CubicOut;
	NUIDialog::Show( Screens, Dialog, Transition );
}

void CUIScreen::SetStatus( const char* Text )
{
	ViewModel.Status.Set( Text );
}

TSharedPtr<CUIWidget> CUIScreen::BuildSettingsWindow( CAssetSubsystem& Assets, const TSharedPtr<CFont>& Body, const TSharedPtr<CFont>& Title )
{
	// 見た目は、テーマ（名前つきの色・ブラシ・フォント・スタイル）にまとめてある。基本のテーマ（画像とフォント）はコードで作り、
	// その子のテーマは、ファイル（Assets/UI/Themes/*.gxuitheme）から読む: Common（どのテーマでも同じスタイル）→ Blue・Green・Rose（色などを上書き）
	const TSharedPtr<FUITheme> Base = Style.MakeTheme( Body, Title );
	ThemeDocuments.Reserve( 4 );
	TSharedPtr<FUITheme> Common = Base;
	ThemeDocuments.AddDefaulted();
	if ( ThemeDocuments.Last().Load( Assets, "UI/Themes/Common.gxuitheme", Base ) )
	{
		Common = ThemeDocuments.Last().GetTheme();
	}
	else
	{
		LOG( LogSamples, Warning, "the UI theme could not be loaded: %s", *ThemeDocuments.Last().GetLastError() );
	}
	Themes.Add( Common );
	const char* ThemeFiles[] = { "UI/Themes/Blue.gxuitheme", "UI/Themes/Green.gxuitheme", "UI/Themes/Rose.gxuitheme" };
	for ( const char* ThemeFile : ThemeFiles )
	{
		// 上書きした名前だけ違い、あとは共通のテーマと同じ（読めなかったテーマは、飛ばす）
		ThemeDocuments.AddDefaulted();
		if ( ThemeDocuments.Last().Load( Assets, ThemeFile, Common ) )
		{
			Themes.Add( ThemeDocuments.Last().GetTheme() );
		}
		else
		{
			LOG( LogSamples, Warning, "the UI theme could not be loaded: %s", *ThemeDocuments.Last().GetLastError() );
		}
	}
	MarkupContext.SetPopupHost( &Screens );
	MarkupContext.SetAnimator( &Animator );
	MarkupContext.SetTheme( Themes[0] );

	// マークアップの {Binding ...} と、イベントの受け手は、ここで渡す
	MarkupContext.RegisterHandler( "OkClicked", [this]() { SetStatus( "OK was clicked" ); } );
	MarkupContext.RegisterHandler( "CancelClicked", [this]() { SetStatus( "Cancel was clicked" ); } );
	MarkupContext.RegisterHandler( "NameSubmitted", [this]() { SetStatus( *FString::Printf( "Hello, %s!", *ViewModel.Name.Get() ) ); } );
	MarkupContext.RegisterHandler( "NameCancelled", [this]() { SetStatus( "Name edit was cancelled" ); } );
	// ボタンの処理の最中に、そのボタンを含む窓を作り直すと危ないので、印だけ付けて、次の Tick で替える
	MarkupContext.RegisterHandler( "NextTheme", [this]() { bThemeChangePending = true; } );

	MarkupContext.RegisterBool( "Music", ViewModel.Music );
	MarkupContext.RegisterInt( "Quality", ViewModel.Quality );
	MarkupContext.RegisterStringList( "QualityItems", ViewModel.QualityItems );
	MarkupContext.RegisterBool( "Sound", ViewModel.Sound );
	MarkupContext.RegisterFloat( "Volume", ViewModel.Volume );
	MarkupContext.RegisterFloat( "VolumeRatio", ViewModel.VolumeRatio );
	MarkupContext.RegisterString( "VolumeText", ViewModel.VolumeText );
	MarkupContext.RegisterString( "Name", ViewModel.Name );
	MarkupContext.RegisterString( "Memo", ViewModel.Memo );
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

void CUIScreen::ReplaceSettingsWindow()
{
	// 古い窓を外し、同じ場所へ、新しい窓を置く
	if ( SettingsWindow )
	{
		Root->RemoveChild( SettingsWindow.Get() );
	}
	SettingsWindow = SettingsDocument.GetRoot();
	PlaceAt( *Root, SettingsWindow, 60.0f, SettingsTop, 560.0f, SettingsHeight );
}

void CUIScreen::ReloadSettingsWindow()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr || !Root )
	{
		return;
	}
	// テーマのファイルが保存し直されたら、読み直す。作ってある窓は、ブラシや色を写して持っているので、作り直す
	bool bThemeChanged = false;
	for ( CUIThemeDocument& ThemeDocument : ThemeDocuments )
	{
		const FString OldError = ThemeDocument.GetLastError();
		if ( ThemeDocument.ReloadIfChanged( *Assets ) )
		{
			bThemeChanged = true;
		}
		else if ( !ThemeDocument.GetLastError().IsEmpty() && ThemeDocument.GetLastError() != OldError )
		{
			LOG( LogSamples, Warning, "the UI theme could not be loaded again: %s", *ThemeDocument.GetLastError() );
		}
	}
	if ( SettingsDocument.ReloadIfChanged( *Assets, MarkupContext, SettingsBindings ) || ( bThemeChanged && SettingsDocument.Rebuild( *Assets, MarkupContext, SettingsBindings ) ) )
	{
		ReplaceSettingsWindow();
	}
}

void CUIScreen::NextTheme()
{
	// テーマを替えて、同じマークアップを作り直す（色もスタイルも、名前でテーマから引かれる）
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr || !Root || Themes.IsEmpty() )
	{
		return;
	}
	ThemeIndex = ( ThemeIndex + 1 ) % Themes.Num();
	MarkupContext.SetTheme( Themes[ThemeIndex] );
	if ( SettingsDocument.Rebuild( *Assets, MarkupContext, SettingsBindings ) )
	{
		ReplaceSettingsWindow();
	}
	SetStatus( *FString::Printf( "Theme: %s", *Themes[ThemeIndex]->GetName() ) );
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
	// 10 行ごとに、少し高い行（行ごとに、高さが違ってもよい）
	NameList->SetItemSizeProvider( []( int32 Index ) { return Index % 10 == 0 ? 44.0f : 28.0f; } );
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
	PlaceAt( *Root, Shadow, 40.0f, SettingsTop - 16.0f, 600.0f, SettingsHeight + 60.0f );

	SettingsWindow = BuildSettingsWindow( *Assets, Body, Title );
	PlaceAt( *Root, SettingsWindow, 60.0f, SettingsTop, 560.0f, SettingsHeight );
	PlaceAt( *Root, BuildViewWindow( *Assets, Body ), 680.0f, SettingsTop, 560.0f, 300.0f );
	PlaceAt( *Root, BuildIconWindow( *Assets, Body ), 680.0f, SettingsTop + 310.0f, 560.0f, SettingsHeight - 310.0f );

	// Reset の操作は、確認のダイアログを出す（ビューモデルは、UI を知らないので、出す仕事は、ここで渡す）
	ViewModel.OnResetRequested = [this]() { AskReset(); };

	// 画面のスタック: 下は、この画面（モーダルではなく、ゲームの入力も止めない）。ダイアログが、上に積まれる
	Screens.SetArea( FUIRect( 0.0f, 0.0f, ScreenWidth, ScreenHeight ) );
	// ツールチップ（部品の Tooltip）の見た目
	FUITooltipStyle TooltipStyle;
	TooltipStyle.Background = Style.Tooltip;
	TooltipStyle.Font = Assets->GetDefaultFont( 18 );
	TooltipStyle.Padding = FUIMargin( 12.0f, 8.0f );
	Screens.SetTooltipStyle( TooltipStyle );
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
	// 同じく --memo-demo で、メモの欄に、欄より多い行を入れておく（複数行の表示の確認）
	if ( NCommandLine::HasFlag( "memo-demo" ) )
	{
		ViewModel.Memo.Set( "Buy milk\nCall Hana at 3 pm\nFinish the level" );
	}
	// 同じく --open-dropdown で、画質のドロップダウンを開いた状態にする（レイアウトが決まってから開くので、最初の更新のあと）
	bOpenDropdownPending = NCommandLine::HasFlag( "open-dropdown" );
	// 同じく --theme-steps=N で、テーマを N 回進めた状態から始める（Theme ボタンを N 回押した状態）
	if ( const TOptional<FString> Steps = NCommandLine::GetValue( "theme-steps" ) )
	{
		const int32 Count = static_cast<int32>( Steps.GetValue().ToFloat() );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			NextTheme();
		}
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
	// 設定の窓のマークアップか、テーマのファイルが保存し直されたら、作り直す（半秒ごとに調べる）
	if ( bThemeChangePending )
	{
		bThemeChangePending = false;
		NextTheme();
	}
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
	FUIInputFrame Frame = NUIInput::MakeFrame( Input, Input, Gamepad, FUIInputMapping(), TextInput, GetAppSubsystem<IClipboard>() );
	Frame.DeltaTime = DeltaTime;   // ツールチップの待ち時間を数えるため
	const FUIScreenInputResult Result = Screens.Update( Frame );
	// 部品の動きを進める（入力のあと。このフレームのホバー・押下が、すぐ動きに出る）
	Animator.Update( DeltaTime );
	if ( bOpenDropdownPending && SettingsWindow )
	{
		bOpenDropdownPending = false;
		if ( CUIDropdown* Dropdown = static_cast<CUIDropdown*>( SettingsWindow->FindByName( "QualityBox" ) ) )
		{
			Dropdown->Open();
		}
	}
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
