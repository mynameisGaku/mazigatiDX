// SPDX-License-Identifier: MIT
#include "AssetUsesPanel.h"

#include "Library/DebugUI/Asset/AssetBrowser.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "VendorModules/ImGui/imgui.h"

void NDebugUI::DrawAssetUsesPanel( FAssetUsesState& State, const FString& AssetPath )
{
	if ( AssetPath.IsEmpty() )
	{
		ImGui::TextDisabled( "Save the file first: worlds refer to it by its path." );
		return;
	}
	const FString Normalized = NWorld::NormalizeAssetPath( AssetPath );
	if ( State.bScanned && State.ScannedPath != Normalized )
	{
		State.bScanned = false;
		State.Results.Reset();
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		ImGui::TextUnformatted( "The asset manager is not running." );
		return;
	}
	if ( ImGui::Button( State.bScanned ? "Scan the worlds again" : "Scan the worlds" ) )
	{
		State.Results = NAssetUsage::FindWorldsUsing( *Assets, AssetPath );
		State.ScannedPath = Normalized;
		State.bScanned = true;
	}
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Reads every .gxworld under the asset root and lists the objects that use this file." );
	}
	if ( !State.bScanned )
	{
		ImGui::TextDisabled( "Not scanned yet." );
		return;
	}
	if ( State.Results.Num() == 0 )
	{
		ImGui::TextDisabled( "No world uses this file." );
		return;
	}
	int32 NumUses = 0;
	for ( const FWorldUsage& Usage : State.Results )
	{
		NumUses += Usage.Uses.Num();
	}
	ImGui::TextDisabled( "%d world(s), %d use(s)", State.Results.Num(), NumUses );
	for ( int32 Index = 0; Index < State.Results.Num(); ++Index )
	{
		const FWorldUsage& Usage = State.Results[Index];
		ImGui::PushID( Index );
		if ( ImGui::SmallButton( "Open" ) )
		{
			NDebugUI::RequestOpenAssetInEditor( Usage.WorldPath );
		}
		ImGui::SameLine();
		ImGui::TextUnformatted( UI_TEXT( Usage.WorldPath ) );
		for ( const FWorldAssetUse& Use : Usage.Uses )
		{
			ImGui::BulletText( "#%d %s  (%s)", Use.ObjectId, Use.ObjectName.IsEmpty() ? "(unnamed)" : UI_TEXT( Use.ObjectName ), UI_TEXT( Use.Component ) );
		}
		ImGui::PopID();
	}
}
