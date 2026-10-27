#include "EditorCommon.h"

#include "Library/Core/Types/CString.h"

bool NDebugUI::InputString( const char* Label, FString& Value, ImGuiInputTextFlags Flags )
{
	return InputStringWithHint( Label, nullptr, Value, Flags );
}

bool NDebugUI::InputStringWithHint( const char* Label, const char* Hint, FString& Value, ImGuiInputTextFlags Flags )
{
	char Buffer[512];
	const FString Utf8 = Value.ToUtf8();
	strncpy_s( Buffer, sizeof( Buffer ), *Utf8, _TRUNCATE );

	const bool bChanged = Hint != nullptr ? ImGui::InputTextWithHint( Label, Hint, Buffer, sizeof( Buffer ), Flags ) : ImGui::InputText( Label, Buffer, sizeof( Buffer ), Flags );
	if ( bChanged ) Value = FString::FromUtf8( Buffer );
	return bChanged;
}

bool NDebugUI::FileCombo( const char* Label, FString& Selected, const TArray<FString>& Files, const char* NoneText )
{
	bool bChanged = false;
	const FString Preview = Selected.IsEmpty() ? FString( NoneText ) : Selected;
	if ( ImGui::BeginCombo( Label, UI_TEXT( Preview ) ) )
	{
		static char Filter[128] = {};
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( ImGui::IsWindowAppearing() )
		{
			Filter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint( "##Filter", "filter", Filter, sizeof( Filter ) );
		const FString FilterText = FString::FromUtf8( Filter );

		if ( Files.IsEmpty() ) ImGui::TextDisabled( "(no files found under the asset folder)" );
		for ( const FString& File : Files )
		{
			if ( !FilterText.IsEmpty() && !File.Contains( FilterText, ESearchCase::IgnoreCase ) ) continue;
			const bool bIsSelected = File == Selected;
			if ( ImGui::Selectable( UI_TEXT( File ), bIsSelected ) )
			{
				Selected = File;
				bChanged = true;
			}
			if ( bIsSelected ) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

void NDebugUI::ImageRect( ImTextureID Texture, int32 TextureWidth, int32 TextureHeight, int32 X, int32 Y, int32 Width, int32 Height, const ImVec2& Size, bool bNearest )
{
	if ( Texture == 0 || TextureWidth <= 0 || TextureHeight <= 0 )
	{
		ImGui::Dummy( Size );
		return;
	}
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Uv0( static_cast<float>( X ) / static_cast<float>( TextureWidth ), static_cast<float>( Y ) / static_cast<float>( TextureHeight ) );
	const ImVec2 Uv1( static_cast<float>( X + Width ) / static_cast<float>( TextureWidth ), static_cast<float>( Y + Height ) / static_cast<float>( TextureHeight ) );
	if ( bNearest ) BeginNearest( DrawList );
	DrawList->AddImage( Texture, Min, ImVec2( Min.x + Size.x, Min.y + Size.y ), Uv0, Uv1 );
	if ( bNearest ) EndNearest( DrawList );
	ImGui::Dummy( Size );
}

void NDebugUI::DrawChecker( ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, float CellSize )
{
	DrawList->AddRectFilled( Min, Max, IM_COL32( 70, 70, 74, 255 ) );
	const ImU32 Light = IM_COL32( 96, 96, 100, 255 );
	int Row = 0;
	for ( float Y = Min.y; Y < Max.y; Y += CellSize, ++Row )
	{
		int Column = Row;
		for ( float X = Min.x; X < Max.x; X += CellSize, ++Column )
		{
			if ( ( Column & 1 ) == 0 ) continue;
			DrawList->AddRectFilled( ImVec2( X, Y ), ImVec2( X + CellSize < Max.x ? X + CellSize : Max.x, Y + CellSize < Max.y ? Y + CellSize : Max.y ), Light );
		}
	}
}

void NDebugUI::PushWarningColor()
{
	ImGui::PushStyleColor( ImGuiCol_Text, IM_COL32( 255, 200, 90, 255 ) );
}

void NDebugUI::PopWarningColor()
{
	ImGui::PopStyleColor();
}

FString NDebugUI::FormatSeconds( float32 Seconds )
{
	return FString::Printf( "%.3f s", Seconds );
}

FString NDebugUI::FormatBytes( int64 Bytes )
{
	const double Value = static_cast<double>( Bytes );
	if ( Value >= 1024.0 * 1024.0 * 1024.0 ) return FString::Printf( "%.2f GB", Value / ( 1024.0 * 1024.0 * 1024.0 ) );
	if ( Value >= 1024.0 * 1024.0 ) return FString::Printf( "%.2f MB", Value / ( 1024.0 * 1024.0 ) );
	if ( Value >= 1024.0 ) return FString::Printf( "%.1f KB", Value / 1024.0 );
	return FString::Printf( "%lld B", static_cast<long long>( Bytes ) );
}
