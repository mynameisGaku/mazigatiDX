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
		return std::isfinite( Field.Acceleration.x ) && std::isfinite( Field.Acceleration.y ) && std::isfinite( Field.Acceleration.z ) && std::isfinite( Field.Position.x ) && std::isfinite( Field.Position.y ) && std::isfinite( Field.Position.z ) && std::isfinite( Field.Radius ) && std::isfinite( Field.Strength );
	}

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
	ImGui::TextDisabled( "Centers and radii use fixed World coordinates; Local particles are affected in World space." );
	ImGui::TextDisabled( "The emitter transform does not move these fields or scale their radii." );

	// コピーに含まれる力場の個数。
	const int32 FieldCount = Emitter.ForceFields.Num();
	State.SelectedForceField = FieldCount > 0 ? std::clamp( State.SelectedForceField, 0, FieldCount - 1 ) : 0;
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

	if ( bAdd || bDuplicate || bDelete || bMoveUp || bMoveDown || bReset )
	{
		bInputEnded = true;
		bStructureChanged |= bAdd || bDuplicate || bDelete || bMoveUp || bMoveDown;
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
			bChanged |= TrackForceFieldInputEnd( ImGui::Checkbox( "Controls enabled##ForceFieldEnabled", &Field.bEnabled ), bInputEnded );
			// UI表示順を定義の列挙値へ対応させる。
			static const char* KindNames[] = { "Uniform", "Radial" };
			// Comboで一時的に選ぶ種類の番号。
			int32 Kind = Field.Kind == FEffectForceFieldDefinition::EKind::Uniform ? 0 : 1;
			if ( TrackForceFieldInputEnd( ImGui::Combo( "Kind##ForceFieldKind", &Kind, KindNames, 2 ), bInputEnded ) )
			{
				Field.Kind = Kind == 0 ? FEffectForceFieldDefinition::EKind::Uniform : FEffectForceFieldDefinition::EKind::Radial;
				bChanged = true;
			}
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat3( "Acceleration (m/s2)##ForceFieldAcceleration", &Field.Acceleration.x, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat3( "World center (m)##ForceFieldPosition", &Field.Position.x, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Radius (m)##ForceFieldRadius", &Field.Radius, 0.01f, 0.001f, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			bChanged |= TrackForceFieldInputEnd( ImGui::DragFloat( "Strength (m/s2)##ForceFieldStrength", &Field.Strength, 0.01f, -FEffectForceFieldDefinition::MaxMagnitude, FEffectForceFieldDefinition::MaxMagnitude, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			ImGui::TextDisabled( "Radial strength: negative attracts, positive repels. Inactive values are editable and retained." );
		}
	}

	return bChanged;
}
