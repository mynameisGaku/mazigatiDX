// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Effect/ParticleEffectEditing.h"
#include "Library/Framework/Asset/SpriteSheet.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Platform/DxLib/EffectPreviewSurface.h"

// 内蔵エフェクトの編集中の文書、選択、再生と画面の寿命を一つのツールにまとめる。
struct FEffectEditorState
{
	// 保存前の定義と、元に戻す・やり直す履歴。
	TParticleEffectDocument Document;
	// 編集中のファイル。空なら保存先が未定。
	FString FilePath;
	// 名前を付けて保存する際の入力欄。
	FString SaveAsPath;
	// 開くダイアログで選択したファイル。
	FString OpenSelection;
	// いま編集しているエミッターの番号。
	int32 SelectedEmitter = 0;
	// 力場一覧で選択中の番号。文書には保存しない。
	int32 SelectedForceField = 0;
	// 力場の範囲表示を試し再生へ重ねるか。文書には保存しない。
	bool bShowForceFieldGuides = true;
	// 初めて表示した際の新規文書作成を済ませたか。
	bool bInitialized = false;
	// 操作結果を表示する文章。
	FString StatusMessage;
	// 操作結果が失敗を示すか。
	bool bStatusIsError = false;
	// アセットのルートから見つかった定義ファイル。
	TArray<FString> EffectFiles;
	// 粒子の画像として選べるファイル。
	TArray<FString> TextureFiles;
	// Flipbook のシート候補。
	TArray<FString> SheetFiles;
	// 同じ選択パスを毎frame読み直さないためのcache key。
	FString FlipbookSheetCachePath;
	// 成功・失敗のどちらも一度試したことを示す。
	bool bFlipbookSheetLoadAttempted = false;
	// 選択シートの読込結果。失敗もpath keyと一緒に保持する。
	TSharedPtr<CSpriteSheet> FlipbookSheetCache;
	// 選択emitterと定義hashに結び付けた保存前検証結果。
	uint64 FlipbookValidationCacheKey = ~0ull;
	// 最後に調べたFlipbook資産の問題。
	TArray<FString> FlipbookValidationIssues;
	// Distortion noiseの読込を一度だけ行うためのpath key。
	FString UVDistortionTextureCachePath;
	// 現在のnoise pathを成功・失敗に関係なく試した印。
	bool bUVDistortionTextureLoadAttempted = false;
	// エディタが検証表示するnoise textureの参照。
	TSharedPtr<CTexture> UVDistortionTextureCache;
	// ファイル一覧を最後に調べた時刻。
	double LastScanTime = -100.0;
	// 現在の文書に残るエラーと警告。
	TArray<FParticleIssue> Issues;
	// 編集内容のコピーだけを参照する、保存前の試し再生。
	TSharedPtr<IEffectPlayback> Playback;
	// いまの再生へ反映した文書の変更番号。
	uint32 PlaybackRevision = ~0u;
	// 再生を進めるか。一時停止中も描画と編集はできる。
	bool bPlaying = true;
	// 一度だけのエフェクトも終了したら試し再生をやり直すか。
	bool bRepeatPreview = true;
	// 一度だけの再生が終わってから、やり直すまでの経過秒。
	float32 FinishedTime = 0.0f;
	// 試し再生だけに掛ける時間の倍率。
	float32 PreviewSpeed = 1.0f;
	// リボンの残像を見るため、試し再生だけ生成位置を円形に動かすか。
	bool bPreviewMotion = false;
	// 生成位置の円運動に使う秒数。定義には保存しない。
	float32 PreviewMotionTime = 0.0f;
	// 試し再生を見るカメラ。
	FEditorCamera Camera;
	// Tree文書を開くかcameraをresetした後、実viewportで一度だけ全体を合わせる印。
	bool bTreeCameraFocusPending = false;
	// Tree、Meshまたは力場を含む文書を開くかcamera reset後、実viewportで一度だけ全体を合わせる印。
	bool bEffectGeometryCameraFocusPending = false;
	// カメラへ渡す描画範囲。
	FEditorViewport View;
	// ツールが開いている間だけ所有する描画先。
	FEffectPreviewSurface Surface;
	// 未保存の変更を確認したあとに実行する操作。
	enum class EPending : uint8
	{
		None,   // 確認待ちの操作なし。
		Open,   // 別の定義を開く。
		New,    // 新しい定義を作る。
		Preset, // プリセットを、原本とは別の新規文書へコピーする。
		Close,  // 文書と再生の画面を閉じる。
	};
	// 確認待ちの操作。
	EPending Pending = EPending::None;
	// 確認待ちの読み込み先。
	FString PendingPath;
	// 次に未保存確認を開く必要があるか。
	bool bRequestUnsavedPopup = false;
	// 保存の成功ごとに増やす番号。配置済みのプレビュー更新にも使う。
	uint32 SaveSerial = 0;
};

namespace NDebugUI
{
	// 内蔵エフェクトの定義を編集し、保存前の再生を同じウィンドウに表示する。
	void DrawEffectEditor( FEffectEditorState& State, bool* pOpen = nullptr );
	// 定義を開く。読み込みの失敗では、編集中の文書と再生を保持する。
	bool OpenEffectFile( FEffectEditorState& State, const FString& AssetPath );
	// 一つのエミッターを持つ新規文書を作る。未保存確認は呼び出し側が行う。
	void NewEffect( FEffectEditorState& State );
	// プリセットを新規文書へコピーする。失敗時は現在の文書を保つ。未保存確認は呼び出し側が行う。
	bool NewEffectFromPreset( FEffectEditorState& State, const FString& PresetPath );
	// 検証を通った定義だけを保存する。失敗では保存済みの印やパスを変えない。
	bool SaveEffectFile( FEffectEditorState& State, const FString& AssetPath = FString() );
	// 文書の変更をコピーして試し再生へ反映し、有限で正の経過時間だけを進める。
	void UpdateEffectEditor( FEffectEditorState& State, float32 DeltaTime );
	// 次の更新で同じ定義を最初から試し再生する。
	void RestartEffectPreview( FEffectEditorState& State );
	// 終了前に再生と描画先を解放する。文書と履歴は保持する。
	void ReleaseEffectEditor( FEffectEditorState& State );
}
