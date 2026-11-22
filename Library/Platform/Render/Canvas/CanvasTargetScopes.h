// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/Render/Canvas/CanvasExecution.h"

// 描画先（画像・影の絵・複数の出力のパス）の切り替えと、積んだ描画先の管理。描けない描画先へ描く範囲は、対の終わりまで飛ばす。
class CCanvasTargetScopes
{
public:
	explicit CCanvasTargetScopes( FCanvasExecution& InRun );
	// 描画先を替える前の、いまの描画先のパスを始める（主画面なら何もしない）。キャンバスと3Dの実行先を取り直す。
	bool EnterTarget( const FRenderTargetDrawView& Target );
	// 描画先・影の絵の、始まりと終わりの命令なら実行して true。
	bool ExecuteScope( const FDrawCommandView& View );
	// 影の絵へ描いている間に使えない命令か、描かない範囲の中か。
	bool ShouldSkip( EDrawCommand Type ) const;
	// 描画先への操作の命令（画面の写し・影の絵の表示・影を受ける設定）なら実行して true。
	bool ExecuteTargetCommand( const FDrawCommandView& View );
	// シェーダーで描く前に、描画先のパスを整える。書く描画先の数と形式を、Request に入れる。
	bool PrepareTargets( FShaderDrawRequest& Request );
	// 複数の出力（MRT）のパスから、1つの出力のパスへ戻る（描いてある内容は保つ）。
	void LeaveMrt();
	// 戻し忘れた描画先を、主画面まで戻す。
	void UnwindToScreen();

private:
	void Unsupported( const FDrawCommandView& View, const char* Reason );
	void ApplyMask();
	FCanvasExecution& Run; // 1 回の実行の状態と、借りた実行先。
	IRenderSurface& Surface; // Run.Surface の別名。
	IRenderTargetDrawSource*& Targets; // Run.Targets の別名。
	IShaderDrawSource*& Shaders; // Run.Shaders の別名。
	IRenderCanvas2D*& Canvas; // Run.Canvas の別名。
	CCanvasNullCanvas2D& NullCanvas; // Run.NullCanvas の別名。
	CGeometryCommandExecutor& Geometry; // Run.Geometry の別名。
	CShaderCommandExecutor& ShaderExecutor; // Run.ShaderExecutor の別名。
	bool& bStopped; // Run.bStopped の別名。
	bool& bAlphaBlend; // Run.State.bAlphaBlend の別名。
	int32& ClipDepth; // Run.State.ClipDepth の別名。
	FCanvasTransform& Transform; // Run.State.Transform の別名。
	TArray<FCanvasMaskState>& MaskStack; // Run.State.MaskStack の別名。
	TArray<FCanvasTargetScope>& TargetStack; // Run.TargetState.TargetStack の別名。
	FRenderTargetDrawView& CurrentTarget; // Run.TargetState.CurrentTarget の別名。
	bool& bMrtActive; // Run.TargetState.bMrtActive の別名。
	TArray<uint64>& MrtSignature; // Run.TargetState.MrtSignature の別名。
	int32& SkipTargetDepth; // Run.TargetState.SkipTargetDepth の別名。
	int32& SkipShadowDepth; // Run.TargetState.SkipShadowDepth の別名。
	bool& bShadowPassActive; // Run.TargetState.bShadowPassActive の別名。
};
