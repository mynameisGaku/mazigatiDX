// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Sound/PcmEdit.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"
#include "Library/Framework/Asset/Sound/Waveform.h"
#include "Library/Framework/Audio/AudioTypes.h"

//=============================================================================
// サウンドエディター（デバッグオーバーレイのエディタ。Editor.exe の Tools からも開く）
//   音のファイル（.wav と、バックエンドが読める形式: .mp3 / .ogg / .wma / .aac / .m4a / .flac など）を波形で見て、範囲を選び、切り出す・音量をそろえる・フェードさせる・割れた所を見つけて、WAV に保存する。
//
//   - 波形: ホイール = 横へ動かす、Ctrl + ホイール = カーソルを中心に拡大縮小、左ドラッグ = 範囲を選ぶ（クリックだけなら、選択を外す）
//   - 割れた所（絶対値が、しきい値以上）は赤く塗る
//   - 加工は、選んだ範囲だけ（選んでいなければ全体）。1 つ前に戻す・やり直すが使える（16 回まで）
//   - 選んだ範囲を削る（Delete selection。つなぎ目は、短くクロスフェードする）・無音の部分を詰める（Shorten silences）・音量を、dB（0.01 刻み）で、または範囲の頭から終わりへ、なめらかに変える（Gain ramp）
//   - 開始位置: 目盛りをクリックすると再生の開始位置（黄色い線）が決まる。選択の始まりと終わりは、秒の数値でも指せる。Play は、選択があればその範囲を、無ければ開始位置から最後まで鳴らす
//   - 試し聞きは、押したときだけ鳴らす（音量の既定は小さめ）
//   - 保存は 16 ビットの WAV（元のファイルは変えない。名前を付けて保存）
//   - ループ区間: 範囲を選んで Set loop = selection（波形に緑の印）。WAV の smpl チャンクとして保存され（他のツールとも共通）、開くと読み込まれる。
//     ゲームでは、CAssetSubsystem::LoadSound が、WAV の smpl を見て、ループ区間（CSound::SetLoopRegion）にする。Play looped で、ループの聞こえ方を確かめられる
//   - 編集の中身は、NPcmEdit / CWaveformPeaks（鳴らさずに検査できる純粋な部品）
//=============================================================================
struct FSoundEditorState
{
	//-------------------------------------------------------------------------
	// 編集中の音
	//-------------------------------------------------------------------------
	FPcmBuffer Pcm;                         // 加工中の音
	CWaveformPeaks Peaks;
	FString FilePath;                       // 開いたファイル（アセットのルートからの相対。空なら、まだ開いていない）
	bool bDirty = false;                    // 開いたあとに加工して、まだ保存していない
	TArray<FFrameRange> Clipped;            // 割れた所
	TArray<FPcmBuffer> UndoStack;
	TArray<FPcmBuffer> RedoStack;

	//-------------------------------------------------------------------------
	// 表示・選択
	//-------------------------------------------------------------------------
	int64 ViewStart = 0;                    // 見えている範囲の先頭（フレーム）
	int64 ViewFrames = 0;                   // 見えている範囲の長さ（フレーム。0 なら、全体に合わせる）
	int64 SelectionStart = 0;
	int64 SelectionEnd = 0;                 // 含まない。Start >= End なら、選んでいない
	int64 DragAnchor = 0;
	bool bDragging = false;
	int64 CursorFrame = 0;                  // 再生の開始位置（フレーム）
	bool bHasCursor = false;
	bool bDraggingCursor = false;
	TArray<FFrameRange> Silences;           // 探した無音（Find silences を押したとき。音が変わると空に戻る）
	bool bShowSilences = true;
	bool bShowClipped = true;
	float32 ClipThreshold = 0.999f;

	//-------------------------------------------------------------------------
	// 加工の設定
	//-------------------------------------------------------------------------
	float32 NormalizeTargetDb = -1.0f;
	float32 GainDb = 0.0f;
	float32 RampStartDb = 0.0f;
	float32 RampEndDb = -12.0f;
	float32 DeleteCrossfadeMilliseconds = 2.0f;
	float32 SilenceThresholdDb = -50.0f;
	float32 SilenceMinMilliseconds = 200.0f;
	float32 SilenceKeepMilliseconds = 60.0f;
	float32 FadeInMilliseconds = 10.0f;
	float32 FadeOutMilliseconds = 10.0f;
	float32 SnapMilliseconds = 5.0f;
	bool bSmoothFade = true;

	//-------------------------------------------------------------------------
	// 試し聞き
	//-------------------------------------------------------------------------
	FSoundHandle PreviewHandle;
	float32 PreviewVolume = 0.4f;
	bool bPreviewLoop = false;

	//-------------------------------------------------------------------------
	// ファイル
	//-------------------------------------------------------------------------
	TArray<FString> SoundFiles;             // アセットのルートの下の音のファイル
	double LastScanTime = -100.0;
	FString OpenSelection;
	FString OpenPath;                       // 一覧に無い拡張子の音を、パスで指して開くための入力
	FString SaveAsPath;
	bool bOverwrite = false;
	FString StatusMessage;
	bool bStatusIsError = false;
};

namespace NDebugUI
{
	// 音のファイル（アセットのルートからの相対パス）を開く。読めない・未保存の加工が別のファイルにあるときは false（理由は State.StatusMessage）
	bool OpenSoundFile( FSoundEditorState& State, const FString& AssetPath );

	// ウィンドウを 1 つ出す。pOpen を渡すと、閉じるボタンが付く
	void DrawSoundEditor( FSoundEditorState& State, bool* pOpen = nullptr );
}
