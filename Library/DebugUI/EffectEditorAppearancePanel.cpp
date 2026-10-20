// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"
#include "EffectEditorCurveInput.h"

#include <algorithm>
#include <cmath>

#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Effect/EffectAppearanceDefinition.h"
#include "Library/Framework/Effect/ParticleEffect.h"

namespace
{
	// UI previewだけに使い、保存値は変更しないよう色成分を有限の0..1へ整える。
	FColor ClampPreviewColor( const FColor& Value )
	{
		return FColor( std::clamp( std::isfinite( Value.r ) ? Value.r : 0.0f, 0.0f, 1.0f ), std::clamp( std::isfinite( Value.g ) ? Value.g : 0.0f, 0.0f, 1.0f ), std::clamp( std::isfinite( Value.b ) ? Value.b : 0.0f, 0.0f, 1.0f ), std::clamp( std::isfinite( Value.a ) ? Value.a : 0.0f, 0.0f, 1.0f ) );
	}

	// Start/End色とColor倍率を掛け、透明度を含む64区間のpreviewを描く。
	void DrawAppearancePreview( const FParticleEmitterDefinition& Emitter )
	{
		ImGui::TextUnformatted( "Color preview" );
		// 64区間を並べるpreviewの左上。
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		// 現在のパネルが使える横幅。
		const ImVec2 Available = ImGui::GetContentRegionAvail();
		// 小さすぎる表示を避けたpreview幅。
		const float32 Width = std::max( 32.0f, Available.x );
		// チェッカーと色を表示するpreview高さ。
		const float32 Height = 34.0f;
		ImGui::InvisibleButton( "##AppearanceColorPreview", ImVec2( Width, Height ) );
		// チェッカーと色帯を描く現在windowの描画list。
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		// preview矩形の右下。
		const ImVec2 BottomRight( Origin.x + Width, Origin.y + Height );
		// 透明度を見せる背景格子の一辺。
		const float32 Cell = 8.0f;
		for ( float32 Y = Origin.y; Y < BottomRight.y; Y += Cell ) for ( float32 X = Origin.x; X < BottomRight.x; X += Cell )
		{
			// 交互に明暗を変える格子セルか。
			const bool bLightCell = ( static_cast<int32>( ( X - Origin.x ) / Cell ) + static_cast<int32>( ( Y - Origin.y ) / Cell ) ) % 2 == 0;
			// 透明色の後ろに見せる格子色。
			const ImU32 Checker = bLightCell ? IM_COL32( 180, 180, 180, 255 ) : IM_COL32( 110, 110, 110, 255 );
			DrawList->AddRectFilled( ImVec2( X, Y ), ImVec2( std::min( X + Cell, BottomRight.x ), std::min( Y + Cell, BottomRight.y ) ), Checker );
		}
		// 寿命開始時の既存RGBA。
		const FColor Start = Emitter.StartColor.ToColor();
		// 寿命終了時の既存RGBA。
		const FColor End = Emitter.EndColor.ToColor();
		for ( int32 Index = 0; Index < 64; ++Index )
		{
			// 現在区間の始点と終点。
			const float32 T0 = static_cast<float32>( Index ) / 64.0f;
			const float32 T1 = static_cast<float32>( Index + 1 ) / 64.0f;
			// 区間両端のColor倍率。
			const FColor Scale0 = Emitter.Appearance.EvaluateColorScale( T0 );
			const FColor Scale1 = Emitter.Appearance.EvaluateColorScale( T1 );
			// 基本色へ倍率を掛け、preview表示範囲へ収めた色。
			const FColor Color0 = ClampPreviewColor( FColor::Lerp( Start, End, T0 ) * Scale0 );
			const FColor Color1 = ClampPreviewColor( FColor::Lerp( Start, End, T1 ) * Scale1 );
			// 区間両端の画面X座標。隣接区間と重ねない。
			const float32 X0 = Origin.x + Width * T0;
			const float32 X1 = Origin.x + Width * T1;
			DrawList->AddRectFilledMultiColor( ImVec2( X0, Origin.y ), ImVec2( X1, BottomRight.y ), ImGui::ColorConvertFloat4ToU32( ImVec4( Color0.r, Color0.g, Color0.b, Color0.a ) ), ImGui::ColorConvertFloat4ToU32( ImVec4( Color1.r, Color1.g, Color1.b, Color1.a ) ), ImGui::ColorConvertFloat4ToU32( ImVec4( Color1.r, Color1.g, Color1.b, Color1.a ) ), ImGui::ColorConvertFloat4ToU32( ImVec4( Color0.r, Color0.g, Color0.b, Color0.a ) ) );
		}
	}
}

// 寿命倍率を編集し、ベースの色・サイズや材質設定を初期化しない。
bool NEffectEditorUI::DrawAppearancePanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
{
	// このパネルで編集する寿命倍率。
	FEffectAppearanceDefinition& Appearance = Emitter.Appearance;
	// このframeで保存定義へ反映する変更の有無。
	bool bChanged = false;
	ImGui::SeparatorText( "Appearance curves" );
	ImGui::TextDisabled( "Curves multiply the existing Start/End size and color." );
	if ( ImGui::SmallButton( "Reset appearance" ) ) { Appearance = FEffectAppearanceDefinition(); bChanged = true; State.Document.EndEditSession(); }
	bChanged |= NEffectEditorUI::DrawScalarCurve( "Size multiplier", Appearance.Size, State.Document, 1.0f, 0.0f, 32.0f );
	ImGui::TextDisabled( "No size keys: the existing Start/End size is unchanged." );
	bChanged |= NEffectEditorUI::DrawColorCurve( "Color multiplier", Appearance.Color, State.Document );
	ImGui::TextDisabled( "No color keys: the existing Start/End color is unchanged." );
	ImGui::TextWrapped( "Color curve multiplies Start/End color. Set Start/End to white to specify the full color with the curve." );
	DrawAppearancePreview( Emitter );
	return bChanged;
}
