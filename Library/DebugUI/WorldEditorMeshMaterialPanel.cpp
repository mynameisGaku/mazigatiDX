// SPDX-License-Identifier: NOASSERTION
#include "WorldEditorMeshMaterialPanel.h"

#include <initializer_list>

#include "EffectEditorCurveInput.h"
#include "EditorCommon.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 入力終了を呼出側のWorld documentへ通知する。
	bool TrackInputEnd( bool bChanged, bool& bInputEnded )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) bInputEnded = true;
		return bChanged;
	}

	// finite値だけをImGuiへ渡す範囲付き表示値。
	float32 SafeValue( float32 Value, float32 Fallback, float32 Minimum, float32 Maximum )
	{
		return NMath::IsFinite( Value ) ? NMath::Clamp( Value, Minimum, Maximum ) : Fallback;
	}

	// 材質曲線へ決定済みの時刻と値を設定する。
	void SetCurve( FEffectScalarCurve& Curve, std::initializer_list<FEffectScalarCurve::FKey> Keys )
	{
		Curve = FEffectScalarCurve();
		for ( const FEffectScalarCurve::FKey& Key : Keys ) Curve.Keys.Add( Key );
	}

	// 結界の脈動と格子ゆがみを設定し、専用source画像を選ぶ。
	void ApplyBarrierPreset( FMeshMaterialEffectDefinition& Effect, FString& SourceTexture )
	{
		Effect = FMeshMaterialEffectDefinition();
		Effect.Material.bEnabled = true;
		Effect.Material.bDissolve = true;
		Effect.Material.MaskTexture = "Effects/mesh_surface_barrier.tga";
		Effect.Material.UVScale = FVector2( 2.0f, 2.0f );
		Effect.Material.UVScroll = FVector2( 0.0f, 0.05f );
		Effect.Material.EdgeWidth = 0.18f;
		Effect.Material.EdgeColor = FColor32( 72, 238, 255, 255 );
		Effect.Material.EdgeEmission = 3.0f;
		SetCurve( Effect.Material.Dissolve, { { 0.0f, 0.08f }, { 0.5f, 0.26f }, { 1.0f, 0.08f } } );
		SetCurve( Effect.Material.Emission, { { 0.0f, 0.75f }, { 0.45f, 1.7f }, { 0.7f, 0.85f }, { 1.0f, 1.4f } } );
		Effect.Material.Distortion.bEnabled = true;
		Effect.Material.Distortion.Texture = "Effects/uv_distortion_flow.tga";
		Effect.Material.Distortion.UVScale = FVector2( 1.5f, 1.5f );
		Effect.Material.Distortion.UVScroll = FVector2( 0.11f, -0.07f );
		SetCurve( Effect.Material.Distortion.Strength, { { 0.0f, 0.045f }, { 0.5f, 0.09f }, { 1.0f, 0.045f } } );
		Effect.Duration = 2.4f;
		Effect.bLoop = true;
		Effect.Opacity = 0.88f;
		Effect.Blend = EDrawBlend::Alpha;
		SourceTexture = "Effects/mesh_surface_barrier.tga";
	}

	// 溶岩の流れと熱い亀裂の境界を設定し、専用source画像を選ぶ。
	void ApplyLavaPreset( FMeshMaterialEffectDefinition& Effect, FString& SourceTexture )
	{
		Effect = FMeshMaterialEffectDefinition();
		Effect.Material.bEnabled = true;
		Effect.Material.bDissolve = true;
		Effect.Material.UVScale = FVector2( 2.0f, 2.0f );
		Effect.Material.UVScroll = FVector2( 0.04f, -0.17f );
		Effect.Material.MaskScale = 2.2f;
		Effect.Material.EdgeWidth = 0.045f;
		Effect.Material.EdgeColor = FColor32( 255, 190, 48, 255 );
		Effect.Material.EdgeEmission = 1.0f;
		SetCurve( Effect.Material.Dissolve, { { 0.0f, 0.17f }, { 0.55f, 0.48f }, { 1.0f, 0.16f } } );
		SetCurve( Effect.Material.Emission, { { 0.0f, 1.1f }, { 0.32f, 1.4f }, { 0.72f, 0.9f }, { 1.0f, 1.25f } } );
		Effect.Material.Distortion.bEnabled = true;
		Effect.Material.Distortion.Texture = "Effects/uv_distortion_flow.tga";
		Effect.Material.Distortion.UVScale = FVector2( 2.0f, 2.0f );
		Effect.Material.Distortion.UVScroll = FVector2( 0.08f, 0.19f );
		SetCurve( Effect.Material.Distortion.Strength, { { 0.0f, 0.055f }, { 0.45f, 0.12f }, { 1.0f, 0.06f } } );
		Effect.Duration = 1.8f;
		Effect.bLoop = true;
		Effect.Opacity = 1.0f;
		Effect.Blend = EDrawBlend::Add;
		SourceTexture = "Effects/mesh_surface_lava.tga";
	}

	// 曲線UIの終了通知を既存fieldの通知とORで集約する。
	// Label: 曲線欄の表示名。Curve: 編集する保存値。数値: 表示範囲。bInputEnded: 既存通知を保持する。
	bool DrawDefinitionCurve( const char* Label, FEffectScalarCurve& Curve, float32 DefaultValue, float32 Minimum, float32 Maximum, bool& bInputEnded )
	{
		// 曲線入力だけの終了通知。
		bool bCurveInputEnded = false;
		// 曲線値が変わったか。
		const bool bChanged = NEffectEditorUI::DrawScalarCurve( Label, Curve, DefaultValue, Minimum, Maximum, bCurveInputEnded );
		bInputEnded |= bCurveInputEnded;
		return bChanged;
	}

	// 四成分が有限な色入力だけを保存色へ変換する。
	bool IsFiniteColor( const float32 (&Color)[4] )
	{
		return NMath::IsFinite( Color[0] ) && NMath::IsFinite( Color[1] ) && NMath::IsFinite( Color[2] ) && NMath::IsFinite( Color[3] );
	}
}

