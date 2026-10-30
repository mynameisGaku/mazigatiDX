#include "CameraSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Camera/CameraRig.h"
#include "Source/Camera/LabelOverlay.h"
#include "Source/Camera/MainView.h"
#include "Source/Camera/Scenery.h"
#include "Source/Camera/TopView.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CCameraSample> Registrar( FSampleInfo { "Camera", "Camera: 3D cameras, projection and picking", "An orbit camera (perspective / orthographic, field of view) looking at a 3D scene, a second top-down camera drawn into a render target, labels placed with WorldToScreen, mouse picking with ScreenToRay, and frustum culling.", 35 } );

	// 描画順（小さいほうが奥）: メインの 3D → 上から見る図 → 名前と説明
	constexpr int32 MainViewOrder = 0;
	constexpr int32 TopViewOrder = 10;
	constexpr int32 LabelOrder = 20;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CCameraSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 上から見る図の描画先（深度（Z バッファ）つき。3D を描くので）
	FRenderTargetDesc TopDesc;
	TopDesc.Width = FCameraDemoState::TopViewSize;
	TopDesc.Height = FCameraDemoState::TopViewSize;
	TopDesc.bAlpha = false;
	TopDesc.bDepth = true;
	const TSharedPtr<CRenderTarget> TopTarget = Assets->CreateRenderTarget( "Samples/Camera/TopView", TopDesc );
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	State = MakeShared<FCameraDemoState>();
	State->Objects = NScenery::MakeObjects();
	State->Visible.SetNum( State->Objects.Num(), false );

	SpawnObject<CCameraRig>( State );
	SpawnObject<CMainView>( State )->SetDrawOrder( MainViewOrder );
	SpawnObject<CTopView>( State, TopTarget )->SetDrawOrder( TopViewOrder );
	SpawnObject<CLabelOverlay>( State, Font )->SetDrawOrder( LabelOrder );
}

void CCameraSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Camera: drag = orbit, wheel / W S = zoom, A D = rotate, Q E = field of view, P = perspective / orthographic, R = auto orbit.", FColor32::Gray );
}
