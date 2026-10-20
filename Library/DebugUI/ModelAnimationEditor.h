#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/DefinitionEditing.h"
#include "Library/Framework/Asset/ModelAnimationAsset.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// モデルのアニメーションエディタ
//   モデルのアニメーションの定義（.modelanim.json）を、モデルを動かして確かめながら作って保存する。
//
//   - 3D プレビュー: ドラッグで回転、中ボタン（または右ボタン）で移動、ホイールで拡大。床の格子と軸つき
//   - クリップ: モデルのファイルに入っているアニメーション（動きだけの別ファイルも）から追加。名前・ループ・速さ・フェードを設定
//   - タイムライン: 再生位置のシーク、出来事（イベント）の追加・ドラッグで移動・名前の変更・削除（位置は 0..1）。再生すると出来事が光る
//   - クロスフェードの確認: 2 つのクリップを選んでフェード時間を変えながら切り替え、重みをバーで見る
//   - 見た目の補正（位置・回転・拡大・1 秒あたりの時間単位）
//   - 元に戻す / やり直す（Ctrl+Z / Ctrl+Y）、保存、検証
//
//   編集の中身は DefinitionEditing.h（NModelAnimationEdit）。ここは画面だけ。
//=============================================================================
struct FModelAnimationEditorState
{
	//-------------------------------------------------------------------------
	// 文書
	//-------------------------------------------------------------------------
	TModelAnimationDocument Document;
	FString FilePath;                       // アセットのルートからのパス（空なら、まだ保存していない新規）

	//-------------------------------------------------------------------------
	// プレビュー（定義から作った資産とアニメーター）
	//-------------------------------------------------------------------------
	TSharedPtr<CModelAnimationAsset> Asset;
	TSharedPtr<CModel> PreviewModel;        // モデルを持たないアニメーション集（またはアニメーションだけのファイル）を 3D で見るためのモデル
	FModelAnimator Animator;
	uint32 AssetRevision = ~0u;
	FString BuiltModelPath = "?";
	FString BuiltPreviewModelPath = "?";
	int32 PlayingClip = INDEX_NONE;         // Animator が再生しているクリップ（選択が変わったら再生し直す）
	bool bPlaying = true;
	float32 PreviewSpeed = 1.0f;
	bool bLoopView = false;
	TArray<TPair<FString, float32>> EventFlashes;   // 出来事の名前と、光らせる残り時間
	int32 ForceLeftTab = INDEX_NONE;                // 起動引数で、最初に開く左のタブ（0 Clips / 1 Model）

	//-------------------------------------------------------------------------
	// 選択
	//-------------------------------------------------------------------------
	int32 SelectedClip = INDEX_NONE;
	int32 SelectedEvent = INDEX_NONE;
	FString NameBuffer;
	FString NewEventName = "Event";
	FString EventNameBuffer;
	int32 EventNameBufferIndex = INDEX_NONE;

	//-------------------------------------------------------------------------
	// 3D 表示
	//-------------------------------------------------------------------------
	int32 PreviewScreen = -1;
	int32 PreviewWidth = 0;
	int32 PreviewHeight = 0;
	float32 Yaw = 0.6f;
	float32 Pitch = 0.35f;
	float32 Distance = 12.0f;
	FVector3 Target = FVector3::Zero;
	FVector3 BoundsMin = FVector3::Zero;
	FVector3 BoundsMax = FVector3::Zero;
	bool bHaveBounds = false;
	bool bShowFloor = true;
	bool bShowAxes = true;
	bool bFitRequested = true;
	float32 Background[3] = { 0.12f, 0.13f, 0.16f };

	//-------------------------------------------------------------------------
	// クロスフェードの確認
	//-------------------------------------------------------------------------
	int32 BlendFrom = INDEX_NONE;
	int32 BlendTo = INDEX_NONE;
	float32 BlendFade = 0.5f;

	//-------------------------------------------------------------------------
	// ファイル
	//-------------------------------------------------------------------------
	TArray<FString> ModelFiles;             // アセットのルートの下のモデル
	TArray<FString> DefinitionFiles;        // アセットのルートの下の .modelanim.json
	double LastScanTime = -100.0;
	FString OpenSelection;
	FString NewModelSelection;
	FString AddSourceSelection;             // クリップを足すアニメーションの入ったファイル（空ならモデル自身）
	FString SaveAsPath;
	bool bExportMV1OnSave = true;           // 保存するとき、定義が使っている変換前のモデル（.fbx / .obj / .x / .pmx など）も .mv1 として一緒に出力する
	FString StatusMessage;
	bool bStatusIsError = false;
	bool bRequestOpenUnsavedPopup = false;
	enum class EPending : uint8 { None, Open, New, NewMotionPack, Close } Pending = EPending::None;
	FString PendingPath;
};

namespace NDebugUI
{
	// ウィンドウを 1 つ出す。pOpen が null でなければ、× ボタンで false になる
	void DrawModelAnimationEditor( FModelAnimationEditorState& State, bool* pOpen = nullptr );

	bool OpenModelAnimationFile( FModelAnimationEditorState& State, const FString& AssetPath );
	bool NewModelAnimationFromModel( FModelAnimationEditorState& State, const FString& ModelPath );
	bool NewMotionPack( FModelAnimationEditorState& State, const FString& PreviewModelPath = FString() );
	// 3D プレビューに使った画面を片付ける（アプリの終了前に）
	void ReleaseModelAnimationEditor( FModelAnimationEditorState& State );
}
