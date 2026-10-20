#include "LogCategoryPanel.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Log/Log.h"
#include "VendorModules/ImGui/imgui.h"

namespace
{
	// ELogVerbosity の値の順
	const char* const VerbosityNames[] = { "NoLogging", "Fatal", "Error", "Warning", "Display", "Log", "Verbose", "VeryVerbose" };

	// コンパイル時に残している最大の Verbosity（これより詳細なログは、有効にしても出ない）
	constexpr int32 CompiledMaxVerbosity = static_cast<int32>( ELogVerbosity::LOG_COMPILE_VERBOSITY );
}

void NDebugUI::DrawLogCategories()
{
	CLogManager& Manager = CLogManager::Get();

	if ( ImGui::SmallButton( "All Verbose" ) ) Manager.SetAllCategoriesVerbosity( ELogVerbosity::Verbose );
	ImGui::SameLine();
	if ( ImGui::SmallButton( "All default" ) ) Manager.ResetAllCategoriesToDefault();
	ImGui::SameLine();
	ImGui::TextDisabled( "(this build keeps logs up to %s)", VerbosityNames[CompiledMaxVerbosity] );

	TArray<FLogCategory*> Categories = Manager.GetCategories();
	Categories.Sort( []( const FLogCategory* A, const FLogCategory* B ) { return std::strcmp( A->GetName(), B->GetName() ) < 0; } );

	if ( !ImGui::BeginTable( "##LogCategories", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) ) return;
	ImGui::TableSetupColumn( "Category", 0, 1.0f );
	ImGui::TableSetupColumn( "Verbosity", 0, 1.0f );

	for ( FLogCategory* Category : Categories )
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( Category->GetName() );

		ImGui::TableNextColumn();
		ImGui::PushID( Category );
		int32 Current = static_cast<int32>( Category->GetVerbosity() );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( ImGui::Combo( "##Verbosity", &Current, VerbosityNames, IM_ARRAYSIZE( VerbosityNames ) ) )
		{
			Category->SetVerbosity( static_cast<ELogVerbosity>( Current ) );
			LOG( LogTemp, Log, "Log category %s: verbosity set to %s", Category->GetName(), VerbosityNames[Current] );
		}
		ImGui::PopID();
	}
	ImGui::EndTable();
}
