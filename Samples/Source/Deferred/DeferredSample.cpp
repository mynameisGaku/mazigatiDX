#include "DeferredSample.h"

#include "Library/Framework/Render/Types/RenderTargetTypes.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Deferred/DeferredRig.h"
#include "Source/Deferred/DeferredSources.h"
#include "Source/Deferred/DeferredView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CDeferredSample> Registrar( FSampleInfo { "Deferred", "Deferred: G-buffer and deferred lighting", "Deferred rendering. A night square with 24 moving lights, neon grooves, spheres and pillars is drawn in two ways that give the same picture. Forward lights every object while it is drawn. Deferred first writes what each surface looks like (color, normal, depth, emission) into a G-buffer of four render targets with one draw (multiple outputs), then a lighting pass reads the G-buffer pixel by pixel, rebuilds the world position from the depth and the camera, and lights it, so the light is computed once per pixel. Keys switch the view (final, albedo, normal, position, depth, emission) and the path, and change the exposure; drag to orbit, wheel to zoom", 51 } );

	// 場面の色の描画先の名前
	constexpr const char* SceneColorName = "deferred-scene-color";
	constexpr const char* GBufferName = "deferred-gbuffer";

	const char* ToViewText( int32 Mode )
	{
		switch ( Mode )
		{
		case 1:
			return "Albedo";
		case 2:
			return "Normal";
		case 3:
			return "Position (from the depth)";
		case 4:
			return "Depth";
		case 5:
			return "Emission";
		default:
			return "Final";
		}
	}
}

void CDeferredSample::OnEnter()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FDeferredDemoState>();

	// 場面の色は 16 ビットの浮動小数点（1 を超える明るさを残す）。Forward が直接描くので、奥行きバッファを持つ
	FRenderTargetDesc SceneColorDesc;
	SceneColorDesc.Width = FDeferredDemoState::ViewWidth;
	SceneColorDesc.Height = FDeferredDemoState::ViewHeight;
	SceneColorDesc.bAlpha = false;
	SceneColorDesc.bDepth = true;
	SceneColorDesc.Format = ERenderTargetFormat::Float16;
	SceneColorDesc.Channels = 4;
	State->SceneColor = Assets->CreateRenderTarget( SceneColorName, SceneColorDesc );
	if ( !State->SceneColor.IsValid() || !State->GBuffer.Create( *Assets, GBufferName, FDeferredDemoState::ViewWidth, FDeferredDemoState::ViewHeight ) || !NDeferredSources::CompileAll( *State, *Assets ) )
	{
		return;
	}

	SpawnObject<CDeferredRig>( State );
	SpawnObject<CDeferredView>( State )->SetDrawOrder( 0 );
}

void CDeferredSample::OnSampleDraw( FImmediateDraw& Screen )
{
	if ( !State.IsValid() )
	{
		return;
	}
	Screen.Text( 16, 56, *FString::Printf( "Deferred: %s path, view = %s (1 view, 2 path, 3 time, Q / E exposure, drag = orbit, wheel = zoom, R = auto orbit)", State->bDeferred ? "deferred" : "forward", ToViewText( State->ViewMode ) ), FColor32::Gray );
}
