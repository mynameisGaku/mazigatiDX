// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"

#include <cmath>
#include <initializer_list>
#include <limits>

#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// Tree Lightningの実パネルを独立ImGui文脈で操作し、画面設定に触れない入力fixture。
	class FTreeLightningInputContext
	{
	public:
		// selftest専用contextとfontを用意し、iniや既存backendを使わない。
		FTreeLightningInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1200.0f, 1200.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// 独立文脈を破棄して呼出元のcontextへ戻す。
		~FTreeLightningInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextを複製しない。
		FTreeLightningInputContext( const FTreeLightningInputContext& ) = delete;
		// ImGui contextを代入しない。
		FTreeLightningInputContext& operator=( const FTreeLightningInputContext& ) = delete;

		// font atlas構築が成功したかを返す。
		bool IsReady() const { return bFontsReady; }
		// panelを一回描き、ImGui frameとwindow stackを閉じる。
		bool Frame( FEffectEditorState& State )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1100.0f, 1100.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "TreeLightningInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				NEffectEditorUI::DrawDefinitionPanel( State );
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bFramesBalanced;
		}
		// キーを一度押して離し、両方のframeでpanelを進める。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}
		// Tab巡回で指定項目へfocusを移す。
		bool TabTo( FEffectEditorState& State, ImGuiID Id )
		{
			for ( int32 Attempt = 0; Attempt < 512; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}
		// 最後に描画した定義panel上の通常項目IDを求める。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }
		// DragIntを一時文字入力へ切り替え、指定値を入力する。
		bool ReplaceNumeric( FEffectEditorState& State, ImGuiID Id, const char* Text )
		{
			Tap( State, ImGuiKey_Enter );
			const bool bActivated = Context->TempInputId == Id && Context->ActiveId == Id;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State );
			return bActivated && Context->TempInputId == Id && Context->ActiveId == Id;
		}

	private:
		// test開始時に選ばれていたImGui context。
		ImGuiContext* Previous = nullptr;
		// このfixtureだけが所有するcontext。
		ImGuiContext* Context = nullptr;
		// 最終frameのpanel window。
		ImGuiWindow* Window = nullptr;
		// font atlasが利用可能か。
		bool bFontsReady = false;
		// すべてのframeがstackを閉じたか。
		bool bFramesBalanced = true;
	};

	// dropdown、数値入力、両Reset入口が実際の定義と履歴へつながることを確認する。
	void TestTreeLightningPanelInput( NAssetTest::FChecker& C, FTreeLightningInputContext& Input )
	{
		C.Group = "effect-tree-lightning-input";
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		Emitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State;
		State.Document.Reset( Definition );
		C.Check( Input.Frame( State ) && Input.Frame( State ), "独立ImGui文脈でEffectEditor panelを描画する" );
		// Draw as combo自身のTab移動先。
		const ImGuiID DrawAs = Input.ItemId( "Draw as" );
		C.Check( Input.TabTo( State, DrawAs ), "Draw asの実入力へTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		Input.Tap( State, ImGuiKey_DownArrow );
		Input.Tap( State, ImGuiKey_DownArrow );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Path.Kind == FEffectPathDefinition::EKind::TreeLightning, "Draw as選択からTree Lightningを実際に選べる" );
		// Tree固有の段数入力。
		const ImGuiID Depth = Input.ItemId( "Tree depth" );
		C.Check( Input.TabTo( State, Depth ), "Tree depth入力へTabで移る" );
		// 実際に文字入力へ切り替わったか。
		const bool bDepthTyped = Input.ReplaceNumeric( State, Depth, "1" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( bDepthTyped && State.Document.Get().Emitters[0].Path.Tree.Depth == 1 && State.Document.CanUndo(), "Tree depthを文字入力してUndo可能な履歴へ記録する" );
		// Undo直後にkindとdepthが入力前の値へ一緒に戻る結果。
		const bool bUndone = State.Document.Undo();
		C.Check( bUndone && State.Document.Get().Emitters[0].Path.Kind == FEffectPathDefinition::EKind::TreeLightning && State.Document.Get().Emitters[0].Path.Tree.Depth == 2, "Tree depthのUndoはkind選択を保ち、入力前の段数へ戻す" );
		// Redo直後にTree選択と入力済みdepthを復元する結果。
		const bool bRedone = State.Document.Redo();
		C.Check( bRedone && State.Document.Get().Emitters[0].Path.Kind == FEffectPathDefinition::EKind::TreeLightning && State.Document.Get().Emitters[0].Path.Tree.Depth == 1, "Tree入力をRedoして選択状態と変更値を復元する" );
		// 通常の値入力でstroke期間より短いintervalを作れないことを確かめる。
		const ImGuiID StrokeInterval = Input.ItemId( "Stroke interval (s)" );
		C.Check( Input.TabTo( State, StrokeInterval ), "stroke interval入力へTabで移る" );
		// 既定stroke期間より短い値を文字入力から試す。
		const bool bIntervalTyped = Input.ReplaceNumeric( State, StrokeInterval, "0.01" );
		Input.Tap( State, ImGuiKey_Tab );
		// 入力後に組合せが妥当なTree値のままか。
		const FEffectLightningTreeDefinition& IntervalTree = State.Document.Get().Emitters[0].Path.Tree;
		C.Check( bIntervalTyped && IntervalTree.StrokeInterval >= IntervalTree.StrokeSeconds, "intervalを短縮入力しても光る時間との妥当な組合せを保つ" );
		// NaNの共通Path値を入力欄へ渡さず共通Resetから直す。
		// 不正座標を注入する編集対象。
		FParticleEmitterDefinition& InvalidCommon = State.Document.BeginEdit().Emitters[0];
		InvalidCommon.Path.End.x = ( std::numeric_limits<float32>::quiet_NaN )();
		State.Document.EndEditSession();
		Input.Frame( State );
		// 共通Path値だけを戻すbutton ID。
		const ImGuiID ResetCommon = Input.ItemId( "Reset tree path values" );
		C.Check( Input.TabTo( State, ResetCommon ), "共通Path値の修復ボタンへTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( std::isfinite( State.Document.Get().Emitters[0].Path.End.x ) && State.Document.Get().Emitters[0].Path.Tree.Depth == 1, "共通PathのResetは不正座標だけ直しTree専用設定を保つ" );
		// 範囲外Tree fieldを設定して専用Resetが独立に修復する。
		// Tree専用範囲外と有効な共通値を一回の編集として仕込む。
		// 不正Tree設定と維持する共通値を入れる編集対象。
		FParticleEmitterDefinition& InvalidTree = State.Document.BeginEdit().Emitters[0];
		InvalidTree.Path.End.x = 1.25f;
		InvalidTree.Path.Tree.Depth = 5;
		State.Document.EndEditSession();
		Input.Frame( State );
		// Tree固有の8項目を戻すbutton ID。
		const ImGuiID ResetTree = Input.ItemId( "Reset tree settings" );
		C.Check( Input.TabTo( State, ResetTree ), "Tree固有設定の修復ボタンへTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Path.Tree.Validate().IsEmpty() && State.Document.Get().Emitters[0].Path.End.x == 1.25f, "Tree Resetは専用8項目だけ直し、有効な共通Path値を維持する" );
	}

	// 実viewportで完成Treeの幅まで収め、旧pathと不正Treeではcameraを保つ。
	void TestTreeCameraFit( NAssetTest::FChecker& C )
	{
		C.Group = "effect-tree-lightning-camera-fit";
		// 決定論的seedと拡大曲線を持つfit対象。
		FParticleEffectDefinition Definition;
		Definition.Seed = 151u;
		// Geometry builderが作る完成形のtree emitter。
		FParticleEmitterDefinition Emitter;
		Emitter.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		Emitter.Path.Tree.GrowthSeconds = 0.5f;
		Emitter.Path.Tree.BranchDelaySeconds = 0.1f;
		Emitter.Path.Segments = 24;
		Emitter.Path.Branches = 4;
		Emitter.Path.BranchLength = 2.0f;
		Emitter.Path.Jitter = 0.0f;
		Emitter.Path.Tree.Depth = 2;
		Emitter.Path.Tree.ChildBranches = 4;
		Emitter.Path.Tree.BranchAngleDegrees = 85.0f;
		Emitter.StartSize = { 0.2f, 0.4f };
		Emitter.EndSize = { 0.1f, 0.3f };
		Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Appearance.Size.Keys.Add( { 1.0f, 2.0f } );
		Definition.Emitters.Add( Emitter );
		// previewが実際に使う横長viewport。
		FEditorViewport Viewport;
		Viewport.Width = 320;
		Viewport.Height = 180;
		// 開いた文書から一度だけfocusを適用する状態。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.bTreeCameraFocusPending = true;
		const bool bFocused = NEffectEditorUI::ApplyPendingTreeCameraFocus( State, Viewport );
		C.Check( bFocused && !State.bTreeCameraFocusPending && std::isfinite( State.Camera.Distance ) && State.Camera.Distance > FEditorCamera::MinDistance, "pending Tree focusは実viewport確定後に一度適用し有限cameraを作る" );
		// 成長中と枝遅延を除き、全seed bounds APIと同じPathを用意する。
		FEffectPathDefinition CompletedPath = Emitter.Path;
		CompletedPath.Tree.GrowthSeconds = 0.0f;
		CompletedPath.Tree.BranchDelaySeconds = 0.0f;
		// 全seedの枝先と最大halo半幅を含む独立な保守sphere。
		FVector3 ExpectedCenter = FVector3::Zero;
		// 全seedの枝先と最大halo半幅までの距離。
		float32 ExpectedRadius = 0.0f;
		const bool bBoundsAvailable = NEffectPath::GetTreeLightningBounds( CompletedPath, FVector3::Zero, CompletedPath.End, 1.2f, ExpectedCenter, ExpectedRadius );
		// focusしたcameraが返却sphereを注視し、全serial geometryも収めるか。
		bool bAllTreeBoundsVisible = bFocused && bBoundsAvailable && ExpectedRadius > 0.0f && State.Camera.Target.x == ExpectedCenter.x && State.Camera.Target.y == ExpectedCenter.y && State.Camera.Target.z == ExpectedCenter.z;
		// serial seedを変えた完成形を同じcameraで順番に調べる。
		for ( uint32 Serial : { 0u, 1u, 7u, 31u, 255u } )
		{
			// backendと同じparticle serial seedで完成形geometryを作る。
			FEffectPathGeometry Geometry;
			NEffectPath::BuildTreeLightning( CompletedPath, Definition.Seed + Serial * 7919u, 0.0f, FVector3::Zero, CompletedPath.End, 0.8f, FColor32( 255, 255, 255, 255 ), Geometry );
			bAllTreeBoundsVisible = bAllTreeBoundsVisible && !Geometry.Strips.IsEmpty();
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips )
			{
				for ( const FEffectPathGeometry::FPoint& Point : Strip.Points )
				{
					// 中心線から外側halo半幅だけずらした実geometryの点。
					const float32 Extent = Point.Width * 1.5f;
					for ( int32 Axis = 0; Axis < 3; ++Axis )
					{
						// その軸に沿う両側のhalo外縁。
						for ( int32 Sign : { -1, 1 } )
						{
							// xyz各軸の正負方向へhalo幅を加えた点。
							FVector3 WorldPoint = Point.Position;
							if ( Axis == 0 ) WorldPoint.x += Extent * static_cast<float32>( Sign );
							else if ( Axis == 1 ) WorldPoint.y += Extent * static_cast<float32>( Sign );
							else WorldPoint.z += Extent * static_cast<float32>( Sign );
							// 投影されたhalo外縁。
							FVector2 Pixel;
							float32 Depth = 0.0f;
							const bool bProjected = State.Camera.WorldToScreen( Viewport, WorldPoint, Pixel, &Depth );
							bAllTreeBoundsVisible = bAllTreeBoundsVisible && bProjected && std::isfinite( Pixel.x ) && std::isfinite( Pixel.y ) && Depth > Viewport.Near && Depth < Viewport.Far && Pixel.x >= 0.0f && Pixel.y >= 0.0f && Pixel.x <= Viewport.Width && Pixel.y <= Viewport.Height;
						}
					}
				}
			}
		}
		C.Check( bAllTreeBoundsVisible, "Depth2/BranchLength2/Angle85のTreeで複数serialの全点とhalo幅が同じfit viewportへ収まる" );
		// 一度fitした後の利用者orbitを次frameで上書きしないcamera。
		State.Camera.Orbit( 17.0f, -9.0f );
		const FEditorCamera ManualCamera = State.Camera;
		const bool bRefocused = NEffectEditorUI::ApplyPendingTreeCameraFocus( State, Viewport );
		C.Check( !bRefocused && State.Camera.Yaw == ManualCamera.Yaw && State.Camera.Pitch == ManualCamera.Pitch && State.Camera.Distance == ManualCamera.Distance && State.Camera.Target.x == ManualCamera.Target.x && State.Camera.Target.y == ManualCamera.Target.y && State.Camera.Target.z == ManualCamera.Target.z, "pendingを一度消費した後の手動orbitとzoomを保つ" );
		// 旧Ribbon文書に同じfocus要求が来た場合の従来camera。
		FParticleEffectDefinition LegacyDefinition;
		FParticleEmitterDefinition LegacyEmitter;
		LegacyEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		LegacyDefinition.Emitters.Add( LegacyEmitter );
		// 旧Lightning pathも同じ文書に含めてcameraへの非干渉を確かめる。
		FParticleEmitterDefinition LegacyLightningEmitter;
		LegacyLightningEmitter.Path.Kind = FEffectPathDefinition::EKind::Lightning;
		LegacyDefinition.Emitters.Add( LegacyLightningEmitter );
		FEffectEditorState LegacyState;
		LegacyState.Document.Reset( LegacyDefinition );
		LegacyState.Camera.Distance = 7.0f;
		LegacyState.bTreeCameraFocusPending = true;
		const bool bLegacyFocused = NEffectEditorUI::ApplyPendingTreeCameraFocus( LegacyState, Viewport );
		C.Check( !bLegacyFocused && !LegacyState.bTreeCameraFocusPending && LegacyState.Camera.Distance == 7.0f && LegacyState.Camera.Target.y == 1.0f, "旧Ribbon/Lightningのcamera位置と距離はTree focus helperで変えない" );
		// 不正なTree終点を読み込んだ時の安全なcamera fallback。
		FParticleEffectDefinition InvalidDefinition = Definition;
		InvalidDefinition.Emitters[0].Path.End.x = ( std::numeric_limits<float32>::quiet_NaN )();
		FEffectEditorState InvalidState;
		InvalidState.Document.Reset( InvalidDefinition );
		InvalidState.Camera.Distance = 8.0f;
		InvalidState.bTreeCameraFocusPending = true;
		const bool bInvalidFocused = NEffectEditorUI::ApplyPendingTreeCameraFocus( InvalidState, Viewport );
		C.Check( !bInvalidFocused && !InvalidState.bTreeCameraFocusPending && InvalidState.Camera.Distance == 8.0f && InvalidState.Camera.Target.y == 1.0f, "非有限Tree値は安全に既定cameraを保ちfocusを拒否する" );
	}
}

// EffectEditorのTree Lightning入力と修復操作をworld selftestへ登録する。
void NWorldTest::RunEffectTreeLightningInputTests( NAssetTest::FChecker& C )
{
	TestTreeCameraFit( C );
	// 呼出元ImGui contextを破棄前後で比べるため保存する。
	ImGuiContext* const PreviousContext = ImGui::GetCurrentContext();
	{
		// 独立contextの寿命をこのscopeに限定する。
		FTreeLightningInputContext Input;
		C.Check( Input.IsReady(), "Tree Lightning入力test用ImGui font atlasを作れる" );
		if ( Input.IsReady() ) TestTreeLightningPanelInput( C, Input );
	}
	C.Check( ImGui::GetCurrentContext() == PreviousContext, "独立入力contextを閉じて呼出元ImGui contextへ戻す" );
}
