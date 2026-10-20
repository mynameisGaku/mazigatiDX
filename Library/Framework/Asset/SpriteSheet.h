#pragma once
#include <initializer_list>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// スプライトシート
//   1 枚の画像に並んだコマ（フレーム）と、コマの並べ方（アニメーションのクリップ）の定義。
//   描画は FSpriteAnimator（コマの切り替え）か、CSpriteSheet のコマの矩形を FDrawContext::SpriteRect に渡して行う。
//
//   // 1. 格子状に並んだ画像: 横 8 分割 x 縦 4 分割、1 コマ 64 x 64 ピクセル
//   FSpriteSheetGrid Grid;
//   Grid.Columns = 8;  Grid.Rows = 4;
//   Grid.CellWidth = 64;  Grid.CellHeight = 64;
//   TSharedPtr<CSpriteSheet> Sheet = Assets->CreateSpriteSheet( Assets->LoadTexture( "Sprites/Hero.png" ), Grid );
//
//   // 2. クリップ（コマの並べ方）を足す
//   Sheet->AddClip( FSpriteClip::FromRange( "Idle", 0, 3, 6.0f ) );                 // 0〜3 番のコマを毎秒 6 コマで繰り返す
//   Sheet->AddClip( FSpriteClip::FromRange( "Run", 8, 15, 14.0f ) );
//   Sheet->AddClip( FSpriteClip::FromRange( "Jump", 16, 19, 10.0f, EAnimLoopMode::Once ) );
//
//   // 3. または、定義ファイル（JSON）から。画像・分割・クリップがまとめて読み込まれる
//   TSharedPtr<CSpriteSheet> Sheet = Assets->LoadSpriteSheet( "Sprites/Hero.sheet.json" );
//
//   定義ファイルは、スプライトシートエディタ（デバッグ画面の Tools）で、画像の上に分割を重ねて確かめながら作って保存できる。
//   定義の中身は FSpriteSheetDefinition（コードからも作れて、JSON と相互に変換できる）。
//
//   コマの番号は、格子（定義した順）のコマ、続けて自由な矩形（Frames）の順。格子を足すと、あとの番号がずれるので、
//   格子を先に作ってから AddFrame する。
//=============================================================================

// 並べ方（コマの番号を 0 から振る順番）
enum class ESpriteGridOrder : uint8
{
	RowMajor,      // 左から右へ、上の段から下へ
	ColumnMajor,   // 上から下へ、左の列から右へ
};

//-----------------------------------------------------------------------------
// FSpriteSheetGrid
//   格子状のスプライトシートの定義。
//   CellWidth / CellHeight が 0 のときは、画像の大きさ・余白・隙間・分割数から求める（画像が読み込まれてから）。
//-----------------------------------------------------------------------------
struct FSpriteSheetGrid
{
	int32 Columns = 1;      // 横の分割数
	int32 Rows = 1;         // 縦の分割数
	int32 CellWidth = 0;    // 1 コマの幅（ピクセル）。0 なら自動
	int32 CellHeight = 0;   // 1 コマの高さ（ピクセル）。0 なら自動
	int32 NumFrames = 0;    // 使うコマの数。0 なら Columns * Rows（最後の段が欠けているときに指定する）
	int32 MarginX = 0;      // 画像の左右の余白（ピクセル）
	int32 MarginY = 0;      // 画像の上下の余白（ピクセル）
	int32 SpacingX = 0;     // コマとコマの間の隙間（ピクセル、横）
	int32 SpacingY = 0;     // コマとコマの間の隙間（ピクセル、縦）
	ESpriteGridOrder Order = ESpriteGridOrder::RowMajor;
	// 基準点（コマの左上が原点、ピクセル）。描くとき、この点が表示位置に来る。指定しなければコマの中央
	bool bCustomPivot = false;
	float32 PivotX = 0.0f;
	float32 PivotY = 0.0f;

	// この格子が作るコマの数
	int32 GetNumFrames() const;
};

// 画像の中の 1 コマ
struct FSpriteFrame
{
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;
	float32 PivotX = 0.0f;   // 基準点（コマの左上が原点、ピクセル）
	float32 PivotY = 0.0f;
};

// クリップの途中で知らせる出来事（足音のタイミングなど）。Step は、そのクリップの再生順で何コマ目に入ったときか（0 から）
struct FSpriteAnimEvent
{
	int32 Step = 0;
	FString Name;
};

//-----------------------------------------------------------------------------
// FSpriteClip
//   コマの並べ方。Frames に、スプライトシートのコマの番号を再生する順に並べる（同じコマを何度入れてもよい）。
//-----------------------------------------------------------------------------
struct FSpriteClip
{
	FString Name;
	TArray<int32> Frames;
	// コマごとの表示時間（秒）。空なら全部 1 / Fps。足りない分は 1 / Fps
	TArray<float32> Durations;
	float32 Fps = 12.0f;
	EAnimLoopMode Loop = EAnimLoopMode::Loop;
	TArray<FSpriteAnimEvent> Events;

	// First 番から Last 番までのコマ（両端を含む。First > Last なら逆順）
	static FSpriteClip FromRange( const FString& InName, int32 First, int32 Last, float32 InFps = 12.0f, EAnimLoopMode InLoop = EAnimLoopMode::Loop );
	// 番号の並びから
	static FSpriteClip FromFrames( const FString& InName, std::initializer_list<int32> InFrames, float32 InFps = 12.0f, EAnimLoopMode InLoop = EAnimLoopMode::Loop );

	// Step 番目のコマだけ表示時間を変える（秒）
	FSpriteClip& SetDuration( int32 Step, float32 Seconds );
	// Step 番目のコマに入ったときの出来事を足す
	FSpriteClip& AddEvent( int32 Step, const FString& EventName );

