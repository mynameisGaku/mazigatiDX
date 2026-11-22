// SPDX-License-Identifier: MIT
#include "CanvasDrawExecutor.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Framework/Render/Scene/SceneDrawOrder.h"
#include "Library/Platform/Render/Canvas/CanvasExecution.h"
#include "Library/Platform/Render/Canvas/CanvasImageEffects.h"
#include "Library/Platform/Render/Canvas/CanvasImagePainter.h"
#include "Library/Platform/Render/Canvas/CanvasSceneCommands.h"
#include "Library/Platform/Render/Canvas/CanvasShapePainter.h"
#include "Library/Platform/Render/Canvas/CanvasStateCommands.h"
#include "Library/Platform/Render/Canvas/CanvasTargetScopes.h"
#include "Library/Platform/Render/Canvas/CanvasTextPainter.h"
#include "Library/Platform/System/BackendSupport.h"

CCanvasDrawExecutor::CCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels, IFontDrawSource* InFonts, IRenderTargetDrawSource* InTargets, IShaderDrawSource* InShaders )
	: Surface( InSurface ), Models( InModels ), Buffers( InBuffers ), Textures( InTextures ), Fonts( InFonts ), Targets( InTargets ), Shaders( InShaders )
{
}

void CCanvasDrawExecutor::Execute( const FDrawSpan& Input )
{
	TArray<uint32> OrderedWords; // 今回の実行だけが所有する並べ替え済み命令。
	bool bSceneValid = true; // 不正なシーンは元の順序のまま処理する。
	const FDrawSpan Span = NSceneDrawOrder::Prepare( Input, OrderedWords, bSceneValid ); // 呼出し中は入力と所有コピーを保持する。
	IRenderCanvas2D* Canvas = Surface.GetCanvas2D(); // フレーム内だけ借りる。
	if ( !Canvas || !Span.Words || Span.NumWords <= 0 )
	{
		return;
	}
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "CanvasExecute" );
	FCanvasExecution Run( Surface, Textures, Buffers, Models, Fonts, Targets, Shaders, Span, Canvas ); // このSpanだけの状態と、借りた実行先。
	CCanvasTargetScopes TargetScopes( Run ); // 描画先（画像・影の絵・複数の出力）の積み。
	CCanvasStateCommands StateCommands( Run ); // 合成・補間・明るさ・変換・クリップ・マスクなどの状態。
	CCanvasShapePainter Shapes( Run ); // 平らな図形。
	CCanvasImagePainter Images( Run ); // 画像・スプライト・タイル・円ゲージ。
	CCanvasTextPainter Text( Run, Images ); // 文字列。
	CCanvasImageEffects ImageEffects( Run, TargetScopes ); // 画像のフィルター・混ぜ合わせ。
	CCanvasSceneCommands Scene( Run, TargetScopes ); // 3D・シェーダー・材質の命令。
	Run.ShaderExecutor.SetPrepare( [&TargetScopes]( FShaderDrawRequest& Request )
		{ return TargetScopes.PrepareTargets( Request ); } ); // シェーダーで描く前に、描画先のパスを整える。
	int32 Offset = 0; // 読み終えたword数。
	while ( Offset < Span.NumWords && !Run.bStopped )
	{
		const uint32 Header = Span.Words[Offset]; // 命令の固定ヘッダー。
		const int32 Count = static_cast<int32>( Header >> 16 ); // ヘッダー込みの長さ。
		if ( Count < 1 || Count > Span.NumWords - Offset )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "Malformed command span" );
			break;
		}
		FDrawCommandView View; // 検査済みの範囲だけを読む。
		View.Type = static_cast<EDrawCommand>( Header & 255 );
		View.Flags = static_cast<uint8>( Header >> 8 );
		View.Data = Span.Words + Offset + 1;
		View.NumDataWords = Count - 1;
		Offset += Count;
		if ( View.Type == EDrawCommand::BeginScene3D || View.Type == EDrawCommand::EndScene3D || View.Type == EDrawCommand::SceneDrawKey3D )
		{
			continue;
		}
		// 描画先・影の絵の、始まりと終わり。描けない範囲は、対になる終わりまで、まとめて飛ばす（画面へ描いてしまわないように）。
		if ( TargetScopes.ExecuteScope( View ) )
		{
			continue;
		}
		// 影の絵へ描いている間は、描画先・カメラ・クリップ・消去などの命令は、使えない（DxLib と同じ。無視する）。描けない範囲の中も、描かない。
		if ( TargetScopes.ShouldSkip( View.Type ) )
		{
			continue;
		}
		if ( StateCommands.ShouldSkipForAlphaTest( View ) )
		{
			continue;
		}
		Run.Canvas->SetAntiAlias( View.Has( FDrawFlag::AntiAlias ) || View.Type == EDrawCommand::BoxAA || View.Type == EDrawCommand::CircleAA || View.Type == EDrawCommand::LineAA );
		bool bHandled2D = false;
		{
			PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Canvas2D" );
			bHandled2D = StateCommands.Execute( View ) || Shapes.Execute( View ) || Text.Execute( View ) || Images.Execute( View ) || TargetScopes.ExecuteTargetCommand( View ) || ImageEffects.Execute( View );
		}
		if ( bHandled2D )
		{
			continue;
		}
		Scene.Execute( View );
	}
	TargetScopes.UnwindToScreen(); // 戻し忘れた描画先を、主画面まで戻す。
	if ( !Run.Canvas )
	{
		return;
	}
	while ( Run.State.ClipDepth > 0 )
	{
		Run.Canvas->PopClip();
		--Run.State.ClipDepth;
	}
	Run.Canvas->SetTextureFilter( true ); // 外側のImGui描画の既定へ戻す。
	Run.Canvas->SetAntiAlias( true );
	Run.Canvas->SetBlend( EGeometryBlend::Alpha );
	if ( Run.bStopped )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "Incomplete span", "Malformed payload: remaining commands were skipped" );
	}
}
