// SPDX-License-Identifier: NOASSERTION
#pragma once

#include "Library/DebugUI/EffectEditor.h"

// 描画コマンドを追加する既存の記録先。
class FDrawContext;

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
	// 固定向きの平面・円環を編集し、描画能力と排他設定を表示する。
	bool DrawMeshPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter );
	// 力場の一覧と保存値を編集する。戻り値はコピーの保存値変更、通知値は既存のtrueを保持する。
	bool DrawForceFieldPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter, bool& bInputEnded, bool& bStructureChanged );
	// カメラ操作を処理し、現在の試し再生を描画する。時間はここでは進めない。
	void DrawPreview( FEffectEditorState& State );
	// Treeの完成形と線幅を含む有限boundsへカメラを合わせる。無効・空なら元のカメラを保つ。
	bool FocusCameraOnTree( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 実viewportが確定した後に保留中のTree focusを一度だけ適用する。
	bool ApplyPendingTreeCameraFocus( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 文書内の力場があればその範囲、無ければTreeと固定meshの有限boundsへカメラを合わせる。失敗時は元のカメラを保つ。
	bool FocusCameraOnEffectGeometry( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 力場の有限な範囲へカメラを合わせる。場が無いか無効なら元のカメラを保つ。
	bool FocusCameraOnForceFields( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 試し再生が使っているエミッター姿勢を返す。Move emitter previewが無効ならIdentityで、拡大は使わない。
	FTransform GetPreviewEmitterTransform( const FEffectEditorState& State );
	// 力場の範囲と中心を既存描画先へ記録する。追従する場は現在の試し再生の姿勢で描く。
	void RecordForceFieldGuides( const FEffectEditorState& State, FDrawContext& Draw );
	// 指定したエミッター姿勢で、追従する場の範囲と向きを描く。世界固定の場は姿勢に依らない。
	void RecordForceFieldGuidesForTransform( const FEffectEditorState& State, FDrawContext& Draw, const FTransform& EmitterTransform );
	// 実viewportが確定した後に保留中の全体focusを一度だけ適用する。
	bool ApplyPendingEffectCameraFocus( FEffectEditorState& State, const FEditorViewport& Viewport );
	// 開いた文書またはcamera reset後のTree・mesh・力場focusを一度だけ保留する。
	void SetPendingEffectCameraFocus( FEffectEditorState& State, const FParticleEffectDefinition& Definition );
}
