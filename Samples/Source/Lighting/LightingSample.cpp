#include "LightingSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Lighting/LightingPanel.h"
#include "Source/Lighting/LightingRig.h"
#include "Source/Lighting/LightingScenery.h"
#include "Source/Lighting/LightingView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CLightingSample> Registrar( FSampleInfo { "Lighting", "Lighting: lights, materials, ambient", "A night courtyard for the lighting commands: a directional light (moon), a point light (an orbiting lamp), a spotlight, street lamps (6 lights at most), ambient light, surface materials (matte, shiny, glowing, plastic, gold), pixel lighting, specular highlights and switching lighting off. Number keys switch the lighting.", 37 } );

	// 描画順（小さいほうが奥）: 3D の中庭 → 名札と一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CLightingSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FLightingDemoState>();
	if ( !NLightingScenery::MakeTextures( *Assets, *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CLightingRig>( State );
	SpawnObject<CLightingView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CLightingPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CLightingSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Lighting: keys 1-9 switch the lights, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
