#include "PostEffectsSample.h"

#include "Library/Core/Interfaces/Render/RenderTargetTypes.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/PostEffects/PostEffectsPanel.h"
#include "Source/PostEffects/PostEffectsRig.h"
#include "Source/PostEffects/PostEffectsSources.h"
#include "Source/PostEffects/PostEffectsView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPostEffectsSample> Registrar( FSampleInfo { "PostEffects", "PostEffects: HDR, bloom, depth fog", "Float render targets and multiple outputs. A night square with glowing orbs and neon grooves is drawn into a float16 render target, so brightness above 1 survives, while the same pixel shader writes the depth into a float32 target as its second output (SV_Target1). Then the bright parts are extracted and blurred into a bloom, mixed with depth fog, and tone mapped (ACES / Reinhard) to the screen. Keys switch the view (8-bit look, bloom only, depth), the tone map, bloom, fog and exposure; drag to orbit, wheel to zoom", 50 } );

	// 描画順（小さいほうが奥）: 3D の広場 → 一覧
	constexpr int32 StageOrder = 0;
	constexpr int32 PanelOrder = 10;

	// 文字の大きさ
	constexpr int32 LabelFontSize = 16;

	// 描画先を作る（奥行きバッファは、3D を描く場面の色だけが持つ）
	TSharedPtr<CRenderTarget> MakeTarget( CAssetManager& Assets, const char* Name, int32 Width, int32 Height, ERenderTargetFormat Format, int32 Channels, bool bDepth )
	{
		FRenderTargetDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.bAlpha = false;
		Desc.bDepth = bDepth;
		Desc.Format = Format;
		Desc.Channels = Channels;
		return Assets.CreateRenderTarget( Name, Desc );
	}
}

void CPostEffectsSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}
	State = MakeShared<FPostEffectsDemoState>();

	// 場面の色は 16 ビットの浮動小数点（1 を超える明るさを残す）、深度は 32 ビットの浮動小数点 1 チャンネル、ブルームは半分の大きさの 16 ビット
	State->SceneColor = MakeTarget( *Assets, "post-effects-color", FPostEffectsDemoState::ViewWidth, FPostEffectsDemoState::ViewHeight, ERenderTargetFormat::Float16, 4, true );
	State->SceneDepth = MakeTarget( *Assets, "post-effects-depth", FPostEffectsDemoState::ViewWidth, FPostEffectsDemoState::ViewHeight, ERenderTargetFormat::Float32, 1, false );
	State->BloomA = MakeTarget( *Assets, "post-effects-bloom-a", FPostEffectsDemoState::BloomWidth, FPostEffectsDemoState::BloomHeight, ERenderTargetFormat::Float16, 4, false );
	State->BloomB = MakeTarget( *Assets, "post-effects-bloom-b", FPostEffectsDemoState::BloomWidth, FPostEffectsDemoState::BloomHeight, ERenderTargetFormat::Float16, 4, false );
	if ( !State->SceneColor.IsValid() || !State->SceneDepth.IsValid() || !State->BloomA.IsValid() || !State->BloomB.IsValid() || !NPostEffectsSources::CompileAll( *State ) )
	{
		return;
	}
	const TSharedPtr<CFont> Font = Assets->GetDefaultFont( LabelFontSize );

	SpawnObject<CPostEffectsRig>( State );
	SpawnObject<CPostEffectsView>( State )->SetDrawOrder( StageOrder );
	SpawnObject<CPostEffectsPanel>( State, Font )->SetDrawOrder( PanelOrder );
}

void CPostEffectsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "PostEffects: keys 1-5 and Q / E switch the settings, drag = orbit, wheel = zoom, R = auto orbit.", FColor32::Gray );
}
