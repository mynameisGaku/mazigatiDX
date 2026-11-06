// SPDX-License-Identifier: MIT
#include "AISample.h"

#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Platform/System/CommandLine.h"
#include "Source/AI/AIActor.h"
#include "Source/AI/AIGuard.h"
#include "Source/AI/AIHero.h"
#include "Source/AI/AIParts.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CAISample> Registrar( FSampleInfo { "AI", "AI: state machine and behavior tree", "A hero driven by a state machine (Hero.gxstate) and a guard driven by a behavior tree (Guard.gxbt). Open the AI debugger from F1.", 70 } );
}

void CAISample::OnEnter()
{
	// ゲーム固有の部品（Halt / Move / MoveToTarget / SensePlayer など）を、名前で登録する
	NAISample::RegisterParts();

	// 確認用の起動引数 --hero-x=700: 主人公の最初の位置（見張りの近くから始めて、追う・殴るを見たいとき）
	float32 HeroX = 260.0f;
	if ( const TOptional<FString> Requested = NCommandLine::GetValue( "hero-x" ) )
	{
		HeroX = static_cast<float32>( Requested.GetValue().ToFloat() );
	}
	const TSharedPtr<CAIHero> Hero = SpawnObject<CAIHero>( FVector2( HeroX, 520.0f ) );
	const TSharedPtr<CAIGuard> Guard = SpawnObject<CAIGuard>( FVector2( 900.0f, 520.0f ) );
	if ( Hero.IsValid() && Guard.IsValid() )
	{
		Guard->SetTarget( Hero->GetId() );
	}
}

void CAISample::OnSampleDraw( FImmediateDraw& Screen )
{
	const int32 Left = static_cast<int32>( CAIActor::ArenaMin.x );
	const int32 Top = static_cast<int32>( CAIActor::ArenaMin.y );
	const int32 Right = static_cast<int32>( CAIActor::ArenaMax.x );
	const int32 Bottom = static_cast<int32>( CAIActor::ArenaMax.y );
	// 舞台の枠（アクターを隠さないよう、塗らない）
	Screen.Box( Left - 20, Top + 60, Right + 20, Bottom + 60, FColor32::Gray, false );

	Screen.Text( 16, 56, "A / D (arrows): walk    Space / J: attack", FColor32::Gray );
	Screen.Text( 16, 76, "Hero = state machine (Hero.gxstate)", FColor32::Gray );
	Screen.Text( 16, 96, "Guard = behavior tree (Guard.gxbt)", FColor32::Gray );
	Screen.Text( 16, 116, "F1 > AI debugger shows the states, nodes and blackboard", FColor32::Gray );
}
