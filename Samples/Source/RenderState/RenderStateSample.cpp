#include "RenderStateSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/RenderState/InfoPanel.h"
#include "Source/RenderState/RenderStateRig.h"
#include "Source/RenderState/StageScenery.h"
#include "Source/RenderState/StageView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CRenderStateSample> Registrar( FSampleInfo { "RenderState", "RenderState: depth, culling, fog, textures", "A 3D stage for the render state commands: depth buffer (translucent glass), culling (a room), texture address and transform (walls, water), distance and height fog, filter and anisotropy (a checkered ground), the world matrix stack (an arm) and clearing depth (a beam). Number keys switch the states.", 36 } );

	// 描画順（小さいほうが奥）: 3D の舞台 → 名札と一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CRenderStateSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FRenderStateDemoState>();
	if ( !NStageScenery::MakeTextures( *Assets, *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CRenderStateRig>( State );
	SpawnObject<CStageView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CInfoPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CRenderStateSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "RenderState: keys 1-8 switch the states, drag = orbit (low angle shows anisotropy), wheel = zoom, R = auto orbit.", FColor32::Gray );
}
