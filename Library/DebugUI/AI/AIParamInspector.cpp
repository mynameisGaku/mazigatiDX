// SPDX-License-Identifier: MIT
#include "AIParamInspector.h"

#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Math/MathUtils.h"
#include "VendorModules/ImGui/imgui.h"

namespace
{
	const ImVec4 ColorBad( 1.0f, 0.45f, 0.4f, 1.0f );

	// 項目の名前（必須は * つき）と、説明のツールチップ
	void DrawLabel( const FParamDefinition& Definition )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( Definition.bRequired ? UI_TEXT( Definition.Name + " *" ) : UI_TEXT( Definition.Name ) );
		if ( !Definition.Hint.IsEmpty() && ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "%s", UI_TEXT( Definition.Hint ) );
		}
		ImGui::SameLine( 130.0f );
		ImGui::SetNextItemWidth( -FLT_MIN );
	}
}

bool NDebugUI::DrawNameCombo( const char* Label, FString& Value, const TArray<FString>& Items, const char* NoneText )
{
	// 選択肢が 1 つも無いときは、確かめようがないので、赤にしない
	const bool bKnown = Value.IsEmpty() || Items.IsEmpty() || Items.Contains( Value );
	if ( !bKnown )
	{
		ImGui::PushStyleColor( ImGuiCol_Text, ColorBad );
	}
	const bool bChanged = FileCombo( Label, Value, Items, NoneText );
	if ( !bKnown )
	{
		ImGui::PopStyleColor();
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "'%s' is not in the list.", UI_TEXT( Value ) );
		}
	}
	return bChanged;
}

bool NDebugUI::DrawBlackboardKeyCombo( const char* Label, FString& Key, const FAIInspectContext& Context )
{
	if ( Context.Blackboard == nullptr )
	{
		return InputString( Label, Key );
	}
	TArray<FString> Names;
	for ( const FBlackboardKeyDefinition& Definition : Context.Blackboard->Keys )
	{
		Names.Add( Definition.Name );
	}
	return DrawNameCombo( Label, Key, Names, "(choose a key)" );
}

