#include "ShadowsSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Shadows/ShadowsPanel.h"
#include "Source/Shadows/ShadowsRig.h"
#include "Source/Shadows/ShadowsScenery.h"
#include "Source/Shadows/ShadowsView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CShadowsSample> Registrar( FSampleInfo { "Shadows", "Shadows: shadow maps, area, depth adjust", "A daytime courtyard for shadow maps: a house, trees, crates, a gapped fence and a walking traveler cast shadows of a moving sun. The shadow map is drawn each frame between BeginShadowMap and EndShadowMap, then used with UseShadowMap. Number keys switch the shadows, the map size, the area the shadows cover, the sun, the depth adjust, a preview of the map (PreviewShadowMap) and a lantern whose light is never shadowed (FLight::ShadowMapSlots).", 39 } );

	// 描画順（小さいほうが奥）: 3D の中庭 → 名札と一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CShadowsSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FShadowsDemoState>();
	if ( !NShadowsScenery::MakeTextures( *Assets, *State ) || !NShadowsScenery::UpdateShadowMap( *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CShadowsRig>( State );
	SpawnObject<CShadowsView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CShadowsPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CShadowsSample::OnSampleTick( float32 DeltaTime )
{
	// 影の絵を作り直すのは、メインスレッドだけ（シーンの更新は、メインスレッド）。2 キーで大きさが替わったとき
	if ( State.IsValid() )
	{
		NShadowsScenery::UpdateShadowMap( *State );
	}
	(void)DeltaTime;
}

void CShadowsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Shadows: keys 1-7 switch the settings, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
