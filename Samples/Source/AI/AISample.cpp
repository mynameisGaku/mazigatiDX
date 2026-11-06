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

	// 同じ C++ のクラス（CAIGuard）に、別のツリー（JSON）を渡すだけで、別の敵・NPC になる（コードは書かない）:
	//   Scout = 遠くから見つけて、近づきすぎずに付きまとう偵察兵（Scout.gxbt）。Villager = 巡回するだけの村人（Villager.gxbt。何も追わない）
	const TSharedPtr<CAIGuard> Scout = SpawnObject<CAIGuard>( FVector2( 1130.0f, 440.0f ), FString( "AI/Scout.gxbt" ), FString( "Scout" ), FColor32( 240, 210, 90 ) );
	if ( Hero.IsValid() && Scout.IsValid() )
	{
		Scout->SetTarget( Hero->GetId() );
	}
	SpawnObject<CAIGuard>( FVector2( 640.0f, 580.0f ), FString( "AI/Villager.gxbt" ), FString( "Villager" ), FColor32( 120, 200, 130 ) );

	// 負荷の確認用の起動引数 --ai-count=300: 見張りを、さらに指定した数だけ、舞台いっぱいに出す（プロファイラーで、AI の更新の時間を見る）
	if ( const TOptional<FString> Requested = NCommandLine::GetValue( "ai-count" ) )
	{
		const int32 Count = Requested.GetValue().ToInt();
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			const float32 X = CAIActor::ArenaMin.x + static_cast<float32>( ( Index * 37 ) % 1150 );
			const float32 Y = CAIActor::ArenaMin.y + 40.0f + static_cast<float32>( ( Index * 53 ) % 380 );
			const TSharedPtr<CAIGuard> Extra = SpawnObject<CAIGuard>( FVector2( X, Y ) );
			if ( Hero.IsValid() && Extra.IsValid() )
			{
				Extra->SetTarget( Hero->GetId() );
			}
		}
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
	Screen.Text( 16, 96, "Guard / Scout / Villager = the same C++ class with three behavior trees (Guard.gxbt, Scout.gxbt, Villager.gxbt)", FColor32::Gray );
	Screen.Text( 16, 116, "F1 > AI debugger shows the states, nodes and blackboard.  AI editor edits the files (a saved file is reloaded while running)", FColor32::Gray );
}