bool NDebugUI::DrawCompareOpCombo( const char* Label, EAICompareOp& Op, const FBlackboardKeyDefinition* Key )
{
	const bool bOrdering = Key == nullptr || NAI::SupportsOrdering( Key->Type );
	bool bChanged = false;
	if ( ImGui::BeginCombo( Label, NAI::ToString( Op ) ) )
	{
		for ( uint8 Index = 0; Index <= static_cast<uint8>( EAICompareOp::GreaterEqual ); ++Index )
		{
			const EAICompareOp Candidate = static_cast<EAICompareOp>( Index );
			const bool bAllowed = bOrdering || Candidate == EAICompareOp::Equal || Candidate == EAICompareOp::NotEqual;
			ImGui::BeginDisabled( !bAllowed );
			if ( ImGui::Selectable( NAI::ToString( Candidate ), Candidate == Op ) )
			{
				Op = Candidate;
				bChanged = true;
			}
			ImGui::EndDisabled();
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

bool NDebugUI::DrawBlackboardValueInput( const char* Label, const FBlackboardKeyDefinition* Key, NlohmannJson& Value )
{
	if ( Key == nullptr )
	{
		FString Text = Value.is_null() ? FString() : NJson::FromJsonString( Value.dump() );
		if ( InputString( Label, Text, ImGuiInputTextFlags_EnterReturnsTrue ) )
		{
			const NlohmannJson Parsed = NlohmannJson::parse( *Text, nullptr, false );
			Value = Parsed.is_discarded() ? NlohmannJson( Text ) : Parsed;
			return true;
		}
		return false;
	}

	FBlackboardValue Current;
	if ( !FBlackboardDefinition::ValueFromJson( *Key, Value, Current ) )
	{
		Current = Key->Default;
	}
	FBlackboardValue Edited = Current;
	bool bChanged = false;
	switch ( Key->Type )
	{
	case EBlackboardKeyType::Bool:
	{
		bool bValue = Current.Integer != 0;
		if ( ImGui::Checkbox( Label, &bValue ) )
		{
			Edited = FBlackboardValue::MakeBool( bValue );
			bChanged = true;
		}
		break;
	}
	case EBlackboardKeyType::Int:
	{
		int Number = static_cast<int>( Current.Integer );
		if ( ImGui::InputInt( Label, &Number ) )
		{
			Edited = FBlackboardValue::MakeInt( Number );
			bChanged = true;
		}
		break;
	}
	case EBlackboardKeyType::Float:
	{
		float Number = Current.Real;
		if ( ImGui::DragFloat( Label, &Number, 0.05f ) )
		{
			Edited = FBlackboardValue::MakeFloat( Number );
			bChanged = true;
		}
		break;
	}
	case EBlackboardKeyType::Vector3:
	{
		float Numbers[3] = { Current.Vector.x, Current.Vector.y, Current.Vector.z };
		if ( ImGui::DragFloat3( Label, Numbers, 0.05f ) )
		{
			Edited = FBlackboardValue::MakeVector( FVector3( Numbers[0], Numbers[1], Numbers[2] ) );
			bChanged = true;
		}
		break;
	}
	case EBlackboardKeyType::Name:
	{
		FString Text = Current.Text;
		if ( InputString( Label, Text ) )
		{
			Edited = FBlackboardValue::MakeName( Text );
			bChanged = true;
		}
		break;
	}
	case EBlackboardKeyType::Object:
		ImGui::TextDisabled( "(an object cannot be compared here)" );
		break;
	case EBlackboardKeyType::Enum:
	{
		if ( Key->EnumValues.IsEmpty() )
		{
			ImGui::TextDisabled( "(the key has no values)" );
			break;
		}
		const int32 Selected = static_cast<int32>( NMath::Clamp<int64>( Current.Integer, 0, Key->EnumValues.Num() - 1 ) );
		if ( ImGui::BeginCombo( Label, UI_TEXT( Key->EnumValues[Selected] ) ) )
		{
			for ( int32 Index = 0; Index < Key->EnumValues.Num(); ++Index )
			{
				if ( ImGui::Selectable( UI_TEXT( Key->EnumValues[Index] ), Index == Selected ) )
				{
					Edited = FBlackboardValue::MakeEnum( Index );
					bChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	}
	if ( bChanged )
	{
		Value = FBlackboardDefinition::ValueToJson( *Key, Edited );
	}
	return bChanged;
}

bool NDebugUI::DrawParamInspector( const FParamSchema& Schema, NlohmannJson& Params, const FAIInspectContext& Context )
{
	if ( Schema.GetNumParams() == 0 )
	{
		ImGui::TextDisabled( "(no parameters)" );
		return false;
	}

	FParamSet Set = NAI::ResolveParams( Schema, Params.is_object() ? &Params : nullptr, "inspector", nullptr );
	bool bChanged = false;
	const TArray<FParamDefinition>& Definitions = Schema.GetParams();
	for ( int32 Index = 0; Index < Definitions.Num(); ++Index )
	{
		const FParamDefinition& Definition = Definitions[Index];
		FParamValue& Value = Set.GetValues()[Index];
		ImGui::PushID( Index );
		DrawLabel( Definition );
		switch ( Definition.Type )
		{
		case EParamType::Bool:
			bChanged |= ImGui::Checkbox( "##value", &Value.bValue );
			break;
		case EParamType::Int:
			if ( Definition.bHasRange )
			{
				bChanged |= ImGui::SliderInt( "##value", &Value.IntValue, static_cast<int>( Definition.Min ), static_cast<int>( Definition.Max ) );
			}
			else
			{
				bChanged |= ImGui::InputInt( "##value", &Value.IntValue );
			}
			break;
		case EParamType::Float:
			if ( Definition.bHasRange )
			{
				bChanged |= ImGui::SliderFloat( "##value", &Value.FloatValue, Definition.Min, Definition.Max );
			}
			else
			{
				bChanged |= ImGui::DragFloat( "##value", &Value.FloatValue, 0.05f );
			}
			break;
		case EParamType::Enum:
			bChanged |= DrawNameCombo( "##value", Value.TextValue, Definition.Options, "(choose)" );
			break;
		case EParamType::BlackboardKey:
			bChanged |= DrawBlackboardKeyCombo( "##value", Value.TextValue, Context );
			break;
		case EParamType::Clip:
			if ( Context.ClipNames != nullptr && !Context.ClipNames->IsEmpty() )
			{
				bChanged |= DrawNameCombo( "##value", Value.TextValue, *Context.ClipNames, "(choose a clip)" );
			}
			else
			{
				bChanged |= InputString( "##value", Value.TextValue );
			}
			break;
		case EParamType::Scalar:
		{
			// 型が実行時に決まる値: JSON として入力する（Enter で反映）。JSON として読めなければ、文字列にする
			FString Text = Value.TextValue;
			if ( InputString( "##value", Text, ImGuiInputTextFlags_EnterReturnsTrue ) )
			{
				const NlohmannJson Parsed = NlohmannJson::parse( *Text, nullptr, false );
				Value.TextValue = NJson::FromJsonString( ( Parsed.is_discarded() ? NlohmannJson( Text ) : Parsed ).dump() );
				bChanged = true;
			}
			if ( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "A JSON value (true, 1.5, \"text\"). Press Enter to apply." );
			}
			break;
		}
		case EParamType::String:
		case EParamType::AssetPath:
			bChanged |= InputString( "##value", Value.TextValue );
			break;
		}
		ImGui::PopID();
	}

	if ( bChanged )
	{
		NlohmannJson Written = NAI::ParamsToJson( Schema, Set );
		Params = ( Written.is_object() && Written.empty() ) ? NlohmannJson() : Written;
	}
	return bChanged;
}
