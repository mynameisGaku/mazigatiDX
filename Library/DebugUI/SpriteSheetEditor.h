#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/SpriteAnimator.h"
#include "Library/Framework/Asset/DefinitionEditing.h"
#include "Library/Framework/Asset/SpriteSheet.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// スプライトシートエディタ
//   スプライトシートの定義（.sheet.json）を、画像の上でコマ割りを見ながら作って保存する。
//
//   - 画像: 分割線・コマの番号・基準点を重ねて表示（ホイールで拡大、中ボタンまたは Space + ドラッグで移動）。
//           コマをクリックで選択、Shift + ドラッグで自由な矩形を足す
//   - 格子: 横縦の分割数、1 コマのピクセル数（0 で自動）、余白、隙間、並べ方、基準点を数値で。画像に収まらないコマは赤で警告
//   - クリップ: コマの並び（ステップ）の追加・並べ替え・削除、表示時間、fps、ループ、出来事（イベント）
//   - プレビュー: 選んだクリップを再生。タイムラインで、コマごとの長さと出来事の位置を見て、ドラッグで動かせる
//   - 元に戻す / やり直す（Ctrl+Z / Ctrl+Y）、保存、検証（番号の範囲外・名前の重複など）
//
//   編集の中身は DefinitionEditing.h（NSpriteSheetEdit）。ここは画面だけ。
//=============================================================================
struct FSpriteSheetEditorState
{
	//-------------------------------------------------------------------------
	// 文書
	//-------------------------------------------------------------------------
	TSpriteSheetDocument Document;
	FString FilePath;                       // アセットのルートからのパス（空なら、まだ保存していない新規）
	TSharedPtr<CSpriteSheet> Sheet;         // 編集中の定義から作った、プレビュー用
	uint32 SheetRevision = ~0u;
	FString SheetTexturePath;               // Sheet が使っている画像（変わったら作り直す）

	//-------------------------------------------------------------------------
	// 選択
	//-------------------------------------------------------------------------
	int32 SelectedGrid = INDEX_NONE;
	int32 SelectedFrame = INDEX_NONE;
	int32 SelectedClip = INDEX_NONE;
	int32 SelectedStep = INDEX_NONE;
	int32 SelectedEvent = INDEX_NONE;

	//-------------------------------------------------------------------------
	// 画像の表示
	//-------------------------------------------------------------------------
	float32 Zoom = 4.0f;
	ImVec2 Pan = ImVec2( 8.0f, 8.0f );     // 画像の左上の、キャンバスの左上からのずれ（画面のピクセル）
	bool bFitRequested = true;
	bool bShowGridLines = true;
	bool bShowIndices = true;
	bool bShowPivots = true;
	bool bNearest = true;
	int32 Background = 0;                   // 0: 市松 / 1: 黒 / 2: 白 / 3: マゼンタ
	bool bDrawing = false;                  // Shift + ドラッグで自由な矩形を作っている
	ImVec2 DrawStart = ImVec2( 0.0f, 0.0f );   // 画像のピクセル座標

	//-------------------------------------------------------------------------
	// プレビュー
	//-------------------------------------------------------------------------
	FSpriteAnimator Preview;
	bool bPlaying = true;
	float32 PreviewSpeed = 1.0f;
	float32 PreviewScale = 4.0f;
	bool bPreviewFlipX = false;
	bool bPreviewPivots = true;
	int32 PreviewClip = INDEX_NONE;         // Preview が再生しているクリップ（選択が変わったら再生し直す）
	TArray<TPair<FString, float32>> EventLog;   // 最近起きた出来事の名前と、表示の残り時間（プレビューで）
	int32 ForceLeftTab = INDEX_NONE;            // 起動引数で、最初に開く左のタブ（0 Source / 1 Grid / 2 Frames / 3 Clips）
	FString NameBuffer;                     // クリップ名の入力中の文字列（入力が終わったときに反映する）
	FString EventNameBuffer;                // 出来事の名前の入力中の文字列
	int32 EventNameBufferIndex = INDEX_NONE;
	FString NewEventName = "Event";         // 「出来事を足す」で使う名前
	int32 RangeFirst = 0;
	int32 RangeLast = 0;
	FString SaveAsPath;

	//-------------------------------------------------------------------------
	// ファイル
	//-------------------------------------------------------------------------
	TArray<FString> TextureFiles;           // アセットのルートの下の画像
	TArray<FString> SheetFiles;             // アセットのルートの下の .sheet.json
	double LastScanTime = -100.0;
	FString OpenSelection;
	FString NewTextureSelection;
	FString StatusMessage;
	bool bStatusIsError = false;
	bool bRequestOpenUnsavedPopup = false;
	enum class EPending : uint8 { None, Open, New, Close } Pending = EPending::None;
	FString PendingPath;
};

namespace NDebugUI
{
	// ウィンドウを 1 つ出す。pOpen が null でなければ、× ボタンで false になる
	void DrawSpriteSheetEditor( FSpriteSheetEditorState& State, bool* pOpen = nullptr );

	// ファイルを開く / 新規（コマンドラインや他のウィンドウから呼ぶ用）。失敗したら false（State.StatusMessage に理由）
	bool OpenSpriteSheetFile( FSpriteSheetEditorState& State, const FString& AssetPath );
	bool NewSpriteSheetFromTexture( FSpriteSheetEditorState& State, const FString& TexturePath );
}