	// Step 番目のコマの表示時間（秒）
	float32 GetDuration( int32 Step ) const;
	// 全部のコマを再生するのにかかる時間（秒。1 周。PingPong の復路は含まない）
	float32 GetTotalDuration() const;
	// Durations が全部同じ値（1 / Fps）なら、空に戻す（保存ファイルを小さく保つ）
	void NormalizeDurations();
};

//-----------------------------------------------------------------------------
// FSpriteSheetDefinition
//   スプライトシートの定義（JSON と相互に変換できる）。書式は SpriteSheet.cpp の先頭のコメントを参照。
//-----------------------------------------------------------------------------
struct FSpriteSheetDefinition
{
	FString TexturePath;               // アセットのルートからのパス
	TArray<FSpriteSheetGrid> Grids;    // 格子状のコマ（この順に番号が振られる）
	TArray<FSpriteFrame> Frames;       // 格子のコマの後ろに足す、自由な矩形
	TArray<FSpriteClip> Clips;

	int32 GetNumGridFrames() const;
	FORCEINLINE int32 GetNumFrames() const { return GetNumGridFrames() + Frames.Num(); }

	NlohmannJson ToJson() const;
	// JSON から。オブジェクトでなければ false（足りない項目は既定値になる）
	static bool FromJson( const NlohmannJson& Json, FSpriteSheetDefinition& OutDefinition );
};

//-----------------------------------------------------------------------------
// CSpriteSheet
//-----------------------------------------------------------------------------
class CSpriteSheet : public CAsset
{
public:
	CSpriteSheet( const FString& InName, const TSharedPtr<CTexture>& InTexture );

	FORCEINLINE const TSharedPtr<CTexture>& GetTexture() const { return Texture; }
	// 画像のハンドル（画像が無ければ -1）
	FORCEINLINE int32 GetTextureHandle() const { return Texture.IsValid() ? Texture->GetHandle() : -1; }
	// 画像を差し替える（エディタで別の画像に変えるとき）。コマ割りは作り直される。
	// DefinitionTexturePath は、定義（保存されるファイル）に書くパス（アセットのルートからの相対）。空なら画像のパスのまま
	void SetTexture( const TSharedPtr<CTexture>& InTexture, const FString& DefinitionTexturePath = FString() );

	// 画像もコマ割りも使える状態か。画像の読み込みを待っていた格子があれば、読み込めていればここで作る
	bool IsReady() const override;
	FString Describe() const override;
	int64 GetMemoryBytes() const override;
	// 画像の読み込みを待っていたコマ割りを、読み込めていれば作る（メインスレッドで。CAssetManager が毎フレーム呼ぶ）。
	// 描画システムなど別のスレッドから呼ぶ GetFrame は、これを呼ばない（読むだけ）
	void Prepare() const;

	//-------------------------------------------------------------------------
	// 定義
	//-------------------------------------------------------------------------
	FORCEINLINE const FSpriteSheetDefinition& GetDefinition() const { return Definition; }
	// 定義を丸ごと入れ替える（コマ割りとクリップを作り直す）。TexturePath も定義のものになる（画像そのものは変わらない）
	void SetDefinition( const FSpriteSheetDefinition& InDefinition );

	//-------------------------------------------------------------------------
	// コマ
	//-------------------------------------------------------------------------
	// 格子状にコマを作る（いまある格子の後ろに足す）。画像の大きさが必要なのに未読み込みなら、読み込み後に作る。
	// 範囲が画像からはみ出す場合は警告して、そのコマは空（描いても何も出ない）にする
	void BuildGrid( const FSpriteSheetGrid& Grid );
	// 自由な矩形を 1 コマ足して、番号を返す。基準点は矩形の中央
	int32 AddFrame( int32 X, int32 Y, int32 Width, int32 Height );
	int32 AddFrame( const FSpriteFrame& Frame );

	int32 GetNumFrames() const;
	// 範囲外なら nullptr
	const FSpriteFrame* GetFrame( int32 Index ) const;

	// 格子が作るコマの矩形を、画像の大きさ（TextureWidth x TextureHeight）から計算する（画像の外にはみ出すかどうかは見ない。そのまま返す）。
	// 描画に使うコマ割りと、エディタが画像の上に重ねる枠は、この計算で同じになる
	static void ComputeGridFrames( const FSpriteSheetGrid& Grid, int32 TextureWidth, int32 TextureHeight, TArray<FSpriteFrame>& OutFrames );

	//-------------------------------------------------------------------------
	// クリップ
	//-------------------------------------------------------------------------
	// 足す（同じ名前があれば置き換える）。戻り値は番号
	int32 AddClip( const FSpriteClip& Clip );
	// 無ければ INDEX_NONE
	int32 FindClip( const FString& Name ) const;
	FORCEINLINE int32 GetNumClips() const { return Definition.Clips.Num(); }
	const FSpriteClip* GetClip( int32 Index ) const;
	// 再生用の時間軸（AddClip のときに作ってある）
	const FAnimTimeline* GetTimeline( int32 Index ) const;

private:
	// 定義から、コマの矩形を作り直す。画像の大きさが分からないうちは、自動の大きさの格子を保留する
	void RebuildFrames() const;
	void FillGrid( const FSpriteSheetGrid& Grid, int32 StartIndex, int32 TextureWidth, int32 TextureHeight ) const;
	void RebuildTimelines();
	static FAnimTimeline MakeTimeline( const FSpriteClip& Clip );

	TSharedPtr<CTexture> Texture;
	FSpriteSheetDefinition Definition;
	mutable TArray<FSpriteFrame> Frames;
	mutable bool bPendingSize = false;   // 画像の大きさが要る格子が、まだ作れていない
	TArray<FAnimTimeline> Timelines;     // Definition.Clips と同じ並び
};
