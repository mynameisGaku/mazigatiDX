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
	const TSampleRegistrar<CShapes3DSample> Registrar( FSampleInfo { "Shapes3D", "Shapes3D: shapes, primitives, buffers", "A night plaza for the 3D shape commands: cones (Cone3D), stars (Pixel3D), and vertex lists drawn as points, lines, line strips, triangles, strips and fans (Primitive3D / PrimitiveIndexed3D), plus images that face the camera with a pivot, four free corners, scale and rotation (BillboardPivot3D, BillboardQuad3D, Sprite3D, SpriteStretch3D, SpritePivot3D). Vertex and index buffers draw a forest (one tree mesh reused with different transforms) and a pond (a grid whose vertices are rewritten every frame). Number keys switch wireframe, fog, culling, the billboard pivot, translucency, lighting and the number of trees.", 38 } );

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
	if ( !NShapes3DScenery::MakeTextures( *Assets, *State ) || !NShapes3DScenery::MakeBuffers( *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CShapes3DRig>( State );
	SpawnObject<CShapes3DView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CShapes3DPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CShapes3DSample::OnSampleTick( float32 DeltaTime )
{
	// 頂点バッファの書き換えは、メインスレッドだけ（シーンの更新は、メインスレッド）
	if ( State.IsValid() )
	{
		NShapes3DScenery::UpdateWater( *State );
	}
	(void)DeltaTime;
}

void CShapes3DSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Shapes3D: keys 1-7 switch the settings, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
