#pragma once
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Text/TextLayout.h"
#include "Library/Math/Color.h"

//=============================================================================
// CFont
//   フォント（特定のフォント名・大きさ・太さなどで作った文字の描画設定）。CAssetManager で読み込む。
//
//   TSharedPtr<CFont> Title = Assets->LoadFont( "Fonts/NotoSansJP-Bold.ttf", 48 );          // フォントファイル
//   TSharedPtr<CFont> Body = Assets->LoadSystemFont( "Meiryo", 20 );                         // 入っているフォント名
//   TSharedPtr<CFont> Debug = Assets->GetDefaultFont( 16 );                                  // 実装の既定フォント
//
//   // メインスレッド（Draw / OnDraw）ですぐに描く
//   Title->Draw( 640, 100, "Game Title", FColor32::White, EDrawTextAlign::Center );
//
//   // 描画システムの中では記録する
//   Title->Record( Draw, 640, 100, "Game Title", FColor32::White, EDrawTextAlign::Center );
//
//   縁取りは EdgeSize を指定したフォントで有効（Draw の EdgeColor が縁の色）。
//   文字コードは FString と同じ（UTF-8）。
//
//   測る・折り返す・飾る:
//   const FFontMetrics Metrics = Body->GetMetrics();                              // 大きさ・行の送り幅・ベースラインなど
//   const FTextSize Size = Body->MeasureSize( "Hello\nWorld" );                  // 幅・高さ・行数
//   Body->DrawWrapped( 40, 40, 300, "長い文章を、幅 300 に収まるように折り返す", FColor32::White );   // 幅 300 の箱に収めて描く
//   FTextStyle Style;  Style.AngleRadians = 0.5f;                                 // 回転・拡大・縦書き・影
//   Body->DrawStyled( 320.0f, 200.0f, "Rotated", FColor32::White, Style );
//=============================================================================
class CFont : public CAsset
{
public:
	CFont( const FString& InKey, int32 InHandle, const FFontDesc& InDesc, const FString& InFontFilePath );
	~CFont() override;

	FORCEINLINE int32 GetHandle() const { return Handle; }
	// 作ったときの指定（Face はフォントファイルから読み取った名前）
	FORCEINLINE const FFontDesc& GetDesc() const { return Desc; }
	FORCEINLINE int32 GetSize() const { return Desc.Size; }
	// このフォントを提供しているフォントファイル（フォント名で読み込んだものは空）
	FORCEINLINE const FString& GetFontFilePath() const { return FontFilePath; }

	FString Describe() const override;
	// 文字の絵のキャッシュの推定（256 文字ぶん、1 文字 (大きさ + 縁取り x 2) の 2 乗 x 4 バイト）
	int64 GetMemoryBytes() const override;

	// 文字列の描画幅（ピクセル）。メインスレッドで呼ぶ
	int32 MeasureWidth( const char* Text ) const;
	FORCEINLINE int32 MeasureWidth( const FString& Text ) const { return MeasureWidth( *Text ); }

	// 描画システムの中で使う。(X, Y) は文字列の左上（Align によって X が左端 / 中央 / 右端）
	void Record( FDrawContext& Draw, int32 X, int32 Y, const char* Text, const FColor32& Color,
				 EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black ) const;

	// いますぐ描く（メインスレッドの Draw / OnDraw で）。IDrawExecutor を通して描くので、描画システムの出力と同じ見た目になる
	void Draw( int32 X, int32 Y, const char* Text, const FColor32& Color,
			   EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black ) const;
	void DrawF( int32 X, int32 Y, const FColor32& Color, _Printf_format_string_ const char* Format, ... ) const;

	//-------------------------------------------------------------------------
	// 測る（メインスレッドで呼ぶ）
	//-------------------------------------------------------------------------
	// フォントの寸法（大きさ・1 行の文字の高さ・行の送り幅・ベースラインまでの高さ・文字の最大の幅・縁取り・字間）。
	// 作るときに測って持っている値なので、描画システムの中（メインスレッド以外）から読んでよい。測れなかったフォントは、すべて 0
	const FFontMetrics& GetMetrics() const { return Metrics; }
	// 行の送り幅（次の行の同じ位置までの距離。ピクセル。GetMetrics().LineHeight）
	int32 GetLineHeight() const { return Metrics.LineHeight; }
	// 作るときに測った寸法を渡す（CAssetManager が呼ぶ）
	void SetMetrics( const FFontMetrics& InMetrics ) { Metrics = InMetrics; }
	// 文字列を描いたときの大きさ（幅・高さ・行数。改行で複数行。末尾の改行のあとも 1 行。空の文字列は 0）。取れなければ、すべて 0
	FTextSize MeasureSize( const char* Text ) const;
	// 文字 1 つずつの位置と幅（改行も 1 つ。壊れた UTF-8 は、先に正しい形にしておく）。取れなければ false
	bool GetGlyphs( const char* Text, TArray<FTextGlyph>& OutGlyphs ) const;

