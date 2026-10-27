// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"
#include "EffectEditorCurveInput.h"


#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 入力を離れたとき、連続編集の履歴を区切る。
	bool TrackInputEnd( bool bChanged, TParticleEffectDocument& Document )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) Document.EndEditSession();
		return bChanged;
	}

	// 警告を色付きで表示欄の幅に合わせて折り返す。
	void TextWarningWrapped( const char* Text )
	{
		ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ) );
		ImGui::TextWrapped( "%s", Text );
		ImGui::PopStyleColor();
	}

	// Sprite・Ribbon・Lightning の材質設定と、描画実装の対応状態を表示する。
	bool DrawMaterialValues( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
	{
		FEffectMaterialDefinition& Material = Emitter.Material; // 編集対象の材質コピー。
		bool bChanged = false; // 現在の欄で材質が変更されたか。
		ImGui::SeparatorText( "Effect material" );
		bChanged |= TrackInputEnd( ImGui::Checkbox( "Enable material", &Material.bEnabled ), State.Document );
		ImGui::TextDisabled( "Material effects apply to quads, mesh particles, ribbons and branch lightning." );
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>(); // 描画実装の材質対応状態。
		const bool bRibbonMaterial = Emitter.Path.Kind != FEffectPathDefinition::EKind::None; // 経路材質の描画能力を選ぶ。
		const bool bMeshMaterial = Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None; // 固定mesh材質を選ぶ経路。
		const bool bMaterialSupported = Executor != nullptr && ( bMeshMaterial ? Executor->SupportsEffectMaterialMesh() : ( bRibbonMaterial ? Executor->SupportsEffectMaterialRibbons() : Executor->SupportsEffectMaterials() ) ); // 現在の描画方式で材質を使えるか。
		if ( !bMaterialSupported ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), bMeshMaterial ? "This renderer does not support material mesh particles." : ( bRibbonMaterial ? "This renderer does not support effect-material ribbons." : "Requires the D3D11 effect-material renderer." ) );
		if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon ) ImGui::TextDisabled( "Ribbon UV: U follows path distance, V follows width; scroll follows particle age." );
		ImGui::TextDisabled( "Emission is LDR; bloom is not included." );
		if ( ImGui::SmallButton( "Reset material" ) ) { Material = FEffectMaterialDefinition(); bChanged = true; }
		bChanged |= TrackInputEnd( ImGui::Checkbox( "Dissolve and edge", &Material.bDissolve ), State.Document );
		bChanged |= TrackInputEnd( NDebugUI::FileCombo( "Mask texture", Material.MaskTexture, State.TextureFiles, "(built-in noise)" ), State.Document );
		bChanged |= TrackInputEnd( NDebugUI::InputString( "Mask path", Material.MaskTexture ), State.Document );
		float32 UVScale[2] = { Material.UVScale.x, Material.UVScale.y }; // 精度を保って編集する画像の反復率。
		if ( TrackInputEnd( ImGui::DragFloat2( "UV scale", UVScale, 0.01f, 0.001f, 128.0f, "%.9g", ImGuiSliderFlags_AlwaysClamp ), State.Document ) ) { Material.UVScale = FVector2( UVScale[0], UVScale[1] ); bChanged = true; }
		float32 UVScroll[2] = { Material.UVScroll.x, Material.UVScroll.y }; // 秒ごとの横・縦スクロール量。
		if ( TrackInputEnd( ImGui::DragFloat2( "UV scroll / second", UVScroll, 0.01f, -128.0f, 128.0f, "%.9g", ImGuiSliderFlags_AlwaysClamp ), State.Document ) ) { Material.UVScroll = FVector2( UVScroll[0], UVScroll[1] ); bChanged = true; }
		bChanged |= TrackInputEnd( ImGui::DragFloat( "Mask scale", &Material.MaskScale, 0.01f, 0.001f, 128.0f, "%.9g", ImGuiSliderFlags_AlwaysClamp ), State.Document );
		if ( Material.bDissolve )
		{
			ImGui::TextDisabled( "Dissolve: 0 keeps the mask; 1 removes it." );
			bChanged |= NEffectEditorUI::DrawScalarCurve( "Dissolve curve", Material.Dissolve, State.Document, 0.0f, 0.0f, 1.0f );
			bChanged |= TrackInputEnd( ImGui::DragFloat( "Edge width", &Material.EdgeWidth, 0.005f, 0.0f, 1.0f, "%.9g", ImGuiSliderFlags_AlwaysClamp ), State.Document );
			float32 EdgeColor[4] = { Material.EdgeColor.r / 255.0f, Material.EdgeColor.g / 255.0f, Material.EdgeColor.b / 255.0f, Material.EdgeColor.a / 255.0f }; // 境界色の4成分。
			const bool bEdgeColorChanged = ImGui::ColorEdit4( "Edge color", EdgeColor ); // 実際に色の入力が変わったか。
			bool bEdgeColorFinite = true; // 4成分を整数色へ安全に変換できるか。
			for ( const float32 Channel : EdgeColor ) if ( !NMath::IsFinite( Channel ) ) bEdgeColorFinite = false;
			if ( TrackInputEnd( bEdgeColorChanged, State.Document ) && bEdgeColorFinite )
			{
				Material.EdgeColor = FColor32( static_cast<uint8>( NMath::Clamp( EdgeColor[0], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[1], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[2], 0.0f, 1.0f ) * 255.0f + 0.5f ), static_cast<uint8>( NMath::Clamp( EdgeColor[3], 0.0f, 1.0f ) * 255.0f + 0.5f ) );
				bChanged = true;
			}
			bChanged |= TrackInputEnd( ImGui::DragFloat( "Edge emission", &Material.EdgeEmission, 0.01f, 0.0f, 32.0f, "%.9g", ImGuiSliderFlags_AlwaysClamp ), State.Document );
		}
		bChanged |= NEffectEditorUI::DrawScalarCurve( "Emission curve", Material.Emission, State.Document, 1.0f, 0.0f, 32.0f );
		return bChanged;
	}

	// 四角形・帯材質の元画像UVを歪め、必要なノイズ画像を一度だけ確認する。
	bool DrawUVDistortionValues( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
	{
		// 選択エミッターのUV歪み値。
		FEffectUVDistortionDefinition& Distortion = Emitter.Material.Distortion;
		// 今回のpanel入力で保存定義が変わったか。
		bool bChanged = false;
		ImGui::SeparatorText( "UV distortion" );
		ImGui::TextWrapped( "Distorts the source image only; the dissolve mask stays in place." );
		if ( ImGui::SmallButton( "Reset distortion" ) )
		{
			Distortion = FEffectUVDistortionDefinition();
			State.UVDistortionTextureCache.Reset();
			State.UVDistortionTextureCachePath.Empty();
			State.bUVDistortionTextureLoadAttempted = false;
			State.Document.EndEditSession();
			NDebugUI::RestartEffectPreview( State );
			bChanged = true;
		}
		bChanged |= TrackInputEnd( ImGui::Checkbox( "Enable UV distortion", &Distortion.bEnabled ), State.Document );
		bChanged |= TrackInputEnd( NDebugUI::FileCombo( "Noise texture", Distortion.Texture, State.TextureFiles, "(choose texture)" ), State.Document );
		bChanged |= TrackInputEnd( NDebugUI::InputString( "Noise path", Distortion.Texture ), State.Document );
		ImGui::TextWrapped( "Red and green channels encode horizontal and vertical flow." );
		// 二成分とも有限値なら保存範囲へ収めて表示する。
		const bool bFiniteScale = NMath::IsFinite( Distortion.UVScale.x ) && NMath::IsFinite( Distortion.UVScale.y );
		// 不正値をImGuiへ渡さない一時的な横・縦倍率。
		float32 UVScale[2] = { bFiniteScale ? NMath::Clamp( Distortion.UVScale.x, 0.001f, 128.0f ) : 1.0f, bFiniteScale ? NMath::Clamp( Distortion.UVScale.y, 0.001f, 128.0f ) : 1.0f };
		if ( !Distortion.IsValidSettings() ) TextWarningWrapped( "Invalid distortion values; edit the fields or reset distortion." );
		if ( TrackInputEnd( ImGui::DragFloat2( "Distortion scale", UVScale, 0.01f, 0.001f, 128.0f, "%.6g", ImGuiSliderFlags_AlwaysClamp ), State.Document ) )
		{
			if ( NMath::IsFinite( UVScale[0] ) && NMath::IsFinite( UVScale[1] ) )
			{
				Distortion.UVScale = FVector2( NMath::Clamp( UVScale[0], 0.001f, 128.0f ), NMath::Clamp( UVScale[1], 0.001f, 128.0f ) );
				bChanged = true;
			}
		}
		// 二成分とも有限値なら範囲内で表示する移動速度。
		const bool bFiniteScroll = NMath::IsFinite( Distortion.UVScroll.x ) && NMath::IsFinite( Distortion.UVScroll.y );
		// 不正値をImGuiへ渡さない一時的な横・縦移動量。
		float32 UVScroll[2] = { bFiniteScroll ? NMath::Clamp( Distortion.UVScroll.x, -128.0f, 128.0f ) : 0.0f, bFiniteScroll ? NMath::Clamp( Distortion.UVScroll.y, -128.0f, 128.0f ) : 0.0f };
		if ( TrackInputEnd( ImGui::DragFloat2( "Distortion scroll / second", UVScroll, 0.01f, -128.0f, 128.0f, "%.6g", ImGuiSliderFlags_AlwaysClamp ), State.Document ) )
		{
			if ( NMath::IsFinite( UVScroll[0] ) && NMath::IsFinite( UVScroll[1] ) )
			{
				Distortion.UVScroll = FVector2( NMath::Clamp( UVScroll[0], -128.0f, 128.0f ), NMath::Clamp( UVScroll[1], -128.0f, 128.0f ) );
				bChanged = true;
			}
		}
		bChanged |= NEffectEditorUI::DrawScalarCurve( "Distortion strength", Distortion.Strength, State.Document, 0.05f, 0.0f, 1.0f );
		// 経路なしならエミッターは四角形粒子として描く。
		const bool bMeshPath = Emitter.Path.Kind == FEffectPathDefinition::EKind::None && Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None;
		const bool bQuad = Emitter.Path.Kind == FEffectPathDefinition::EKind::None && Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::None;
		// ribbonや二種類の雷は帯として歪み画像を参照する。
		const bool bRibbonPath = Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon || Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning || Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning;
		// 描画記録が実装されている経路か。
		const bool bSupportedPath = bQuad || bRibbonPath || bMeshPath;
		// 材質と対応経路の両方が有効なら歪みを再生へ使う。
		const bool bCanApply = Distortion.bEnabled && Emitter.Material.bEnabled && bSupportedPath;
		if ( Distortion.bEnabled && !Emitter.Material.bEnabled ) TextWarningWrapped( "Material is disabled; distortion is saved but not applied." );
		if ( Distortion.bEnabled && !bSupportedPath ) TextWarningWrapped( "This path does not support UV distortion; the setting is kept for editing." );
		if ( bRibbonPath ) ImGui::TextWrapped( "Ribbon and lightning use each source particle's age. Noise phase stays continuous along split strips." );
		// 現在登録されている描画実装の歪み対応状態。
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		// 選択中の経路が描画可能な歪み対応を持つか。
		const bool bRendererSupportsDistortion = Executor != nullptr && ( bMeshPath ? Executor->SupportsEffectMaterialMesh() : ( bQuad ? Executor->SupportsEffectUVDistortion() : Executor->SupportsEffectRibbonUVDistortion() ) );
		if ( bCanApply && !bRendererSupportsDistortion ) TextWarningWrapped( "The active renderer does not support UV distortion for this path." );
		// テクスチャをメインスレッドで管理する既存のアセット窓口。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		if ( bCanApply )
		{
			if ( Distortion.Texture != State.UVDistortionTextureCachePath )
			{
				State.UVDistortionTextureCachePath = Distortion.Texture;
				State.UVDistortionTextureCache.Reset();
				State.bUVDistortionTextureLoadAttempted = false;
			}
			if ( Assets != nullptr && !Distortion.Texture.IsEmpty() && !State.bUVDistortionTextureLoadAttempted )
			{
				State.bUVDistortionTextureLoadAttempted = true;
				State.UVDistortionTextureCache = Assets->LoadTexture( Distortion.Texture, false );
			}
			if ( Assets == nullptr ) ImGui::TextDisabled( "Asset manager unavailable; manual editing remains available." );
			else if ( Distortion.Texture.IsEmpty() ) TextWarningWrapped( "Choose a noise texture before previewing or saving." );
			else if ( !State.UVDistortionTextureCache.IsValid() || !State.UVDistortionTextureCache->IsReady() ) TextWarningWrapped( "Noise texture is missing or not ready." );
			else ImGui::TextDisabled( "Noise texture is ready." );
			if ( ImGui::SmallButton( "Recheck noise and restart preview" ) )
			{
				State.UVDistortionTextureCache.Reset();
				State.UVDistortionTextureCachePath.Empty();
				State.bUVDistortionTextureLoadAttempted = false;
				NDebugUI::RestartEffectPreview( State );
			}
			ImGui::TextWrapped( "Restart rechecks this editor's texture reference. Other owners may keep a previously loaded image." );
		}
		else
		{
			State.UVDistortionTextureCache.Reset();
			State.UVDistortionTextureCachePath.Empty();
			State.bUVDistortionTextureLoadAttempted = false;
		}
		return bChanged;
	}
}

// 効果材質を編集する。変更は呼び出し側がエミッター全体と一緒に一度だけ記録する。
bool NEffectEditorUI::DrawMaterialPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
{
	bool bChanged = DrawMaterialValues( State, Emitter );
	bChanged |= DrawUVDistortionValues( State, Emitter );
	return bChanged;
}
