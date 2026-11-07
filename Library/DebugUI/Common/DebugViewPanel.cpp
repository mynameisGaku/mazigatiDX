// SPDX-License-Identifier: MIT
#include "DebugViewPanel.h"

#include "VendorModules/ImGui/imgui.h"

void NDebugUI::DrawDebugViewOptions( const FDebugView& View )
{
	for ( const FDebugViewOption& Option : View.Options )
	{
		ImGui::PushID( Option.Name );
		switch ( Option.Type )
		{
		case FDebugViewOption::EType::Bool:
			ImGui::Checkbox( Option.Name, static_cast<bool*>( Option.Value ) );
			break;
		case FDebugViewOption::EType::Int:
			ImGui::SetNextItemWidth( 120.0f );
			ImGui::DragInt( Option.Name, static_cast<int32*>( Option.Value ), 1.0f, static_cast<int32>( Option.Min ), static_cast<int32>( Option.Max ) );
			break;
		default:
			ImGui::SetNextItemWidth( 120.0f );
			ImGui::DragFloat( Option.Name, static_cast<float32*>( Option.Value ), 0.1f, Option.Min, Option.Max, "%.2f" );
			break;
		}
		if ( ImGui::IsItemHovered() && Option.Description[0] != '\0' )
		{
			ImGui::SetTooltip( "%s", Option.Description );
		}
		ImGui::PopID();
	}
}
