#include "Shapes3DSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Shapes3D/Shapes3DPanel.h"
#include "Source/Shapes3D/Shapes3DRig.h"
#include "Source/Shapes3D/Shapes3DScenery.h"
#include "Source/Shapes3D/Shapes3DView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CShapes3DSample> Registrar( FSampleInfo { "Shapes3D", "Shapes3D: 3D shapes and primitives", "A night plaza for the 3D shape commands: cones (Cone3D), stars (Pixel3D), and vertex lists drawn as points, lines, line strips, triangles, strips and fans (Primitive3D / PrimitiveIndexed3D), plus images that face the camera with a pivot, four free corners, scale and rotation (BillboardPivot3D, BillboardQuad3D, Sprite3D, SpriteStretch3D, SpritePivot3D). Number keys switch wireframe, fog, culling, the billboard pivot, translucency and lighting.", 38 } );

	// 描画順（小さいほうが奥）: 3D の広場 → 名札と一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;
}

void CShapes3DSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FShapes3DDemoState>();
	if ( !NShapes3DScenery::MakeTextures( *Assets, *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CShapes3DRig>( State );
	SpawnObject<CShapes3DView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CShapes3DPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CShapes3DSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Shapes3D: keys 1-6 switch the settings, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
