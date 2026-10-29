#include "Dialogue.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 窓の位置と大きさ（画面の座標。ピクセル）。見せる範囲の下に置く
	constexpr int32 BoxLeft = 80;
	constexpr int32 BoxRight = FVillageState::ViewWidth - 80;
	constexpr int32 BoxHeight = 150;
	constexpr int32 BoxBottom = FVillageState::ViewTop + FVillageState::ViewHeight - 16;
	constexpr int32 BoxTop = BoxBottom - BoxHeight;

	// 窓の縁と文字の間の余白、名前の札の大きさ（ピクセル）
	constexpr int32 Padding = 24;
	constexpr int32 NameTabWidth = 170;
	constexpr int32 NameTabHeight = 34;

	// 文の最大の行数、1 秒に出す文字数
	constexpr int32 MaxLines = 3;
	constexpr float32 CharsPerSecond = 30.0f;

	// 文字列の先頭から、NumChars 文字（UTF-8 の文字の数）だけの部分
	FString PrefixChars( const FString& Text, int32 NumChars )
	{
		int32 Offset = 0;
		int32 Count = 0;
		while ( Offset < Text.Len() && Count < NumChars )
		{
			NTextLayout::DecodeUtf8( *Text, Text.Len(), Offset );
			++Count;
		}
		return Text.Left( Offset );
	}
}

CDialogue::CDialogue( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CFont>& InBodyFont, const TSharedPtr<CFont>& InNameFont )
	: State( InState )
	, BodyFont( InBodyFont )
	, NameFont( InNameFont )
{
	// 最初に、はじまりの説明を出す（コンストラクターは、メインスレッドで呼ばれる）
	OpenIntroduction();
}

void CDialogue::OpenIntroduction()
{
	TArray<FString> Intro;
	Intro.Add( "ようこそ、旅人さん。ここは小さな村です。道の上に落ちている金貨を、ぜんぶ集めてみてください。" );
	Intro.Add( "夜になると、ランタンと街灯の明かりだけが頼りになります。N キーで、昼と夜を切り替えられますよ。" );
	Intro.Add( "移動は WASD か矢印キー、走るときは Shift です。私に話しかけるときは、そばで Enter を押してください。" );
	Open( "村の長老", Intro );
}

void CDialogue::OpenVillagerTalk()
{
	const int32 Total = State.IsValid() ? State->CoinPositions.Num() : 0;
	const int32 Coins = State.IsValid() ? State->Coins : 0;
	TArray<FString> Talk;
	Talk.Add( FString::Printf( "金貨は、いま %d 枚 / %d 枚。%s", Coins, Total, Coins >= Total ? "ぜんぶ集めましたね。すばらしい！" : "まだ、道のどこかに落ちていますよ。" ) );
	Talk.Add( "池のまわりは、水があって通れません。道をたどっていけば、迷いませんよ。" );
	Open( "村の長老", Talk );
}

void CDialogue::Open( const FString& InSpeaker, const TArray<FString>& InPages )
{
	Speaker = InSpeaker;
	Pages = InPages;
	PageIndex = 0;
	bOpen = Pages.Num() > 0;
	if ( State.IsValid() )
	{
		State->bDialogueOpen = bOpen;
	}
	StartPage();
}

void CDialogue::StartPage()
{
	Layout = FTextLayout();
	TotalChars = 0;
	RevealedChars = 0.0f;
	if ( !bOpen || !BodyFont.IsValid() || !Pages.IsValidIndex( PageIndex ) )
	{
		return;
	}
	// 窓の幅に収まるように折り返す（文字を測るので、文が変わるときに 1 度だけ）
	FTextLayoutOptions Options;
	Options.MaxWidth = BoxRight - BoxLeft - Padding * 2;
	Options.MaxLines = MaxLines;
	Layout = BodyFont->Layout( *Pages[PageIndex], Options );
	for ( int32 Index = 0; Index < Layout.Lines.Num(); ++Index )
	{
		TotalChars += Layout.GetLineText( Index ).CharCount();
	}
}

void CDialogue::Close()
{
	bOpen = false;
	if ( State.IsValid() )
	{
		State->bDialogueOpen = false;
		State->DialogueClosedTime = State->Time;
	}
}

void CDialogue::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}

	// 村人が話しかけた
	if ( State->bTalkRequested )
	{
		State->bTalkRequested = false;
		if ( !bOpen )
		{
			OpenVillagerTalk();
		}
	}
	if ( !bOpen )
	{
		return;
	}

	// 1 文字ずつ出す
	RevealedChars = NMath::Min( static_cast<float32>( TotalChars ), RevealedChars + CharsPerSecond * DeltaTime );

	// Enter（か Space）: 全部を出す → 次の文 → 閉じる
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && ( Input->IsKeyPressed( EKey::Enter ) || Input->IsKeyPressed( EKey::Space ) ) )
	{
		if ( RevealedChars < static_cast<float32>( TotalChars ) )
		{
			RevealedChars = static_cast<float32>( TotalChars );
		}
		else if ( PageIndex + 1 < Pages.Num() )
		{
			++PageIndex;
			StartPage();
		}
		else
		{
			Close();
		}
	}
}

void CDialogue::Draw( FDrawContext& CommandList )
{
	if ( !bOpen || !BodyFont.IsValid() || !NameFont.IsValid() )
	{
		return;
	}

	// 窓（少し透けた、暗い青の角丸の箱と、白い縁）と、話す人の名前の札
	CommandList.Blend( EDrawBlend::Alpha, 225 );
	CommandList.RoundBox( BoxLeft, BoxTop, BoxRight, BoxBottom, 14, 14, FColor32( 18, 22, 50 ) );
	CommandList.RoundBox( BoxLeft + 16, BoxTop - NameTabHeight + 8, BoxLeft + 16 + NameTabWidth, BoxTop + 8, 10, 10, FColor32( 52, 64, 140 ) );
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.RoundBox( BoxLeft, BoxTop, BoxRight, BoxBottom, 14, 14, FColor32( 224, 228, 255 ), false );
	NameFont->Record( CommandList, BoxLeft + 16 + 16, BoxTop - NameTabHeight + 12, *Speaker, FColor32::White );

	// 本文: ここまでに出した文字だけを、行ごとに描く（影つき）
	FTextStyle Style;
	Style.bShadow = true;
	Style.ShadowColor = FColor32( 0, 0, 20 );
	Style.ShadowOffsetX = 1.0f;
	Style.ShadowOffsetY = 1.0f;
	const FFontMetrics& Metrics = BodyFont->GetMetrics();
	int32 Remaining = static_cast<int32>( RevealedChars );
	float32 Y = static_cast<float32>( BoxTop + Padding );
	for ( int32 Index = 0; Index < Layout.Lines.Num() && Remaining > 0; ++Index )
	{
		const FString Line = Layout.GetLineText( Index );
		const FString Visible = PrefixChars( Line, Remaining );
		Remaining -= Line.CharCount();
		BodyFont->RecordStyled( CommandList, static_cast<float32>( BoxLeft + Padding ), Y, *Visible, FColor32::White, Style );
		Y += static_cast<float32>( Metrics.LineHeight );
	}

	// 全部を出したら、「次へ」の印（点滅する三角形）
	if ( RevealedChars >= static_cast<float32>( TotalChars ) && ( static_cast<int32>( State->Time * 2.0f ) % 2 ) == 0 )
	{
		const int32 X = BoxRight - Padding - 12;
		const int32 MarkY = BoxBottom - Padding;
		CommandList.Triangle( X, MarkY, X + 16, MarkY, X + 8, MarkY + 10, FColor32( 255, 226, 110 ) );
	}
}
