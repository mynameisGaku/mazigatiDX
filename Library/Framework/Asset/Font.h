#pragma once
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Render/DrawContext.h"
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

private:
	int32 Handle;
	FFontDesc Desc;
	FString FontFilePath;
};
