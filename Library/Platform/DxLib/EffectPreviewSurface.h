// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// プレビューで使うカメラと描画窓口。描画APIの型は公開しない。
struct FEditorCamera;
struct FEditorViewport;
class IEffectPlayback;
class IDrawExecutor;
struct FDrawSpan;

// ツール用の描画先を所有する。DxLibの初期化から終了まで、メインスレッドだけで使う。
class FEffectPreviewSurface
{
public:
	// 描画先を持たない状態で作る。最初のRenderで確保する。
	FEffectPreviewSurface() = default;
	// 所有する描画先を解放する。DxLibの終了前に破棄すること。
	~FEffectPreviewSurface();
	// 描画先の所有権は複製しない。
	FEffectPreviewSurface( const FEffectPreviewSurface& ) = delete;
	// 描画先の所有権は代入で共有しない。
	FEffectPreviewSurface& operator=( const FEffectPreviewSurface& ) = delete;

	// ImGuiツールの表示中に、指定した視界の格子と再生状態を描く。時間は進めない。
	// 不正な視界・カメラや描画先の確保失敗はfalse。Playbackがnullptrなら格子だけ描く。
	// 元の描画先とカメラ・描画設定を戻し、3DのZ比較と書き込みは2Dの重ね描き用に無効にする。
	bool Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor );
	// 既存の格子・粒子を描いたあと、Line3D / Sphere3Dの範囲線だけを同じcameraで実行する。
	// Overlayは呼び出し中だけ借用し、空なら追加描画なし。全体65535 words以内で他命令・壊れた並びはfalse。
	bool Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor, const FDrawSpan& Overlay );
	// 所有する描画先を解放する。呼び出し時の描画先として使っていないこと。
	void Release();
	// 表示用の描画先を返す。まだ確保していない場合は-1。
	int32 GetHandle() const;

private:
	// この値だけが所有する描画先。-1なら未確保。
	int32 Handle = -1;
	// 現在の描画先の横幅。
	int32 Width = 0;
	// 現在の描画先の縦幅。
	int32 Height = 0;
};
