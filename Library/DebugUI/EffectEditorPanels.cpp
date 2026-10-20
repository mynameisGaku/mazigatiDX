// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"

namespace
{
	// 別の入力欄へ移るときは、連続した編集の履歴をそこで区切る。
	bool TrackInputEnd( bool bChanged, TParticleEffectDocument& Document )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) Document.EndEditSession();
		return bChanged;
	}

	// 最小と最大を並べて編集する。入力されなければ範囲を変えず false を返す。
	bool DrawRange( const char* Label, FParticleRange& Range, float32 Minimum = 0.0f, float32 Maximum = 10000.0f )
	{
		// 別のフィールドを配列として扱わず、入力用の二つの値へ写す。
		float32 Values[2] = { Range.Min, Range.Max };
		if ( !ImGui::DragFloat2( Label, Values, 0.01f, Minimum, Maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp ) ) return false;
		Range.Min = Values[0];
		Range.Max = Values[1];
		return true;
	}

	// 色を 0..1 の入力へ写し、編集された場合だけ各成分を 0..255 に戻す。
	bool DrawColor( const char* Label, FColor32& Color )
	{
		// 色の入力に使う、赤・緑・青・不透明度。
		float32 Channels[4] = { Color.r / 255.0f, Color.g / 255.0f, Color.b / 255.0f, Color.a / 255.0f };
		if ( !ImGui::ColorEdit4( Label, Channels, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf ) ) return false;
		// 整数の色へ変換できない入力は、元の色のままにする。
		for ( const float32 Channel : Channels )
		{
			if ( !std::isfinite( Channel ) ) return false;
		}
		Color = FColor32( static_cast<uint8>( std::clamp( Channels[0], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( std::clamp( Channels[1], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( std::clamp( Channels[2], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( std::clamp( Channels[3], 0.0f, 1.0f ) * 255.0f + 0.5f ) );
		return true;
	}

	// 定義全体の設定をコピー上で編集し、変更があれば既存の履歴へ一度だけ記録する。
	void DrawProperties( FEffectEditorState& S )
	{
		// 編集開始で元の文書が更新されても、画面の入力はこのコピーだけを使う。
		FString Name = S.Document.Get().Name;
		// エミッターが生成を続ける秒数。0 は停止するまで続ける。
		float32 Duration = S.Document.Get().Duration;
		// 継続秒数を過ぎたあと、定義の先頭へ戻すか。
		bool bLoop = S.Document.Get().bLoop;
		// 再現可能な粒子の生成に使うシード。
		uint32 Seed = S.Document.Get().Seed;
		// このフレームで全体の設定が変更されたか。
		bool bChanged = TrackInputEnd( NDebugUI::InputString( "Name", Name ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Duration (s)", &Duration, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		ImGui::TextDisabled( "0 seconds: emit until stopped" );
		bChanged |= TrackInputEnd( ImGui::Checkbox( "Loop", &bLoop ), S.Document );
		bChanged |= TrackInputEnd( ImGui::InputScalar( "Seed", ImGuiDataType_U32, &Seed ), S.Document );
		ImGui::TextDisabled( "Format version: %d", S.Document.Get().Version );
		if ( bChanged ) NParticleEdit::SetProperties( S.Document, Name, Duration, bLoop, Seed, "effect-properties" );
	}

	// 一覧の選択と追加・複製・削除・移動を扱う。配列の変更後は元の参照を使わない。
	void DrawEmitterList( FEffectEditorState& S )
	{
		if ( !S.Document.Get().Emitters.IsValidIndex( S.SelectedEmitter ) ) S.SelectedEmitter = S.Document.Get().Emitters.IsEmpty() ? INDEX_NONE : 0;
		if ( ImGui::BeginListBox( "##Emitters", ImVec2( -1.0f, 112.0f ) ) )
		{
			// 一覧の表示中は配列のサイズを変えず、選択だけを更新する。
			for ( int32 Index = 0; Index < S.Document.Get().Emitters.Num(); ++Index )
			{
				// 同名のエミッターも、配列上の番号で区別する表示名。
				const FString Label = FString::Printf( "%d  %s", Index + 1, *S.Document.Get().Emitters[Index].Name );
				ImGui::PushID( Index );
				if ( ImGui::Selectable( UI_TEXT( Label ), S.SelectedEmitter == Index ) )
				{
					S.Document.EndEditSession();
					S.SelectedEmitter = Index;
				}
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}
		ImGui::BeginDisabled( S.Document.Get().Emitters.Num() >= FParticleEffectDefinition::MaxEmitters );
		if ( ImGui::Button( "Add emitter" ) ) S.SelectedEmitter = NParticleEdit::AddEmitter( S.Document );
		ImGui::SameLine();
		ImGui::BeginDisabled( !S.Document.Get().Emitters.IsValidIndex( S.SelectedEmitter ) );
		if ( ImGui::Button( "Duplicate" ) ) S.SelectedEmitter = NParticleEdit::DuplicateEmitter( S.Document, S.SelectedEmitter );
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::BeginDisabled( !S.Document.Get().Emitters.IsValidIndex( S.SelectedEmitter ) );
		if ( ImGui::Button( "Delete emitter" ) && NParticleEdit::RemoveEmitter( S.Document, S.SelectedEmitter ) ) S.SelectedEmitter = std::min( S.SelectedEmitter, S.Document.Get().Emitters.Num() - 1 );
		ImGui::SameLine();
		ImGui::BeginDisabled( S.SelectedEmitter <= 0 );
		if ( ImGui::Button( "Up" ) && NParticleEdit::MoveEmitter( S.Document, S.SelectedEmitter, S.SelectedEmitter - 1 ) ) --S.SelectedEmitter;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( S.SelectedEmitter < 0 || S.SelectedEmitter + 1 >= S.Document.Get().Emitters.Num() );
		if ( ImGui::Button( "Down" ) && NParticleEdit::MoveEmitter( S.Document, S.SelectedEmitter, S.SelectedEmitter + 1 ) ) ++S.SelectedEmitter;
		ImGui::EndDisabled();
		ImGui::EndDisabled();
	}

	// 一括生成する時刻と個数を編集する。削除は表示の走査を終えてから行う。
	bool DrawBursts( FParticleEmitterDefinition& Emitter, TParticleEffectDocument& Document )
	{
		// このフレームで生成時刻や個数が変わったか。
		bool bChanged = false;
		if ( ImGui::Button( "Add burst" ) )
		{
			// 末尾へ加え、次回の時刻順への整列でも既存行が移らないようにする。
			FParticleBurst Added;
			Added.Time = Emitter.Bursts.IsEmpty() ? 0.0f : Emitter.Bursts.Last().Time;
			Emitter.Bursts.Add( Added );
			bChanged = true;
		}
		// 走査のあとで削除する行。未指定なら配列を維持する。
		int32 RemoveIndex = INDEX_NONE;
		// 時刻順で表示している、各一括生成の設定。
		for ( int32 Index = 0; Index < Emitter.Bursts.Num(); ++Index )
		{
			ImGui::PushID( Index );
			// 更新対象の一括生成。ループの途中では配列を増減しない。
			FParticleBurst& Burst = Emitter.Bursts[Index];
			// 編集中の行が隣の行と入れ替わらないよう、前後の時刻を境界にする。
			const float32 Earliest = Index > 0 ? std::max( Emitter.Bursts[Index - 1].Time, 0.0f ) : 0.0f;
			// 最後の行は、有限の正の時刻を任意に指定できる。
			const float32 Latest = Index + 1 < Emitter.Bursts.Num() ? std::max( Emitter.Bursts[Index + 1].Time, Earliest ) : ( std::numeric_limits<float32>::max )();
			ImGui::TextDisabled( "Burst %d", Index + 1 );
			if ( TrackInputEnd( ImGui::DragFloat( "Time (s)", &Burst.Time, 0.01f, 0.0f, 0.0f, "%.3f" ), Document ) )
			{
				Burst.Time = std::clamp( Burst.Time, Earliest, Latest );
				bChanged = true;
			}
			bChanged |= TrackInputEnd( ImGui::DragInt( "Count", &Burst.Count, 1.0f, 0, ( std::numeric_limits<int32>::max )(), "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
			if ( ImGui::SmallButton( "Remove burst" ) ) RemoveIndex = Index;
			ImGui::PopID();
		}
		if ( RemoveIndex != INDEX_NONE )
		{
			Emitter.Bursts.RemoveAt( RemoveIndex );
			bChanged = true;
		}
		return bChanged;
	}

	// リボンと雷の形を、エミッターのコピー上で編集する。未選択の種類の設定は保持する。
	bool DrawPathSettings( FParticleEmitterDefinition& Emitter, TParticleEffectDocument& Document )
	{
		ImGui::SeparatorText( "Path" );
		// 既存の板、帯、枝分かれする雷、固定形状のTree lightningを並べた表示名。
		static const char* KindNames[] = { "Sprites", "Ribbon", "Branch lightning", "Tree lightning (fixed path)" };
		// 編集中の種類を、選択肢の番号へ写す。
		int32 Kind = static_cast<int32>( Emitter.Path.Kind );
		// このフレームに、種類または形の設定が変わったか。
		bool bChanged = false;
		if ( ImGui::Combo( "Draw as", &Kind, KindNames, 4 ) )
		{
			Emitter.Path.Kind = static_cast<FEffectPathDefinition::EKind>( Kind );
			bChanged = true;
		}
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon )
		{
			ImGui::TextWrapped( "Connects particles in birth order. Move the emitter to leave a trail." );
			bChanged |= TrackInputEnd( ImGui::DragFloat( "Break distance (m)", &Emitter.Path.BreakDistance, 0.01f, 0.001f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
		}
		// Treeの共通形状値が不正なら、同じ関数末尾のUV repeat入力も隠す状態。
		bool bTreeCommonPathValid = true;
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning || Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning )
		{
			ImGui::TextWrapped( "The end position is relative to the emitter. Jitter and branch length are ratios of the main bolt length." );
			// Treeの共通形状値をImGuiへ渡す前に有限性と許容範囲を調べる。
			bTreeCommonPathValid = Emitter.Path.Kind != FEffectPathDefinition::EKind::TreeLightning || ( std::isfinite( Emitter.Path.End.x ) && std::isfinite( Emitter.Path.End.y ) && std::isfinite( Emitter.Path.End.z ) && ( Emitter.Path.End.x != 0.0f || Emitter.Path.End.y != 0.0f || Emitter.Path.End.z != 0.0f ) && std::fabs( Emitter.Path.End.x ) <= 10000.0f && std::fabs( Emitter.Path.End.y ) <= 10000.0f && std::fabs( Emitter.Path.End.z ) <= 10000.0f && Emitter.Path.Segments >= 2 && Emitter.Path.Segments <= 128 && Emitter.Path.Branches >= 0 && Emitter.Path.Branches <= 16 && std::isfinite( Emitter.Path.Jitter ) && Emitter.Path.Jitter >= 0.0f && Emitter.Path.Jitter <= 1.0f && std::isfinite( Emitter.Path.BranchLength ) && Emitter.Path.BranchLength >= 0.0f && Emitter.Path.BranchLength <= 2.0f && std::isfinite( Emitter.Path.RefreshHz ) && Emitter.Path.RefreshHz >= 0.0f && std::isfinite( Emitter.Path.UVRepeat ) && Emitter.Path.UVRepeat > 0.0f && Emitter.Path.UVRepeat <= 10000.0f && std::isfinite( Emitter.Path.BreakDistance ) && Emitter.Path.BreakDistance > 0.0f );
			if ( !bTreeCommonPathValid ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Common tree path values are invalid; reset them before editing." );
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning && ImGui::SmallButton( "Reset tree path values" ) )
			{
				// 共通パス項目だけ既定値へ戻し、Tree固有の8項目と他の材質を保つ。
				const FEffectPathDefinition Defaults;
				Emitter.Path.End = Defaults.End;
				Emitter.Path.Segments = Defaults.Segments;
				Emitter.Path.Branches = Defaults.Branches;
				Emitter.Path.Jitter = Defaults.Jitter;
				Emitter.Path.BranchLength = Defaults.BranchLength;
				Emitter.Path.RefreshHz = Defaults.RefreshHz;
				Emitter.Path.UVRepeat = Defaults.UVRepeat;
				Emitter.Path.BreakDistance = Defaults.BreakDistance;
				bChanged = true;
			}
			if ( bTreeCommonPathValid )
			{
				bChanged |= TrackInputEnd( ImGui::DragFloat3( "End position (m)", &Emitter.Path.End.x, 0.01f, -10000.0f, 10000.0f, "%.3f" ), Document );
				bChanged |= TrackInputEnd( ImGui::DragInt( "Segments", &Emitter.Path.Segments, 1.0f, 2, 128, "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
				bChanged |= TrackInputEnd( ImGui::DragInt( "Branches", &Emitter.Path.Branches, 1.0f, 0, 16, "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
				bChanged |= TrackInputEnd( ImGui::DragFloat( "Jitter ratio", &Emitter.Path.Jitter, 0.005f, 0.0f, Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ? 1.0f : 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
				bChanged |= TrackInputEnd( ImGui::DragFloat( "Branch length ratio", &Emitter.Path.BranchLength, 0.005f, 0.0f, Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ? 2.0f : 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
			}
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning )
			{
				bChanged |= TrackInputEnd( ImGui::DragFloat( "Refresh (Hz)", &Emitter.Path.RefreshHz, 0.1f, 0.0f, 120.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ), Document );
				ImGui::TextDisabled( "0 Hz: keep the same bolt shape" );
			}
			else
			{
				ImGui::TextWrapped( "The jagged tree shape is fixed for each burst; branches grow from the trunk and can split again." );
				// 編集欄へ渡す前に8項目の範囲と組合せを検査する。
				const TArray<FString> TreeIssues = Emitter.Path.Tree.Validate();
				// 不正値を入力欄へ渡さず、Tree専用Resetで修復できるようにする。
				if ( !TreeIssues.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Tree settings are invalid; reset them before editing." );
				if ( ImGui::SmallButton( "Reset tree settings" ) ) { Emitter.Path.Tree = FEffectLightningTreeDefinition(); bChanged = true; }
				if ( TreeIssues.IsEmpty() )
				{
					// Tree Lightningの段数、枝数、角度、成長と発光設定。
					FEffectLightningTreeDefinition& Tree = Emitter.Path.Tree;
					bChanged |= TrackInputEnd( ImGui::DragInt( "Tree depth", &Tree.Depth, 1.0f, 0, 2, "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragInt( "Children per branch", &Tree.ChildBranches, 1.0f, 0, 4, "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragFloat( "Branch angle (deg)", &Tree.BranchAngleDegrees, 0.5f, 5.0f, 85.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragFloat( "Growth seconds", &Tree.GrowthSeconds, 0.005f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragFloat( "Branch delay (s)", &Tree.BranchDelaySeconds, 0.001f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragInt( "Stroke count", &Tree.StrokeCount, 1.0f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragFloat( "Stroke interval (s)", &Tree.StrokeInterval, 0.005f, std::max( 0.001f, Tree.StrokeSeconds ), 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
					bChanged |= TrackInputEnd( ImGui::DragFloat( "Stroke duration (s)", &Tree.StrokeSeconds, 0.001f, 0.0001f, Tree.StrokeInterval, "%.4f", ImGuiSliderFlags_AlwaysClamp ), Document );
				}
			}
		}
		if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None && ( Emitter.Path.Kind != FEffectPathDefinition::EKind::TreeLightning || bTreeCommonPathValid ) ) bChanged |= TrackInputEnd( ImGui::DragFloat( "UV repeat (/m)", &Emitter.Path.UVRepeat, 0.01f, 0.001f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), Document );
		return bChanged;
	}

	// 選んだエミッターの全項目をコピー上で編集する。変更は最後に一度だけ文書へ反映する。
	void DrawEmitterSettings( FEffectEditorState& S )
	{
		if ( !S.Document.Get().Emitters.IsValidIndex( S.SelectedEmitter ) ) return;
		// このフレームだけ編集するコピー。履歴を積んだ後も古い文書を参照しない。
		FParticleEmitterDefinition Emitter = S.Document.Get().Emitters[S.SelectedEmitter];
		// どれかの入力が変化した場合だけ、文書を更新する。
		bool bChanged = TrackInputEnd( NDebugUI::InputString( "Emitter name", Emitter.Name ), S.Document );
		bChanged |= TrackInputEnd( NDebugUI::FileCombo( "Image", Emitter.Texture, S.TextureFiles, "(soft dot)" ), S.Document );
		bChanged |= TrackInputEnd( NDebugUI::InputString( "Image path", Emitter.Texture ), S.Document );
		if ( ImGui::SmallButton( "Use soft dot" ) )
		{
			Emitter.Texture.Empty();
			bChanged = true;
		}
		bChanged |= NEffectEditorUI::DrawFlipbookPanel( S, Emitter );
		bChanged |= NEffectEditorUI::DrawMeshPanel( S, Emitter );
		// 定義の列挙値と同じ順序にある、合成方法の表示名。
		static const char* BlendNames[] = { "Alpha", "Add" };
		// 画面が編集中に使う合成方法の番号。
		int32 Blend = static_cast<int32>( Emitter.Blend );
		if ( ImGui::Combo( "Blend", &Blend, BlendNames, 2 ) )
		{
			Emitter.Blend = static_cast<EParticleBlend>( Blend );
			bChanged = true;
		}
		// 生まれた位置に残すか、エフェクトの動きについて来るか。
		static const char* SpaceNames[] = { "World", "Local" };
		// 画面が編集中に使う空間の番号。
		int32 Space = static_cast<int32>( Emitter.Space );
		if ( ImGui::Combo( "Space", &Space, SpaceNames, 2 ) )
		{
			Emitter.Space = static_cast<EParticleSpace>( Space );
			bChanged = true;
		}
		if ( Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None ) ImGui::TextDisabled( "Mesh orientation is fixed; Orientation is ignored." );
		else if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::None )
		{
			// カメラに合わせる向きと、固定する向き。
			static const char* OrientNames[] = { "Camera", "Camera Y", "Fixed" };
			// 画面が編集中に使う板の向きの番号。
			int32 Orient = static_cast<int32>( Emitter.Orient );
			if ( ImGui::Combo( "Orientation", &Orient, OrientNames, 3 ) )
			{
				Emitter.Orient = static_cast<EParticleOrient>( Orient );
				bChanged = true;
			}
		}
		else ImGui::TextDisabled( "Path strips face the camera" );
		bChanged |= DrawPathSettings( Emitter, S.Document );
		bChanged |= NEffectEditorUI::DrawMaterialPanel( S, Emitter );
		ImGui::SeparatorText( "Emission" );
		bChanged |= TrackInputEnd( ImGui::DragInt( "Particle limit", &Emitter.MaxParticles, 1.0f, 1, FParticleEffectDefinition::MaxParticlesPerEmitter, "%d", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Delay (s)", &Emitter.Delay, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Rate (/s)", &Emitter.Rate, 0.1f, 0.0f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		if ( ImGui::TreeNodeEx( "Bursts", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			bChanged |= DrawBursts( Emitter, S.Document );
			ImGui::TreePop();
		}
		ImGui::TextDisabled( "Ranges: minimum / maximum" );
		bChanged |= TrackInputEnd( DrawRange( "Lifetime (s)", Emitter.Lifetime, 0.001f ), S.Document );
		bChanged |= TrackInputEnd( DrawRange( "Speed (m/s)", Emitter.Speed ), S.Document );
		ImGui::SeparatorText( "Shape and motion" );
		// 円錐・球・箱の生成位置と向きを選ぶ表示名。
		static const char* ShapeNames[] = { "Cone", "Sphere", "Box" };
		// 画面が編集中に使う形の番号。
		int32 Shape = static_cast<int32>( Emitter.Shape );
		if ( ImGui::Combo( "Shape", &Shape, ShapeNames, 3 ) )
		{
			Emitter.Shape = static_cast<EParticleShape>( Shape );
			bChanged = true;
		}
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Radius (m)", &Emitter.ShapeRadius, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Cone angle (deg)", &Emitter.ConeAngle, 0.1f, 0.0f, 180.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat3( "Box size (m)", &Emitter.BoxSize.x, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat3( "Direction", &Emitter.Direction.x, 0.01f, -10000.0f, 10000.0f, "%.3f" ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat3( "Gravity", &Emitter.Gravity.x, 0.01f, -10000.0f, 10000.0f, "%.3f" ), S.Document );
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Drag", &Emitter.Drag, 0.01f, 0.0f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), S.Document );
		ImGui::SeparatorText( "Appearance" );
		bChanged |= TrackInputEnd( DrawRange( "Start size (m)", Emitter.StartSize ), S.Document );
		bChanged |= TrackInputEnd( DrawRange( "End size (m)", Emitter.EndSize ), S.Document );
		bChanged |= TrackInputEnd( DrawColor( "Start color", Emitter.StartColor ), S.Document );
		bChanged |= TrackInputEnd( DrawColor( "End color", Emitter.EndColor ), S.Document );
		// Appearance入力だけの履歴session名を選ぶ変更状態。
		const bool bAppearanceChanged = NEffectEditorUI::DrawAppearancePanel( S, Emitter );
		bChanged |= bAppearanceChanged;
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::None )
		{
			bChanged |= TrackInputEnd( DrawRange( "Start angle (deg)", Emitter.StartAngle, -360000.0f, 360000.0f ), S.Document );
			bChanged |= TrackInputEnd( DrawRange( "Spin (deg/s)", Emitter.Spin, -360000.0f, 360000.0f ), S.Document );
		}
		if ( bAppearanceChanged ) NParticleEdit::SetEmitter( S.Document, S.SelectedEmitter, Emitter, "emitter-appearance" );
		else if ( bChanged ) NParticleEdit::SetEmitter( S.Document, S.SelectedEmitter, Emitter, "emitter-settings" );
	}
}

namespace
{
	// 中心・半径で渡されたgeometry sphereを、既存boundsへ包含する。
	// float中心へ丸めた際の誤差も含め、失敗時はboundsを変更しない。
	bool AddCameraBoundsSphere( FVector3 SphereCenter, float64 SphereRadius, FVector3& BoundsCenter, float64& BoundsRadius, bool& bHasBounds )
	{
		if ( !std::isfinite( SphereCenter.x ) || !std::isfinite( SphereCenter.y ) || !std::isfinite( SphereCenter.z ) || !std::isfinite( SphereRadius ) || SphereRadius <= 0.0 ) return false;
		if ( !bHasBounds )
		{
			BoundsCenter = SphereCenter;
			BoundsRadius = SphereRadius;
			bHasBounds = true;
			return true;
		}
		// 中心差をdoubleで計算し、距離の二乗overflowを避ける。
		// 3軸の中心差。
		const float64 DeltaX = static_cast<float64>( SphereCenter.x ) - BoundsCenter.x;
		const float64 DeltaY = static_cast<float64>( SphereCenter.y ) - BoundsCenter.y;
		const float64 DeltaZ = static_cast<float64>( SphereCenter.z ) - BoundsCenter.z;
		// sphere中心間のdouble距離。
		const float64 CenterDistance = std::sqrt( DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ );
		if ( !std::isfinite( CenterDistance ) ) return false;
		if ( CenterDistance + SphereRadius <= BoundsRadius ) return true;
		if ( CenterDistance + BoundsRadius <= SphereRadius )
		{
			BoundsCenter = SphereCenter;
			BoundsRadius = SphereRadius;
			return true;
		}
		if ( CenterDistance <= 1.0e-12 )
		{
			BoundsRadius = std::max( BoundsRadius, SphereRadius );
			return true;
		}
		// 二つのsphereを両方含む最小sphereの中心と半径。
		// 二つを含めるための新半径と中心移動率。
		const float64 NewRadius = ( CenterDistance + BoundsRadius + SphereRadius ) * 0.5;
		const float64 Move = ( NewRadius - BoundsRadius ) / CenterDistance;
		// 新しい球中心の3軸座標。
		const float64 NewX = static_cast<float64>( BoundsCenter.x ) + DeltaX * Move;
		const float64 NewY = static_cast<float64>( BoundsCenter.y ) + DeltaY * Move;
		const float64 NewZ = static_cast<float64>( BoundsCenter.z ) + DeltaZ * Move;
		if ( !std::isfinite( NewRadius ) || !std::isfinite( NewX ) || !std::isfinite( NewY ) || !std::isfinite( NewZ ) || NewRadius > ( std::numeric_limits<float32>::max )() ) return false;
		// float中心へ丸めた差分も半径へ足し、包含を維持する。
		const FVector3 RoundedCenter( static_cast<float32>( NewX ), static_cast<float32>( NewY ), static_cast<float32>( NewZ ) );
		// float丸めで失う3軸の差。
		const float64 RoundX = NewX - RoundedCenter.x;
		const float64 RoundY = NewY - RoundedCenter.y;
		const float64 RoundZ = NewZ - RoundedCenter.z;
		const float64 RoundError = std::sqrt( RoundX * RoundX + RoundY * RoundY + RoundZ * RoundZ );
		if ( !std::isfinite( RoundError ) || NewRadius + RoundError > ( std::numeric_limits<float32>::max )() ) return false;
		BoundsCenter = RoundedCenter;
		BoundsRadius = NewRadius + RoundError;
		return true;
	}

	// bounds計算に使う範囲値が有限で昇順か調べる。bRequirePositiveMinimumで寿命下限も確認する。
	bool IsValidCameraRange( const FParticleRange& Range, bool bRequirePositiveMinimum )
	{
		return std::isfinite( Range.Min ) && std::isfinite( Range.Max ) && Range.Min <= Range.Max && ( bRequirePositiveMinimum ? Range.Min > 0.0f : Range.Min >= 0.0f );
	}

	// bIncludeMeshがtrueなら固定meshも加え、原点・identity transformのboundsへfocusする。移動previewの円運動は対象外。
	bool FocusCameraOnGeometry( FEffectEditorState& S, const FEditorViewport& Viewport, bool bIncludeMesh )
	{
		if ( !Viewport.IsValid() || !std::isfinite( Viewport.FovY ) || Viewport.FovY <= 0.0f || Viewport.FovY >= 3.14159265f || !std::isfinite( Viewport.Near ) || !std::isfinite( Viewport.Far ) || Viewport.Near <= 0.0f || Viewport.Far <= Viewport.Near ) return false;
		// 複数emitterの保守sphereを包む中心。
		FVector3 BoundsCenter = FVector3::Zero;
		// 複数emitterの保守sphereを包むdouble半径。
		float64 BoundsRadius = 0.0;
		// Treeまたはmesh sphereを一つ以上追加したか。
		bool bHasBounds = false;
		// 空間範囲を調べる定義全体。
		const FParticleEffectDefinition& Definition = S.Document.Get();
		for ( int32 EmitterIndex = 0; EmitterIndex < Definition.Emitters.Num(); ++EmitterIndex )
		{
			// 今回のboundsへ加える定義。
			const FParticleEmitterDefinition& Emitter = Definition.Emitters[EmitterIndex];
			if ( bIncludeMesh && Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None || Emitter.Flipbook.bEnabled ) ) return false;
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning )
			{
				if ( !Emitter.Path.Validate().IsEmpty() || !Emitter.Appearance.Size.Validate().IsEmpty() ) return false;
				// 粒子幅に使う開始時と終了時の最大値。
				const float32 BaseSize = std::max( Emitter.StartSize.Max, Emitter.EndSize.Max );
				if ( !IsValidCameraRange( Emitter.StartSize, false ) || !IsValidCameraRange( Emitter.EndSize, false ) || !std::isfinite( BaseSize ) || BaseSize <= 0.0f ) return false;
				// Size曲線の最大倍率。空曲線は既定の一倍を使う。
				float32 AppearanceScale = 1.0f;
				if ( !Emitter.Appearance.Size.Keys.IsEmpty() )
				{
					AppearanceScale = 0.0f;
					for ( const FEffectScalarCurve::FKey& Key : Emitter.Appearance.Size.Keys )
					{
						// Tree particle幅へ掛ける倍率候補。
						if ( !std::isfinite( Key.Value ) || Key.Value < 0.0f || Key.Value > 32.0f ) return false;
						AppearanceScale = std::max( AppearanceScale, Key.Value );
					}
				}
				// 完成形treeを使い、粒子halo分も加えるPath copy。
				FEffectPathDefinition CompletedPath = Emitter.Path;
				CompletedPath.Tree.GrowthSeconds = 0.0f;
				CompletedPath.Tree.BranchDelaySeconds = 0.0f;
				// 最も太いparticle幅に対する最大halo半幅。
				const float32 HaloPadding = BaseSize * AppearanceScale * 1.5f;
				// Tree helperが返すemitter sphereの中心。
				FVector3 EmitterCenter = FVector3::Zero;
				// Tree helperが返すemitter sphereの半径。
				float32 EmitterRadius = 0.0f;
				if ( !std::isfinite( HaloPadding ) || HaloPadding <= 0.0f || !NEffectPath::GetTreeLightningBounds( CompletedPath, FVector3::Zero, CompletedPath.End, HaloPadding, EmitterCenter, EmitterRadius ) || !AddCameraBoundsSphere( EmitterCenter, EmitterRadius, BoundsCenter, BoundsRadius, bHasBounds ) ) return false;
				continue;
			}
			if ( !bIncludeMesh || Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::None ) continue;
			if ( !Emitter.Mesh.IsValidSettings() || Emitter.Path.Kind != FEffectPathDefinition::EKind::None || Emitter.Flipbook.bEnabled || !Emitter.Flipbook.Validate().IsEmpty() || !Emitter.Appearance.Size.Validate().IsEmpty() ) return false;
			if ( Emitter.Shape != EParticleShape::Cone && Emitter.Shape != EParticleShape::Sphere && Emitter.Shape != EParticleShape::Box ) return false;
			if ( !IsValidCameraRange( Emitter.StartSize, false ) || !IsValidCameraRange( Emitter.EndSize, false ) || !IsValidCameraRange( Emitter.Lifetime, true ) || !IsValidCameraRange( Emitter.Speed, false ) || !std::isfinite( Emitter.ShapeRadius ) || Emitter.ShapeRadius < 0.0f || !std::isfinite( Emitter.BoxSize.x ) || !std::isfinite( Emitter.BoxSize.y ) || !std::isfinite( Emitter.BoxSize.z ) || Emitter.BoxSize.x < 0.0f || Emitter.BoxSize.y < 0.0f || Emitter.BoxSize.z < 0.0f || !std::isfinite( Emitter.Gravity.x ) || !std::isfinite( Emitter.Gravity.y ) || !std::isfinite( Emitter.Gravity.z ) || !std::isfinite( Emitter.Drag ) || Emitter.Drag < 0.0f ) return false;
			if ( !std::isfinite( Emitter.ConeAngle ) || Emitter.ConeAngle < 0.0f || Emitter.ConeAngle > 180.0f || !std::isfinite( Emitter.Direction.x ) || !std::isfinite( Emitter.Direction.y ) || !std::isfinite( Emitter.Direction.z ) ) return false;
			// doubleで計算したspawn軸の長さ二乗。
			const float64 DirectionLengthSquared = static_cast<float64>( Emitter.Direction.x ) * Emitter.Direction.x + static_cast<float64>( Emitter.Direction.y ) * Emitter.Direction.y + static_cast<float64>( Emitter.Direction.z ) * Emitter.Direction.z;
			if ( !std::isfinite( DirectionLengthSquared ) || DirectionLengthSquared < 1.0e-12 ) return false;
			// meshの最大辺とAppearance倍率を有限精度で求める。
			float64 AppearanceScale = 1.0;
			if ( !Emitter.Appearance.Size.Keys.IsEmpty() )
			{
				AppearanceScale = 0.0;
				for ( const FEffectScalarCurve::FKey& Key : Emitter.Appearance.Size.Keys )
				{
					// size倍率の上限候補。
					if ( !std::isfinite( Key.Value ) || Key.Value < 0.0f || Key.Value > 32.0f ) return false;
					AppearanceScale = std::max( AppearanceScale, static_cast<float64>( Key.Value ) );
				}
			}
			// start/end sizeの最大値。
			const float64 BaseSize = std::max( static_cast<float64>( Emitter.StartSize.Max ), static_cast<float64>( Emitter.EndSize.Max ) );
			// Planeは半辺の対角、Ringは外半径の係数。
			const float64 ShapeFactor = Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::Plane ? std::sqrt( 0.5 ) : 0.5;
			// sizeとAppearanceを反映したmesh半径。
			const float64 MeshRadius = BaseSize * AppearanceScale * ShapeFactor;
			// Cone・Sphereは半径、Boxは半対角をspawn範囲へ使う。
			float64 SpawnRadius = Emitter.Shape == EParticleShape::Box ? 0.5 * std::sqrt( static_cast<float64>( Emitter.BoxSize.x ) * Emitter.BoxSize.x + static_cast<float64>( Emitter.BoxSize.y ) * Emitter.BoxSize.y + static_cast<float64>( Emitter.BoxSize.z ) * Emitter.BoxSize.z ) : static_cast<float64>( Emitter.ShapeRadius );
			// 寿命全体の移動に使う最大時間。
			const float64 Lifetime = Emitter.Lifetime.Max;
			// 重力加速度ベクトルの長さ。
			const float64 GravityLength = std::sqrt( static_cast<float64>( Emitter.Gravity.x ) * Emitter.Gravity.x + static_cast<float64>( Emitter.Gravity.y ) * Emitter.Gravity.y + static_cast<float64>( Emitter.Gravity.z ) * Emitter.Gravity.z );
			// speedと重力が寿命中に作る保守移動距離。
			const float64 TravelRadius = static_cast<float64>( Emitter.Speed.Max ) * Lifetime + GravityLength * Lifetime * Lifetime;
			// mesh外周、spawn、移動の合計保守半径。
			const float64 EmitterRadius = MeshRadius + SpawnRadius + TravelRadius;
			if ( !std::isfinite( EmitterRadius ) || EmitterRadius <= 0.0 || EmitterRadius > ( std::numeric_limits<float32>::max )() || !AddCameraBoundsSphere( FVector3::Zero, EmitterRadius, BoundsCenter, BoundsRadius, bHasBounds ) ) return false;
		}
		if ( !bHasBounds || !std::isfinite( BoundsCenter.x ) || !std::isfinite( BoundsCenter.y ) || !std::isfinite( BoundsCenter.z ) || !std::isfinite( BoundsRadius ) || BoundsRadius <= 0.0 || BoundsRadius > FEditorCamera::MaxDistance ) return false;
		// doubleで計算した範囲をcamera APIのfloatへ安全に渡す。
		const float32 SafeBoundsRadius = static_cast<float32>( BoundsRadius );
		if ( !std::isfinite( SafeBoundsRadius ) || SafeBoundsRadius <= 0.0f ) return false;
		// focusで収まらない異常値を検出できるよう、変更前cameraを保存する。
		const FEditorCamera PreviousCamera = S.Camera;
		S.Camera.Focus( BoundsCenter, SafeBoundsRadius, Viewport );
		// 球中心の奥行きと投影位置。
		FVector2 CenterPixel;
		// カメラ前方に沿った球中心までの距離。
		float32 CenterDepth = 0.0f;
		if ( !S.Camera.WorldToScreen( Viewport, BoundsCenter, CenterPixel, &CenterDepth ) || !std::isfinite( CenterDepth ) || !std::isfinite( CenterPixel.x ) || !std::isfinite( CenterPixel.y ) ) { S.Camera = PreviousCamera; return false; }
		if ( CenterDepth - SafeBoundsRadius <= Viewport.Near || CenterDepth + SafeBoundsRadius >= Viewport.Far ) { S.Camera = PreviousCamera; return false; }
		// 縦横の半視野角のうち狭い方を使った、球の角度方向の収まり。
		const float32 HalfFovY = Viewport.FovY * 0.5f;
		const float32 HalfFovX = std::atan( std::tan( HalfFovY ) * Viewport.GetAspect() );
		const float32 LimitingHalfFov = std::min( HalfFovX, HalfFovY );
		if ( !std::isfinite( LimitingHalfFov ) || LimitingHalfFov <= 0.0f || SafeBoundsRadius > CenterDepth * std::sin( LimitingHalfFov ) * 0.92f || CenterPixel.x < 1.0f || CenterPixel.y < 1.0f || CenterPixel.x > Viewport.Width - 1.0f || CenterPixel.y > Viewport.Height - 1.0f ) { S.Camera = PreviousCamera; return false; }
		return true;
	}
}

// Tree専用の完成形boundsを既存どおり使い、無効・空なら元cameraを保つ。
bool NEffectEditorUI::FocusCameraOnTree( FEffectEditorState& S, const FEditorViewport& Viewport )
{
	return FocusCameraOnGeometry( S, Viewport, false );
}

// Treeと固定meshを包むboundsへcameraを合わせ、無効・空なら元cameraを保つ。
bool NEffectEditorUI::FocusCameraOnEffectGeometry( FEffectEditorState& S, const FEditorViewport& Viewport )
{
	return FocusCameraOnGeometry( S, Viewport, true );
}

// pending focusを消費して一度だけ適用し、その後の手動camera操作を保護する。
bool NEffectEditorUI::ApplyPendingTreeCameraFocus( FEffectEditorState& S, const FEditorViewport& Viewport )
{
	if ( !S.bTreeCameraFocusPending ) return false;
	S.bTreeCameraFocusPending = false;
	return FocusCameraOnTree( S, Viewport );
}

// pending focusを消費して一度だけ適用し、その後の手動camera操作を保護する。
bool NEffectEditorUI::ApplyPendingEffectCameraFocus( FEffectEditorState& S, const FEditorViewport& Viewport )
{
	if ( !S.bEffectGeometryCameraFocusPending ) return false;
	S.bEffectGeometryCameraFocusPending = false;
	S.bTreeCameraFocusPending = false;
	return FocusCameraOnEffectGeometry( S, Viewport );
}

// 文書にTree・meshがある場合だけopen/reset後のfocusを一度保留する。
void NEffectEditorUI::SetPendingEffectCameraFocus( FEffectEditorState& S, const FParticleEffectDefinition& Definition )
{
	S.bTreeCameraFocusPending = false;
	S.bEffectGeometryCameraFocusPending = false;
	for ( const FParticleEmitterDefinition& Emitter : Definition.Emitters )
	{
		// pending判定に使う現在のemitter。
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ) S.bTreeCameraFocusPending = true;
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning || Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None ) S.bEffectGeometryCameraFocusPending = true;
	}
}

// 全体設定と選択したエミッターを、親が用意したスクロール領域へ表示する。
void NEffectEditorUI::DrawDefinitionPanel( FEffectEditorState& S )
{
	ImGui::PushItemWidth( -135.0f );
	ImGui::SeparatorText( "Effect" );
	DrawProperties( S );
	ImGui::SeparatorText( "Emitters" );
	DrawEmitterList( S );
	ImGui::Separator();
	DrawEmitterSettings( S );
	ImGui::PopItemWidth();
}

// 試し再生の操作とカメラ入力を扱い、専用の描画先を画像として表示する。
void NEffectEditorUI::DrawPreview( FEffectEditorState& S )
{
	if ( ImGui::Button( S.bPlaying ? "Pause" : "Play" ) ) S.bPlaying = !S.bPlaying;
	ImGui::SameLine();
	if ( ImGui::Button( "Restart" ) ) NDebugUI::RestartEffectPreview( S );
	ImGui::SameLine();
	ImGui::Checkbox( "Repeat preview", &S.bRepeatPreview );
	ImGui::SameLine();
	ImGui::TextDisabled( "%d particles", S.Playback.IsValid() ? S.Playback->GetNumParticles() : 0 );
	if ( ImGui::Checkbox( "Move emitter preview", &S.bPreviewMotion ) ) NDebugUI::RestartEffectPreview( S );
	ImGui::SameLine();
	ImGui::TextDisabled( "Preview only" );
	ImGui::SetNextItemWidth( 130.0f );
	ImGui::SliderFloat( "Speed", &S.PreviewSpeed, 0.05f, 4.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp );
	ImGui::SameLine();
	if ( ImGui::Button( "Reset camera" ) )
	{
		S.Camera = FEditorCamera();
		S.Camera.Distance = 5.0f;
		// reset後に実viewportで一度だけTree・meshの全体を合わせる。
		NEffectEditorUI::SetPendingEffectCameraFocus( S, S.Document.Get() );
	}
	ImGui::TextDisabled( "Left drag: orbit | middle/right drag: pan | wheel: zoom" );
	if ( !S.Issues.IsEmpty() )
	{
		ImGui::BeginChild( "EffectValidation", ImVec2( 0.0f, 90.0f ), ImGuiChildFlags_Borders );
		// 再生や保存を止めている理由を、編集している値と同じ画面で示す。
		for ( const FParticleIssue& Issue : S.Issues )
		{
			if ( Issue.Emitter >= 0 ) ImGui::Text( "Emitter %d", Issue.Emitter + 1 );
			ImGui::PushStyleColor( ImGuiCol_Text, Issue.bError ? ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ) : ImVec4( 1.0f, 0.8f, 0.35f, 1.0f ) );
			ImGui::TextWrapped( "%s: %s", Issue.bError ? "Error" : "Warning", UI_TEXT( Issue.Message ) );
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
	}
	// プレビューを描く領域の左上。入力用の領域を作る前に記録する。
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	// 親の残り領域を、入力と画像の表示にそのまま使う。
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if ( Size.x < 16.0f || Size.y < 16.0f ) return;
	// 大きなウィンドウでも縦横比を保ち、描画先の辺を 2048 以内にする。
	const float32 RenderScale = std::min( 1.0f, 2048.0f / std::max( Size.x, Size.y ) );
	S.View.Width = std::clamp( static_cast<int32>( Size.x * RenderScale ), 2, 2048 );
	S.View.Height = std::clamp( static_cast<int32>( Size.y * RenderScale ), 2, 2048 );
	// 実際のviewport比率が確定したこの時点で、open/reset時のfocusを一度だけ適用する。
	NEffectEditorUI::ApplyPendingEffectCameraFocus( S, S.View );
	ImGui::InvisibleButton( "##EffectPreview", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
	// この領域の上で操作されたマウスだけをカメラへ渡す。
	const ImGuiIO& Input = ImGui::GetIO();
	if ( ImGui::IsItemActive() )
	{
		if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) S.Camera.Orbit( Input.MouseDelta.x, Input.MouseDelta.y );
		else S.Camera.Pan( S.View, Input.MouseDelta.x * RenderScale, Input.MouseDelta.y * RenderScale );
	}
	if ( ImGui::IsItemHovered() && Input.MouseWheel != 0.0f ) S.Camera.Zoom( Input.MouseWheel );
	// 描画の実行は既存の窓口へ渡し、描画先の作成と状態の復元は Surface に任せる。
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	// 描けなかったフレームに、以前の画像だけが残ることを避ける。
	const bool bRendered = Executor != nullptr && S.Surface.Render( S.Camera, S.View, S.Playback.Get(), *Executor );
	if ( bRendered && S.Surface.GetHandle() >= 0 ) ImGui::GetWindowDrawList()->AddImage( NDebugUI::GetImGuiTexture( S.Surface.GetHandle() ), Origin, ImVec2( Origin.x + Size.x, Origin.y + Size.y ) );
	else ImGui::GetWindowDrawList()->AddText( ImVec2( Origin.x + 12.0f, Origin.y + 12.0f ), IM_COL32( 230, 230, 230, 255 ), "Preview unavailable" );
}
