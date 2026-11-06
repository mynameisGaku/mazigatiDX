// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"

// プレビューで使うカメラと描画窓口。描画APIの型は公開しない。
struct FEditorCamera;
struct FEditorViewport;
class IEffectPlayback;
class IDrawExecutor;
struct FDrawSpan;
class CRenderTarget;

// ツール用の描画先（CRenderTarget）を所有する。メインスレッドだけで使う。
// 描くのは、フレームワークの描画先とコマンドリスト（PushTarget / PushCamera / Depth）だけで、DxLib を直接は呼ばない
// （実行のたびに、実行側が描画先・カメラ・描画の設定を元に戻すので、保存して戻す必要もない）。
class FEffectPreviewSurface
{
public:
	// 描画先を持たない状態で作る。最初のRenderで確保する。
	FEffectPreviewSurface() = default;
	// 所有する描画先を手放す。
	~FEffectPreviewSurface();
	// 描画先の所有権は複製しない。
	FEffectPreviewSurface( const FEffectPreviewSurface& ) = delete;
	// 描画先の所有権は代入で共有しない。
	FEffectPreviewSurface& operator=( const FEffectPreviewSurface& ) = delete;

	// ImGuiツールの表示中に、指定した視界の格子と再生状態を描く。時間は進めない。
	// 不正な視界・カメラや描画先の確保失敗はfalse（確保に失敗したときは、前の描画先と絵を残す）。Playbackがnullptrなら格子だけ描く。
	bool Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor );
	// 既存の格子・粒子を描いたあと、Line3D / Sphere3Dの範囲線だけを同じcameraで実行する。
	// Overlayは呼び出し中だけ借用し、空なら追加描画なし。全体65535 words以内で他命令・壊れた並びはfalse。
	bool Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor, const FDrawSpan& Overlay );
	// 所有する描画先を手放す。
	void Release();
	// 表示用の描画先の画像の番号（ImGui に渡す）を返す。まだ確保していない場合は-1。
	int32 GetHandle() const;

private:
	// この値だけが所有する描画先。nullptrなら未確保。
	TSharedPtr<CRenderTarget> Target;
	// 現在の描画先の横幅。
	int32 Width = 0;
	// 現在の描画先の縦幅。
	int32 Height = 0;
};
