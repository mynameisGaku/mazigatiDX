// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"

#include <cmath>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 入力欄を離れたときに履歴sessionを閉じる。
	bool TrackMeshInputEnd( bool bChanged, TParticleEffectDocument& Document )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) Document.EndEditSession();
		return bChanged;
	}

	// mesh warningを欄幅に合わせて折り返す。
	void TextMeshWarning( const char* Text )
	{
		ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ) );
		ImGui::TextWrapped( "%s", Text );
		ImGui::PopStyleColor();
	}
}

bool NEffectEditorUI::DrawMeshPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
{
	FEffectMeshDefinition& Mesh = Emitter.Mesh;
	// panel内で保存定義へ反映する変更の有無。
	bool bChanged = false;
	ImGui::SeparatorText( "Mesh geometry" );
	if ( ImGui::SmallButton( "Reset mesh" ) )
	{
		Mesh = FEffectMeshDefinition();
		State.Document.EndEditSession();
		bChanged = true;
	}
	// 誤ったJSON型や範囲外値はImGuiへ渡さず、meshだけを戻せるようにする。
	const TArray<FString> MeshIssues = Mesh.Validate();
	if ( !MeshIssues.IsEmpty() )
	{
		TextMeshWarning( "Mesh settings are invalid; reset mesh to repair the saved values." );
		return bChanged;
	}
	// 保存enumと同じ順で並ぶ固定形状。
	static const char* KindNames[] = { "None (quad)", "Plane", "Ring" };
	// 検証済みmesh種別を表示するcombo番号。
	int32 Kind = static_cast<int32>( Mesh.Kind );
	if ( ImGui::Combo( "Mesh kind", &Kind, KindNames, 3 ) )
	{
		Mesh.Kind = static_cast<FEffectMeshDefinition::EKind>( Kind );
		bChanged = true;
	}
	if ( Mesh.Kind == FEffectMeshDefinition::EKind::None ) return bChanged;
	if ( Mesh.Kind == FEffectMeshDefinition::EKind::Ring )
	{
		// 分割数はRing形状だけで編集し、Planeへ切り替えても値を保持する。
		bChanged |= TrackMeshInputEnd( ImGui::DragInt( "Ring segments", &Mesh.Segments, 1.0f, 3, 128, "%d", ImGuiSliderFlags_AlwaysClamp ), State.Document );
		// 円環の穴半径を外半径との比率で編集する。
		bChanged |= TrackMeshInputEnd( ImGui::DragFloat( "Ring inner radius", &Mesh.InnerRadius, 0.005f, 0.05f, 0.95f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), State.Document );
	}
	// 固定形状の角度を編集する前に有限値を保証する。
	if ( NMath::IsFinite( Mesh.RotationDegrees.x ) && NMath::IsFinite( Mesh.RotationDegrees.y ) && NMath::IsFinite( Mesh.RotationDegrees.z ) ) bChanged |= TrackMeshInputEnd( ImGui::DragFloat3( "Mesh rotation (deg)", &Mesh.RotationDegrees.x, 0.25f, -360000.0f, 360000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), State.Document );
	else TextMeshWarning( "Mesh rotation is invalid; reset mesh to repair it." );
	ImGui::TextWrapped( "Mesh keeps a fixed world-facing shape; particle Orientation is ignored. Particle size, spin and effect rotation still apply." );
	ImGui::TextDisabled( "Only the first 64 mesh particles per playback are recorded, in emitter and spawn order." );
	ImGui::TextDisabled( "Enabled material uses its curves, scroll, dissolve and distortion; disabled material uses neutral unlit values." );
	if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None ) TextMeshWarning( "Mesh and Path cannot be used together. Set Path kind to None to use this mesh." );
	if ( Emitter.Flipbook.bEnabled ) TextMeshWarning( "Mesh and Flipbook cannot be used together. Disable Flipbook to use this mesh." );
	// mesh命令の描画実装が接続されているか確認する。
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	if ( Executor == nullptr || !Executor->SupportsEffectMaterialMesh() ) TextMeshWarning( "The active renderer does not support material mesh particles; mesh particles are not drawn." );
	return bChanged;
}