bool NWorldEditorUI::DrawMaterialEffectDefinitionPanel( FMeshMaterialEffectDefinition& Effect, const TArray<FString>& TextureFiles, bool& bInputEnded )
{
	// 今回のpanelで保存定義が変わったか。
	bool bChanged = false;
	// 入力終了通知をこのpanel内で集約する。
	bInputEnded = false;
	// 材質定義だけを有効化する唯一のflag。
	bChanged |= TrackInputEnd( ImGui::Checkbox( "Enable material effect", &Effect.Material.bEnabled ), bInputEnded );
	// 有限値だけを連続入力へ渡すduration。
	float32 Duration = SafeValue( Effect.Duration, 2.0f, 0.001f, 3600.0f );
	if ( TrackInputEnd( ImGui::DragFloat( "Duration (seconds)", &Duration, 0.02f, 0.001f, 3600.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Effect.Duration = NMath::Clamp( Duration, 0.001f, 3600.0f ); bChanged = true; }
	// ループする保存設定。
	bChanged |= TrackInputEnd( ImGui::Checkbox( "Loop", &Effect.bLoop ), bInputEnded );
	// 有限値だけをopacity入力へ渡す。
	float32 Opacity = SafeValue( Effect.Opacity, 1.0f, 0.0f, 1.0f );
	if ( TrackInputEnd( ImGui::SliderFloat( "Opacity", &Opacity, 0.0f, 1.0f, "%.3f" ), bInputEnded ) ) { Effect.Opacity = NMath::Clamp( Opacity, 0.0f, 1.0f ); bChanged = true; }
	// 許可blendだけを選ぶ番号。
	int32 Blend = Effect.Blend == EDrawBlend::Add ? 1 : 0;
	if ( ImGui::Combo( "Blend", &Blend, "Alpha\0Add\0" ) ) { Effect.Blend = Blend == 1 ? EDrawBlend::Add : EDrawBlend::Alpha; bChanged = true; }
	if ( ImGui::IsItemDeactivatedAfterEdit() ) bInputEnded = true;
	// 内包する全材質fieldを編集する。
	FEffectMaterialDefinition& Material = Effect.Material;
	bChanged |= TrackInputEnd( ImGui::Checkbox( "Dissolve and edge", &Material.bDissolve ), bInputEnded );
	// 画像maskを一覧から選ぶ。
	bChanged |= TrackInputEnd( NDebugUI::FileCombo( "Mask texture", Material.MaskTexture, TextureFiles, "(built-in noise)" ), bInputEnded );
	// 画像maskを相対pathで直接指定する。
	bChanged |= TrackInputEnd( NDebugUI::InputString( "Mask path", Material.MaskTexture ), bInputEnded );
	// 有限値だけをUV scale入力へ渡す。
	float32 UVScale[2] = { SafeValue( Material.UVScale.x, 1.0f, 0.001f, 128.0f ), SafeValue( Material.UVScale.y, 1.0f, 0.001f, 128.0f ) };
	if ( TrackInputEnd( ImGui::DragFloat2( "UV scale", UVScale, 0.01f, 0.001f, 128.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Material.UVScale = FVector2( NMath::Clamp( UVScale[0], 0.001f, 128.0f ), NMath::Clamp( UVScale[1], 0.001f, 128.0f ) ); bChanged = true; }
	// 有限値だけをUV scroll入力へ渡す。
	float32 UVScroll[2] = { SafeValue( Material.UVScroll.x, 0.0f, -128.0f, 128.0f ), SafeValue( Material.UVScroll.y, 0.0f, -128.0f, 128.0f ) };
	if ( TrackInputEnd( ImGui::DragFloat2( "UV scroll / second", UVScroll, 0.01f, -128.0f, 128.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Material.UVScroll = FVector2( NMath::Clamp( UVScroll[0], -128.0f, 128.0f ), NMath::Clamp( UVScroll[1], -128.0f, 128.0f ) ); bChanged = true; }
	// 有限値だけをmask scale入力へ渡す。
	float32 MaskScale = SafeValue( Material.MaskScale, 1.0f, 0.001f, 128.0f );
	if ( TrackInputEnd( ImGui::DragFloat( "Mask scale", &MaskScale, 0.01f, 0.001f, 128.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Material.MaskScale = NMath::Clamp( MaskScale, 0.001f, 128.0f ); bChanged = true; }
	if ( Material.bDissolve )
	{
		ImGui::TextDisabled( "Dissolve: 0 keeps the mask; 1 removes it." );
		bChanged |= DrawDefinitionCurve( "Dissolve curve", Material.Dissolve, 0.0f, 0.0f, 1.0f, bInputEnded );
		// 有限値だけをedge width入力へ渡す。
		float32 EdgeWidth = SafeValue( Material.EdgeWidth, 0.08f, 0.0f, 1.0f );
		if ( TrackInputEnd( ImGui::DragFloat( "Edge width", &EdgeWidth, 0.005f, 0.0f, 1.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Material.EdgeWidth = NMath::Clamp( EdgeWidth, 0.0f, 1.0f ); bChanged = true; }
		// RGBA境界色を有限値で編集する。
		float32 EdgeColor[4] = { Material.EdgeColor.r / 255.0f, Material.EdgeColor.g / 255.0f, Material.EdgeColor.b / 255.0f, Material.EdgeColor.a / 255.0f };
		const bool bColorChanged = ImGui::ColorEdit4( "Edge color", EdgeColor );
		if ( TrackInputEnd( bColorChanged, bInputEnded ) && IsFiniteColor( EdgeColor ) ) { Material.EdgeColor = FColor32( static_cast<uint8>( NMath::Clamp( EdgeColor[0], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[1], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[2], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[3], 0.0f, 1.0f ) * 255.0f + 0.5f ) ); bChanged = true; }
		// 有限値だけをedge emission入力へ渡す。
		float32 EdgeEmission = SafeValue( Material.EdgeEmission, 2.0f, 0.0f, 32.0f );
		if ( TrackInputEnd( ImGui::DragFloat( "Edge emission", &EdgeEmission, 0.01f, 0.0f, 32.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Material.EdgeEmission = NMath::Clamp( EdgeEmission, 0.0f, 32.0f ); bChanged = true; }
	}
	bChanged |= DrawDefinitionCurve( "Emission curve", Material.Emission, 1.0f, 0.0f, 32.0f, bInputEnded );
	// ノイズ画像の設定を独立して編集する。
	FEffectUVDistortionDefinition& Distortion = Material.Distortion;
	bChanged |= TrackInputEnd( ImGui::Checkbox( "Enable UV distortion", &Distortion.bEnabled ), bInputEnded );
	bChanged |= TrackInputEnd( NDebugUI::FileCombo( "Noise texture", Distortion.Texture, TextureFiles, "(choose texture)" ), bInputEnded );
	bChanged |= TrackInputEnd( NDebugUI::InputString( "Noise path", Distortion.Texture ), bInputEnded );
	// 有限値だけを歪みscale入力へ渡す。
	float32 DistortionScale[2] = { SafeValue( Distortion.UVScale.x, 1.0f, 0.001f, 128.0f ), SafeValue( Distortion.UVScale.y, 1.0f, 0.001f, 128.0f ) };
	if ( TrackInputEnd( ImGui::DragFloat2( "Distortion scale", DistortionScale, 0.01f, 0.001f, 128.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Distortion.UVScale = FVector2( NMath::Clamp( DistortionScale[0], 0.001f, 128.0f ), NMath::Clamp( DistortionScale[1], 0.001f, 128.0f ) ); bChanged = true; }
	// 有限値だけを歪みscroll入力へ渡す。
	float32 DistortionScroll[2] = { SafeValue( Distortion.UVScroll.x, 0.0f, -128.0f, 128.0f ), SafeValue( Distortion.UVScroll.y, 0.0f, -128.0f, 128.0f ) };
	if ( TrackInputEnd( ImGui::DragFloat2( "Distortion scroll / second", DistortionScroll, 0.01f, -128.0f, 128.0f, "%.5g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Distortion.UVScroll = FVector2( NMath::Clamp( DistortionScroll[0], -128.0f, 128.0f ), NMath::Clamp( DistortionScroll[1], -128.0f, 128.0f ) ); bChanged = true; }
	bChanged |= DrawDefinitionCurve( "Distortion strength", Distortion.Strength, 0.0f, 0.0f, 1.0f, bInputEnded );
	// 保存定義の問題を編集者へそのまま示す。
	const TArray<FString> Issues = Effect.Validate();
	for ( const FString& Issue : Issues ) ImGui::TextWrapped( "%s", UI_TEXT( Issue ) );
	return bChanged;
}

bool NWorldEditorUI::DrawMeshMaterialEffectPanel( FMeshMaterialEffectDefinition& Effect, FString& SourceTexture, const TArray<FString>& TextureFiles, FMeshRenderer* PreviewRenderer, bool& bInputEnded )
{
	// 今回のmesh panelで保存定義またはsource画像が変わったか。
	bool bChanged = false;
	// preset/resetの入力終了通知を保持する。
	bInputEnded = false;
	ImGui::SeparatorText( "Mesh material effect" );
	ImGui::TextWrapped( "Uses the material Image above; an empty path uses the built-in white image." );
	if ( ImGui::SmallButton( "Pulsing barrier" ) ) { ApplyBarrierPreset( Effect, SourceTexture ); bChanged = true; bInputEnded = true; }
	ImGui::SameLine();
	if ( ImGui::SmallButton( "Flowing lava" ) ) { ApplyLavaPreset( Effect, SourceTexture ); bChanged = true; bInputEnded = true; }
	if ( ImGui::SmallButton( "Reset mesh effect" ) ) { Effect = FMeshMaterialEffectDefinition(); bChanged = true; bInputEnded = true; }
	// 共通の保存定義UIが出した終了通知をmesh側へ足す。
	bool bDefinitionInputEnded = false;
	bChanged |= DrawMaterialEffectDefinitionPanel( Effect, TextureFiles, bDefinitionInputEnded );
	bInputEnded |= bDefinitionInputEnded;
	// 材質mesh描画を実装するexecutorが利用できるか。
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	if ( Effect.Material.bEnabled && ( Executor == nullptr || !Executor->SupportsEffectMaterialMesh() ) ) ImGui::TextWrapped( "The active renderer does not support mesh material effects; this slot is not drawn. Stop to return to the regular mesh." );
	ImGui::TextDisabled( "Mesh material effects are unlit LDR; bloom is not included." );
	if ( PreviewRenderer != nullptr )
	{
		ImGui::SeparatorText( "Preview transport" );
		// transport操作がこのmeshの全effect slotへ共通に働くことを示す。
		ImGui::TextWrapped( "Transport controls apply to every material-effect slot on this mesh." );
		// mesh rendererの全slotに共通する再生状態を表示する。
		const bool bPlaying = PreviewRenderer->IsEffectsPlaying();
		const bool bPaused = PreviewRenderer->IsEffectsPaused();
		const bool bFinished = PreviewRenderer->IsEffectsFinished();
		if ( ImGui::Button( "Play" ) ) PreviewRenderer->PlayEffects();
		ImGui::SameLine();
		if ( ImGui::Button( "Pause" ) ) PreviewRenderer->PauseEffects();
		ImGui::SameLine();
		if ( ImGui::Button( "Stop" ) ) PreviewRenderer->StopEffects();
		ImGui::SameLine();
		if ( ImGui::Button( "Restart" ) ) PreviewRenderer->RestartEffects();
		const char* StateText = bPlaying ? "Playing" : ( bPaused ? "Paused" : ( bFinished ? "Finished (final frame held)" : "Stopped" ) );
		ImGui::TextDisabled( "%s%s", StateText, bPaused ? " (time held)" : "" );
	}
	return bChanged;
}
