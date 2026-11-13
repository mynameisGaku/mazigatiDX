#include "MaterialsSample.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Framework/Render/Types/RenderTargetTypes.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Materials/MaterialsRig.h"
#include "Source/Materials/MaterialsView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CMaterialsSample> Registrar( FSampleInfo { "Materials", "Materials: node graphs, post process, UI",
																	   "Materials made from node graphs (EngineContent/Materials): marble, car paint, rim glow, a checker and six Substrate materials on 3D shapes, lit by a sun with shadows, a point light and a sky environment map; a chain of post-process materials (grayscale, vignette, pixelate) on the scene, and UI materials (a glowing button that reacts to the mouse, a shimmering band). Number keys toggle the post-process passes; drag to orbit, wheel to zoom",
																	   51 } );

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;

	// 影のマップの大きさ（画素）
	constexpr int32 ShadowMapSize = 2048;

	// 組み込みのマテリアルを読む。無い・作れないときは null。
	// 環境マップと影を読む版のシェーダーも、ここ（メインスレッド）で作っておく（描く命令は、ほかのスレッドで記録されることがある）
	TSharedPtr<CMaterial> Load( CMaterialSubsystem& Materials, const char* Name )
	{
		const TSharedPtr<CMaterial> Material = Materials.LoadMaterial( FString::Printf( "Engine:/Materials/%s.gxmaterial", Name ) );
		FMaterialHlslFeatures Features;
		Features.bEnvironmentMap = true;
		Features.bShadowMap = true;
		if ( Material.IsValid() && Material->IsValid() && Material->PrepareShaders( Features ) )
		{
			return Material;
		}
		return TSharedPtr<CMaterial>();
	}
}

void CMaterialsSample::OnEnter()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	CMaterialSubsystem* Materials = GetAppSubsystem<CMaterialSubsystem>();
	if ( Assets == nullptr || Materials == nullptr )
	{
		return;
	}
	State = MakeShared<FMaterialsDemoState>();

	// 場面を描く描画先（3D を描くので、専用の奥行きバッファを持つ）。ポストプロセスは、これを画像として読む
	FRenderTargetDesc Desc;
	Desc.Width = FMaterialsDemoState::ViewWidth;
	Desc.Height = FMaterialsDemoState::ViewHeight;
	Desc.bAlpha = false;
	Desc.bDepth = true;
	State->SceneTarget = Assets->CreateRenderTarget( "materials-scene", Desc );

	State->Checker = Load( *Materials, "Surface/Checker" );
	State->Marble = Load( *Materials, "Surface/Marble" );
	State->CarPaint = Load( *Materials, "Surface/CarPaint" );
	State->RimGlow = Load( *Materials, "Surface/RimGlow" );
	const char* SubstrateNames[FMaterialsDemoState::NumSubstrateSpheres] = { "Surface/BrushedMetal", "Surface/Pearl", "Surface/Wax", "Surface/RustedMetal", "Surface/CandyPaint", "Surface/HammeredMetal" };
	bool bSubstrateLoaded = true;
	for ( int32 Index = 0; Index < FMaterialsDemoState::NumSubstrateSpheres; ++Index )
	{
		State->SubstrateSpheres[Index] = Load( *Materials, SubstrateNames[Index] );
		bSubstrateLoaded = bSubstrateLoaded && State->SubstrateSpheres[Index].IsValid();
	}
	State->GlowButton = Load( *Materials, "UI/GlowButton" );
	State->Shimmer = Load( *Materials, "UI/Shimmer" );
	const TSharedPtr<CMaterial> Grayscale = Load( *Materials, "PostProcess/Grayscale" );
	const TSharedPtr<CMaterial> Vignette = Load( *Materials, "PostProcess/Vignette" );
	const TSharedPtr<CMaterial> Pixelate = Load( *Materials, "PostProcess/Pixelate" );
	// 場面の光: 空の絵から作る環境マップと、太陽の影。無くても、場面は描ける（空と地面の色の映り込みになり、影は落ちない）
	State->Environment = CMaterialEnvironmentMap::Load( *Assets, "Engine:/Environments/Sky.png" );
	State->Shadow = CMaterialShadowMap::Create( *Assets, "materials-shadow", ShadowMapSize );
	if ( !bSubstrateLoaded || !State->SceneTarget.IsValid() || !State->Checker || !State->Marble || !State->CarPaint || !State->RimGlow || !State->GlowButton || !State->Shimmer || !Grayscale || !Vignette || !Pixelate )
	{
		State.Reset();
		return;
	}
	// 周りを暗くする効果だけ、最初から入れておく
	State->PostStack.Add( Grayscale );
	State->PostStack.Add( Vignette );
	State->PostStack.Add( Pixelate );
	State->PostStack.GetEntry( 0 ).bEnabled = false;
	State->PostStack.GetEntry( 2 ).bEnabled = false;

	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );
	SpawnObject<CMaterialsRig>( State );
	SpawnObject<CMaterialsView>( State, Font )->SetDrawOrder( 0 );
}

void CMaterialsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Materials: keys 1-3 toggle post-process passes, 4 = time, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
