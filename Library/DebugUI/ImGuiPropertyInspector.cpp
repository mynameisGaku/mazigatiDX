#include "ImGuiPropertyInspector.h"

#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "VendorModules/ImGui/imgui.h"

#pragma region FImGuiPropertyInspector

void FImGuiPropertyInspector::Text( const char* Label, const FString& Value )
{
	ImGui::LabelText( Label, "%s", *Value.ToUtf8() );
}

bool FImGuiPropertyInspector::Bool( const char* Label, bool& Value )
{
	return ImGui::Checkbox( Label, &Value );
}

bool FImGuiPropertyInspector::Int( const char* Label, int32& Value )
{
	return ImGui::DragInt( Label, &Value );
}

bool FImGuiPropertyInspector::Float( const char* Label, float32& Value )
{
	return ImGui::DragFloat( Label, &Value, 0.01f );
}

bool FImGuiPropertyInspector::Vector3( const char* Label, FVector3& Value )
{
	float Values[3] = { Value.x, Value.y, Value.z };
	if ( !ImGui::DragFloat3( Label, Values, 0.01f ) ) return false;
	Value = FVector3( Values[0], Values[1], Values[2] );
	return true;
}

#pragma endregion

#pragma region DebugUI

namespace
{
	// CObject が列挙したプロパティを ImGui で表示する
	void DrawProperties( CObject& Object )
	{
		FImGuiPropertyInspector Inspector;
		Object.InspectProperties( Inspector );
	}
}

void NDebugUI::DrawGameObject( CGameObject& Object )
{
	ImGui::PushID( &Object );

	const FString Label = FString::Printf( "#%llu %s", static_cast<unsigned long long>( Object.GetId() ), *Object.GetName() );
	const bool bHasChildren = !Object.GetChildren().IsEmpty();
	const bool bHasComponents = Object.GetNumComponents() > 0;

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if ( Object.IsPendingDestroy() ) ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.6f, 0.6f, 0.6f, 1.0f ) );
	const bool bOpen = ImGui::TreeNodeEx( "##Object", Flags, "%s%s%s", *Label.ToUtf8(),
										  bHasComponents ? "  [C]" : "", bHasChildren ? "  [+]" : "" );
	if ( Object.IsPendingDestroy() ) ImGui::PopStyleColor();

	if ( bOpen )
	{
		ImGui::TextDisabled( "%s", *Object.GetTypeName().ToUtf8() );
		DrawProperties( Object );

		// コンポーネント
		for ( int32 i = 0; i < Object.GetNumComponents(); ++i )
		{
			CComponent* Component = Object.GetComponentAt( i );
			ImGui::PushID( Component );
			if ( ImGui::TreeNodeEx( "##Component", ImGuiTreeNodeFlags_DefaultOpen, "%s", *Component->GetName().ToUtf8() ) )
			{
				DrawProperties( *Component );
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		// 子オブジェクト（表示中に配列が変わらないようコピーしてから回す）
		const TArray<CGameObject*> Children = Object.GetChildren();
		for ( CGameObject* Child : Children ) DrawGameObject( *Child );

		ImGui::TreePop();
	}

	ImGui::PopID();
}

void NDebugUI::DrawSceneHierarchy( CScene& Scene )
{
	for ( CGameObject* Root : Scene.GetRootObjects() ) DrawGameObject( *Root );
}

void NDebugUI::DrawSubsystems( const char* Label, const FSubsystemCollection& Subsystems )
{
	if ( !ImGui::TreeNode( Label, "%s (%d)", Label, Subsystems.GetAll().Num() ) ) return;

	for ( CSubsystem* Subsystem : Subsystems.GetAll() )
	{
		ImGui::PushID( Subsystem );
		if ( ImGui::TreeNode( "##Subsystem", "%s", *Subsystem->GetTypeName().ToUtf8() ) )
		{
			DrawProperties( *Subsystem );
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::TreePop();
}

#pragma endregion
