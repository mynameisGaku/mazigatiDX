#include "ShadersSample.h"

#include "Library/Core/Interfaces/Render/RenderTargetTypes.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Shaders/ShadersPanel.h"
#include "Source/Shaders/ShadersRig.h"
#include "Source/Shaders/ShadersSources.h"
#include "Source/Shaders/ShadersView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CShadersSample> Registrar( FSampleInfo { "Shaders", "Shaders: toon, ripples, post effects", "Your own HLSL shaders on 3D shapes and a full-screen effect. A garden of spheres and a pillar is drawn into a render target with a vertex shader (matrices from shader constants) and pixel shaders (toon shading, normal colors, depth, wobble, animated ripples, checker), then the target is shown through a post-effect pixel shader (grayscale, vignette, mosaic, wave) with ShaderRect. Number keys switch the shaders; drag to orbit, wheel to zoom", 49 } );

	// 描画順（小さいほうが奥）: 3D の庭 → 一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CShadersSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FShadersDemoState>();

	// 場面を描く描画先（3D を描くので、専用の奥行きバッファを持つ）。ポストエフェクトは、これを画像として読む
	FRenderTargetDesc Desc;
	Desc.Width = FShadersDemoState::ViewWidth;
	Desc.Height = FShadersDemoState::ViewHeight;
	Desc.bAlpha = false;
	Desc.bDepth = true;
	State->SceneTarget = Assets->CreateRenderTarget( "shaders-scene", Desc );
	if ( !State->SceneTarget.IsValid() || !NShadersSources::CompileAll( *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CShadersRig>( State );
	SpawnObject<CShadersView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CShadersPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CShadersSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Shaders: keys 1-4 switch the shaders, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
