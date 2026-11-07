#include "EffectsSample.h"

#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Effects/EffectsGallery.h"
#include "Source/Effects/EffectsState.h"
#include "Source/Effects/EffectsView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CEffectsSample> Registrar( FSampleInfo { "Effects", "Effects: particles and lightning", "Play .gxeffect files (flame, sparks, aura, ribbon trail, lightning) on a night stage. Left / Right selects, Space restarts, the mouse moves the lightning and the ribbon.", 95 } );
}

void CEffectsSample::OnEnter()
{
	State = MakeShared<FEffectsState>();

	// 描く順: 舞台（エフェクトもここで記録される）。台帳（再生を作って、動かす）は、描かない
	SpawnObject<CEffectsGallery>( State );
	SpawnObject<CEffectsView>( State )->SetDrawOrder( 0 );
}

void CEffectsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 64, "Left / Right: choose   Space: restart   Up / Down: scale   Mouse: move the lightning end and the ribbon", FColor32::Gray );
}