	//-------------------------------------------------------------------------
	// 折り返し（描画システムの中で使うのは Record 系、メインスレッドですぐ描くのは Draw 系）
	//   Layout と RecordWrapped は、文字を測る（DxLib を呼ぶ）ので、メインスレッドで呼ぶ。描画システムの中で使うときは、Tick などで Layout を作っておいて、
	//   Draw の中では RecordLayout を使う（RecordLayout は、測らず、記録するだけ）
	//-------------------------------------------------------------------------
	// 文字列を、Options の通りに行に分ける（FTextLayout を参照）。行の位置・描く位置の計算に使える（メインスレッドで呼ぶ）
	FTextLayout Layout( const char* Text, const FTextLayoutOptions& Options ) const;
	// 配置した結果 Layout を、左上 (X, Y) から、幅 BoxWidth の箱の中に描く。Align で、各行を箱の左端・中央・右端に揃える。空の行は何も描かない
	void RecordLayout( FDrawContext& Draw, const FTextLayout& Layout, int32 X, int32 Y, int32 BoxWidth, const FColor32& Color,
					   EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black ) const;
	// 文字列を、幅 MaxWidth に収まるように折り返して、左上 (X, Y) から描く（Layout + RecordLayout）。MaxLines が 0 でなければ、その行数で切る
	void RecordWrapped( FDrawContext& Draw, int32 X, int32 Y, int32 MaxWidth, const char* Text, const FColor32& Color,
						EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black, int32 MaxLines = 0 ) const;
	void DrawWrapped( int32 X, int32 Y, int32 MaxWidth, const char* Text, const FColor32& Color,
					  EDrawTextAlign Align = EDrawTextAlign::Left, const FColor32& EdgeColor = FColor32::Black, int32 MaxLines = 0 ) const;

	//-------------------------------------------------------------------------
	// 飾り（拡大・回転・縦書き・影。FTextStyle を参照）
	//-------------------------------------------------------------------------
	// (X, Y) に、文字列の基準点（Style.AnchorX / AnchorY）が来るように描く。値が使えないとき（FDrawContext::TextStyled を参照）は、何も記録せず false
	bool RecordStyled( FDrawContext& Draw, float32 X, float32 Y, const char* Text, const FColor32& Color, const FTextStyle& Style = FTextStyle() ) const;
	void DrawStyled( float32 X, float32 Y, const char* Text, const FColor32& Color, const FTextStyle& Style = FTextStyle() ) const;

	//-------------------------------------------------------------------------
	// 文字の代わりの画像（アイコンなどを、文字列の中に入れる）
	//-------------------------------------------------------------------------
	// Character（UTF-8 の 1 文字）の代わりに、画像を描く。Advance は次の文字までの幅（ピクセル）、(DrawX, DrawY) は画像を描く位置のずれ。登録できたら true。
	// この版は、画像の持ち主が、このフォントで描くあいだ、手放さないこと（解放された画像を指すと、何も描かれない）
	bool AddCharImage( const char* Character, const IImage& Image, int32 Advance, int32 DrawX = 0, int32 DrawY = 0 );
	// 画像（CTexture など）を、フォントが持つ版（登録を外すか、フォントが消えるまで、画像を手放さない。こちらを使えば、寿命を気にしなくてよい）
	bool AddCharImage( const char* Character, const TSharedPtr<CTexture>& Image, int32 Advance, int32 DrawX = 0, int32 DrawY = 0 );
	// 登録を外す（元の文字が描かれる）。フォントが持っていた画像も手放す
	void RemoveCharImage( const char* Character );

private:
	int32 Handle;
	FFontDesc Desc;
	FString FontFilePath;
	FFontMetrics Metrics;
	// AddCharImage（TSharedPtr の版）で渡された画像（文字 -> 画像）
	TMap<FString, TSharedPtr<CTexture>> OwnedCharImages;
};
