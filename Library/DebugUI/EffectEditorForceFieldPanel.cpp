// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"

#include <algorithm>
#include <cmath>

#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Effect/EffectForceFieldDefinition.h"

namespace
{
	// 終了した数値入力の通知を呼び出し側の既存値へ加える。
	bool TrackForceFieldInputEnd( bool bChanged, bool& bInputEnded )
	{
		bInputEnded |= ImGui::IsItemDeactivatedAfterEdit();
		return bChanged;
	}

	// 数値入力へ渡せる力場保存値かを、全成分について調べる。
	bool IsFiniteForceField( const FEffectForceFieldDefinition& Field )
	{
		return std::isfinite( Field.Acceleration.x ) && std::isfinite( Field.Acceleration.y ) && std::isfinite( Field.Acceleration.z ) && std::isfinite( Field.Position.x ) && std::isfinite( Field.Position.y ) && std::isfinite( Field.Position.z ) && std::isfinite( Field.Axis.x ) && std::isfinite( Field.Axis.y ) && std::isfinite( Field.Axis.z ) && std::isfinite( Field.Radius ) && std::isfinite( Field.Strength ) && std::isfinite( Field.Frequency ) && std::isfinite( Field.ScrollSpeed );
	}

	// 回転軸として使える有限の非ゼロベクトルかを、二乗和のoverflowを避けて調べる。
	bool IsUsableForceFieldAxis( const FVector3& Axis )
	{
		// 各成分をdoubleへ広げた長さの二乗。
		const double LengthSquared = static_cast<double>( Axis.x ) * Axis.x + static_cast<double>( Axis.y ) * Axis.y + static_cast<double>( Axis.z ) * Axis.z;
		return std::isfinite( LengthSquared ) && LengthSquared >= 1.0e-12;
	}

	// Comboの表示順。FEffectForceFieldDefinition::EKindの宣言順と1対1にする。
	constexpr FEffectForceFieldDefinition::EKind ForceFieldKindOrder[] = { FEffectForceFieldDefinition::EKind::Uniform, FEffectForceFieldDefinition::EKind::Radial, FEffectForceFieldDefinition::EKind::Vortex, FEffectForceFieldDefinition::EKind::Turbulence };
	// Comboに表示する種類名。ForceFieldKindOrderと同じ順に並べる。
	constexpr const char* ForceFieldKindNames[] = { "Uniform", "Radial", "Vortex", "Turbulence" };
	// Comboの項目数。
	constexpr int32 ForceFieldKindCount = static_cast<int32>( sizeof( ForceFieldKindOrder ) / sizeof( ForceFieldKindOrder[0] ) );

}

