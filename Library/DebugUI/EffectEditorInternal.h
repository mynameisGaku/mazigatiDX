// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_DEBUGUI_EFFECTEDITORINTERNAL_H
#define FRAMEWORK_DEBUGUI_EFFECTEDITORINTERNAL_H

#include "Library/DebugUI/EffectEditor.h"

namespace NEffectEditorUI
{
	// 開く・新規・閉じるを要求する。未保存なら実行せず確認待ちにし、false を返す。
	bool RequestAction( FEffectEditorState& State, FEffectEditorState::EPending Action, const FString& Path, bool* pOpen );
	// 確認済みの操作を実行する。保存が必要なのに失敗した場合は、文書と操作を保持する。
	bool CompletePendingAction( FEffectEditorState& State, bool bSave, bool* pOpen );
	// 全体の設定とエミッター一覧・設定を編集する。履歴は既存の文書へ記録する。
	void DrawDefinitionPanel( FEffectEditorState& State );
	// Sprite の材質・寿命曲線をエミッターのコピー上で編集する。
	bool DrawMaterialPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter );
	// 寿命に沿う幅とRGBA倍率をエミッターのコピー上で編集し、入力終了時に履歴を区切る。
	bool DrawAppearancePanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter );
	// 四角形粒子のシート・クリップ・時計を編集し、選択資産の検証結果を表示する。
	bool DrawFlipbookPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter );
	// カメラ操作を処理し、現在の試し再生を描画する。時間はここでは進めない。
	void DrawPreview( FEffectEditorState& State );
	// Treeの完成形と線幅を含む有限boundsへカメラを合わせる。無効・空なら元のカメラを保つ。
	bool FocusCameraOnTree( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 実viewportが確定した後に保留中のTree focusを一度だけ適用する。
	bool ApplyPendingTreeCameraFocus( FEffectEditorState& State, const FEditorViewport& Viewport );
}

#endif