// 力場の一覧と保存値を編集する。戻り値はコピーの保存値変更、通知値は既存のtrueを保持する。
bool NEffectEditorUI::DrawForceFieldPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter, bool& bInputEnded, bool& bStructureChanged )
{
	// 保存値を変更したか。
	bool bChanged = false;
	// 文書に保存しない表示設定の作業コピー。
	bool bShowGuides = State.bShowForceFieldGuides;
	ImGui::SeparatorText( "Force fields" );
	if ( ImGui::Checkbox( "Show force field guides", &bShowGuides ) ) State.bShowForceFieldGuides = bShowGuides;
	ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
	ImGui::TextWrapped( "Fields are evaluated in World space, so Local particles are affected in World space too." );
	ImGui::TextWrapped( "A field stays fixed in the World unless Follow emitter is on; then its center and directions move and turn with the emitter. The emitter scale never changes a radius or an acceleration." );
	ImGui::PopStyleColor();

	// コピーに含まれる力場の個数。
	const int32 FieldCount = Emitter.ForceFields.Num();
	State.SelectedForceField = FieldCount > 0 ? std::clamp( State.SelectedForceField, 0, FieldCount - 1 ) : 0;
	// 前frameの構造操作で必要になったfocus復旧要求。
	const bool bRestoreForceFieldFocus = State.bForceFieldFocusPending;
	// 選択中の添字が現在のコピーにあるか。
	const bool bHasSelection = Emitter.ForceFields.IsValidIndex( State.SelectedForceField );
	// 新規作成と複製を止める上限判定。
	const bool bAtLimit = FieldCount >= FEffectForceFieldDefinition::MaxFields;
	// 各toolbar操作が選ばれたか。
	bool bAdd = false;
	bool bDuplicate = false;
	bool bDelete = false;
	bool bMoveUp = false;
	bool bMoveDown = false;
	bool bReset = false;

	ImGui::BeginDisabled( bAtLimit );
	if ( bRestoreForceFieldFocus && !bHasSelection )
	{
		ImGui::SetKeyboardFocusHere();
		State.bForceFieldFocusPending = false;
	}
	if ( ImGui::Button( "Add##ForceFieldAdd" ) ) bAdd = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !bHasSelection || bAtLimit );
	if ( ImGui::Button( "Duplicate##ForceFieldDuplicate" ) ) bDuplicate = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !bHasSelection );
	if ( ImGui::Button( "Delete##ForceFieldDelete" ) ) bDelete = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !bHasSelection || State.SelectedForceField <= 0 );
	if ( ImGui::Button( "Up##ForceFieldUp" ) ) bMoveUp = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !bHasSelection || State.SelectedForceField >= FieldCount - 1 );
	if ( ImGui::Button( "Down##ForceFieldDown" ) ) bMoveDown = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !bHasSelection );
	if ( ImGui::Button( "Reset##ForceFieldReset" ) ) bReset = true;
	ImGui::EndDisabled();

	// 次frameに選択行へ戻す必要がある配列操作。
	const bool bStructuralOperation = bAdd || bDuplicate || bDelete || bMoveUp || bMoveDown;
	if ( bAdd || bDuplicate || bDelete || bMoveUp || bMoveDown || bReset )
	{
		bInputEnded = true;
		bStructureChanged |= bStructuralOperation;
		if ( bStructuralOperation ) State.bForceFieldFocusPending = true;
		State.Document.EndEditSession();
	}
	if ( bAdd )
	{
		// 追加する既定の一様場。
		FEffectForceFieldDefinition Field;
		Field.Acceleration = FVector3( 1.0f, 0.0f, 0.0f );
		Emitter.ForceFields.Add( Field );
		State.SelectedForceField = Emitter.ForceFields.Num() - 1;
		bChanged = true;
	}
	else if ( bDuplicate )
	{
		Emitter.ForceFields.Insert( Emitter.ForceFields[State.SelectedForceField], State.SelectedForceField + 1 );
		++State.SelectedForceField;
		bChanged = true;
	}
	else if ( bDelete )
	{
		Emitter.ForceFields.RemoveAt( State.SelectedForceField );
		State.SelectedForceField = Emitter.ForceFields.IsEmpty() ? 0 : std::min( State.SelectedForceField, Emitter.ForceFields.Num() - 1 );
		bChanged = true;
	}
	else if ( bMoveUp )
	{
		Emitter.ForceFields.Swap( State.SelectedForceField, State.SelectedForceField - 1 );
		--State.SelectedForceField;
		bChanged = true;
	}
	else if ( bMoveDown )
	{
		Emitter.ForceFields.Swap( State.SelectedForceField, State.SelectedForceField + 1 );
		++State.SelectedForceField;
		bChanged = true;
	}
	else if ( bReset )
	{
		Emitter.ForceFields[State.SelectedForceField] = FEffectForceFieldDefinition{};
		bChanged = true;
	}

	if ( Emitter.ForceFields.IsEmpty() ) ImGui::TextDisabled( "No force fields" );
	else
	{
		// 選択と順序を操作する一覧の添字。
		for ( int32 Index = 0; Index < Emitter.ForceFields.Num(); ++Index )
		{
			ImGui::PushID( Index );
			// 現在選択されている一覧行。
			const bool bSelected = State.SelectedForceField == Index;
			if ( bRestoreForceFieldFocus && !bStructuralOperation && bSelected )
			{
				ImGui::SetKeyboardFocusHere();
				State.bForceFieldFocusPending = false;
			}
			if ( ImGui::Selectable( FString::Printf( "Force field %d", Index + 1 ).GetData(), bSelected ) ) State.SelectedForceField = Index;
			ImGui::PopID();
		}
	}

	if ( Emitter.ForceFields.IsValidIndex( State.SelectedForceField ) )
	{
		// 表示・編集する保存値。
		FEffectForceFieldDefinition& Field = Emitter.ForceFields[State.SelectedForceField];
		// 数値widgetへ渡す前に、保存型のすべての問題を確認する。
		const TArray<FString> Issues = Field.Validate();
		if ( !Issues.IsEmpty() )
		{
			for ( const FString& Issue : Issues ) ImGui::TextWrapped( "%s", UI_TEXT( Issue ) );
			ImGui::TextDisabled( "Numeric controls are hidden until the whole field is reset." );
		}
		else if ( !IsFiniteForceField( Field ) )
		{
			ImGui::TextWrapped( "力場の値に有限でない数値があります。全体をResetしてください。" );
		}
		else
		{
			bChanged |= TrackForceFieldInputEnd( ImGui::Checkbox( "Enable force field##ForceFieldEnabled", &Field.bEnabled ), bInputEnded );
			// Comboで一時的に選ぶ種類の番号。表示できない種類は検証で止まるため、ここでは4種のどれかになる。
			int32 Kind = 0;
			for ( int32 KindIndex = 0; KindIndex < ForceFieldKindCount; ++KindIndex ) if ( ForceFieldKindOrder[KindIndex] == Field.Kind ) Kind = KindIndex;
			if ( TrackForceFieldInputEnd( ImGui::Combo( "Kind##ForceFieldKind", &Kind, ForceFieldKindNames, ForceFieldKindCount ), bInputEnded ) )
			{
				Field.Kind = ForceFieldKindOrder[std::clamp( Kind, 0, ForceFieldKindCount - 1 )];
				bChanged = true;
			}
			bChanged |= TrackForceFieldInputEnd( ImGui::Checkbox( "Follow emitter##ForceFieldFollowEmitter", &Field.bFollowEmitter ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat3( "Acceleration (m/s2)##ForceFieldAcceleration", &Field.Acceleration.x, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat3( "World center (m)##ForceFieldPosition", &Field.Position.x, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			// 回転軸の編集コピー。全成分がゼロになる入力は保存へ反映しない。
			FVector3 EditedAxis = Field.Axis;
			// 回転軸の入力があったか。
			const bool bAxisEdited = ImGui::DragFloat3( "Axis##ForceFieldAxis", &EditedAxis.x, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp );
			// 使える軸だけを保存へ反映する。
			const bool bAxisChanged = bAxisEdited && IsUsableForceFieldAxis( EditedAxis );
			if ( bAxisChanged ) Field.Axis = EditedAxis;
			bChanged |= TrackForceFieldInputEnd( bAxisChanged, bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Radius (m)##ForceFieldRadius", &Field.Radius, 0.01f, 0.001f, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Strength (m/s2)##ForceFieldStrength", &Field.Strength, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Frequency (rad/m)##ForceFieldFrequency", &Field.Frequency, 0.01f, 0.001f, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Scroll speed (rad/s)##ForceFieldScrollSpeed", &Field.ScrollSpeed, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
			ImGui::TextWrapped( "Radial: negative Strength attracts, positive repels. Vortex: swirls around Axis (the sign reverses the turning direction). Turbulence: Strength is the amplitude, Frequency the wave density, Scroll speed how fast it changes. Inactive values are editable and retained." );
			ImGui::PopStyleColor();
		}
	}

	return bChanged;
}
