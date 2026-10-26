#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "WorldEffectSelfTestSupport.h"

using namespace NAssetTest;
using namespace NEffectTest;

//=============================================================================
// エフェクトのシーンでの確認（偽のバックエンド）
//   アセットの保存と読み込み・部品（CEffectComponent）・ワールドの Effect 部品の設置・描画システムが記録するコマンド。
//   フレームをまたぐので、3 つの関数に分けてある: 始める（設置）-> 確認する（2 フレーム以上あと）-> 片付けの確認（さらに 2 フレーム以上あと）。
//=============================================================================
namespace
{
	// フレームをまたいで持つもの（確認用の静的な置き場）
	struct FEffectSceneState
	{
		FWorldInstance Level;
		TSharedPtr<CGameObject> Standalone;   // 部品を直接付けたオブジェクト
		TSharedPtr<CGameObject> Kept;         // 終わっても消さないオブジェクト
		TSharedPtr<CGameObject> Detached;     // 部品を取り外すオブジェクト
		TSharedPtr<CGameObject> Follower;     // 位置に追従する確認用
		TSharedPtr<CGameObject> Popper;       // 終わったら消える確認用
		TSharedPtr<CGameObject> Streamer;     // 動かしながら出す確認用
		TSharedPtr<CTexture> Texture;
		TWeakPtr<IEffectPlayback> FirePlayback;
		FVector3 FirePosition = FVector3::Zero;
	};
	FEffectSceneState GState;

	FParticleEmitterDefinition BurstEmitter( int32 Count )
	{
		FParticleEmitterDefinition E = QuietEmitter();
		E.Bursts.Add( Burst( 0.0f, Count ) );
		return E;
	}

	bool SaveEffect( CAssetManager& Assets, const char* Path, const FParticleEffectDefinition& Definition )
	{
		return Assets.SaveEffect( Path, Definition );
	}

	FWorldObject MakeEffectObject( int32 Id, const char* Name, const char* Path, const FVector3& Position )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Transform.Position = Position;
		FWorldEffect Effect;
		Effect.Path = Path;
		Object.Effect = Effect;
		return Object;
	}
}

void NWorldTest::RunEffectSceneStart( FChecker& C, CScene& Scene, CAssetManager& Assets, const FWorldSpawnerRegistry& Registry )
{
	FEffectSceneState& G = GState;
	G = FEffectSceneState();

	//-------------------------------------------------------------------------
	// アセット
	//-------------------------------------------------------------------------
	C.Group = "effect-asset";
	// 火: 加算・一定の色（始めと終わりが同じ。アルファ 128 は、加算では暗さになる）・大きさ 1・出たところで止まる・5 つ
	FParticleEmitterDefinition Fire = BurstEmitter( 5 );
	Fire.Blend = EParticleBlend::Add;
	Fire.StartColor = FColor32( 200, 100, 50, 200 );
	Fire.EndColor = Fire.StartColor;
	FParticleEffectDefinition FireEffect;
	FireEffect.Name = "Fire";
	FireEffect.Seed = 3;
	FireEffect.Emitters.Add( Fire );
	// 煙と火花: 1 つ目は透明な煙（画像あり）3 つ、2 つ目は加算の火花 2 つ
	FParticleEmitterDefinition Smoke = BurstEmitter( 3 );
	Smoke.Blend = EParticleBlend::Alpha;
	Smoke.Texture = "a.png";
	Smoke.StartColor = FColor32( 255, 255, 255, 200 );
	Smoke.EndColor = Smoke.StartColor;
	FParticleEmitterDefinition Spark = BurstEmitter( 2 );
	Spark.Blend = EParticleBlend::Add;
	Spark.StartColor = FColor32( 255, 255, 255, 255 );
	Spark.EndColor = Spark.StartColor;
	FParticleEffectDefinition MixedEffect;
	MixedEffect.Emitters.Add( Smoke );
	MixedEffect.Emitters.Add( Spark );
	// 使い切り: 0.25 秒だけ出して、粒子の寿命も 0.25 秒
	FParticleEmitterDefinition Pop = BurstEmitter( 1 );
	Pop.Lifetime = { 0.25f, 0.25f };
	FParticleEffectDefinition PopEffect;
	PopEffect.Duration = 0.25f;
	PopEffect.bLoop = false;
	PopEffect.Emitters.Add( Pop );
	// 流れ: 1 回の更新（0.125 秒）ごとに 1 つ、そのときの位置に出る。動かないで長生き
	FParticleEmitterDefinition Stream = QuietEmitter();
	Stream.Rate = 8.0f;
	FParticleEffectDefinition StreamEffect;
	StreamEffect.Emitters.Add( Stream );
	// 見えないもの: アルファ 0 の粒子と、大きさ 0 の粒子
	FParticleEmitterDefinition Clear = BurstEmitter( 3 );
	Clear.StartColor = FColor32( 255, 255, 255, 0 );
	Clear.EndColor = Clear.StartColor;
	FParticleEmitterDefinition Tiny = BurstEmitter( 2 );
	Tiny.StartSize = { 0.0f, 0.0f };
	Tiny.EndSize = { 0.0f, 0.0f };
	FParticleEffectDefinition InvisibleEffect;
	InvisibleEffect.Emitters.Add( Clear );
	InvisibleEffect.Emitters.Add( Tiny );
	// 画像が読めないエミッター
	FParticleEmitterDefinition Lost = BurstEmitter( 1 );
	Lost.Texture = "missing.png";
	FParticleEffectDefinition LostEffect;
	LostEffect.Emitters.Add( Lost );
	C.Check( SaveEffect( Assets, "fx/fire.effect.json", FireEffect ) && SaveEffect( Assets, "fx/mixed.effect.json", MixedEffect ) && SaveEffect( Assets, "fx/pop.effect.json", PopEffect ) && SaveEffect( Assets, "fx/lost.effect.json", LostEffect ) && SaveEffect( Assets, "fx/stream.effect.json", StreamEffect ) && SaveEffect( Assets, "fx/invisible.effect.json", InvisibleEffect ), "the effects are saved" );

	const TSharedPtr<CEffectAsset> Loaded = Assets.LoadEffect( "fx/fire.effect.json" );
	C.Check( Loaded.IsValid() && Loaded->GetAssetType() == EAssetType::Effect && Loaded->GetDefinition().ToJson() == FireEffect.ToJson() && Loaded->GetLoadWarnings().IsEmpty() && Loaded->GetDefinition().Seed == 3u && Loaded->GetDefinition().Name == "Fire",
			 "the saved effect loads back identical (%d load warnings)", Loaded.IsValid() ? Loaded->GetLoadWarnings().Num() : -1 );
	C.Check( Loaded.IsValid() && Assets.LoadEffect( "fx/fire.effect.json" ) == Loaded && Loaded->GetFileBytes() > 0 && Loaded->GetMemoryBytes() > 0 && Loaded->Describe().Contains( "1 emitter(s)" ) && Loaded->Describe().Contains( "endless" ),
			 "loading it again shares it; it knows its file size and memory, and describes itself (%s)", Loaded.IsValid() ? *Loaded->Describe() : "?" );
	Assets.UpdateStats();
	C.Check( !Assets.LoadEffect( "fx/none.effect.json" ).IsValid() && Assets.GetStats().Get( EAssetType::Effect ).Count == 1 && FString( ToString( EAssetType::Effect ) ) == "Effect", "a missing effect does not load; the statistics count the loaded one" );
	// 壊れたファイル: 読み込めない（JSON でない・オブジェクトでない）
	WriteFileText( Assets.GetRootDirectory() / "fx" / "broken.effect.json", "{" );
	WriteFileText( Assets.GetRootDirectory() / "fx" / "notobject.effect.json", "[1, 2]" );
	C.Check( !Assets.LoadEffect( "fx/broken.effect.json" ).IsValid() && !Assets.LoadEffect( "fx/notobject.effect.json" ).IsValid(), "a file that is not JSON, and one that is not an object, do not load" );
	WriteFileText( Assets.GetRootDirectory() / "fx" / "odd.effect.json", "{\"Emitters\":[{\"Blend\":\"Mul\"}]}" );
	const TSharedPtr<CEffectAsset> Odd = Assets.LoadEffect( "fx/odd.effect.json" );
	C.Check( Odd.IsValid() && Odd->GetLoadWarnings().Num() == 1 && Odd->GetDefinition().Emitters.Num() == 1, "a file with an unreadable item loads, with the warning kept on the asset" );
	// 保存したあとは、新しい内容を読む
	FParticleEffectDefinition Changed = FireEffect;
	Changed.Name = "Changed";
	C.Check( Assets.SaveEffect( "fx/fire.effect.json", Changed ), "an effect is saved over" );
	const TSharedPtr<CEffectAsset> Reloaded = Assets.LoadEffect( "fx/fire.effect.json" );
	C.Check( Reloaded.IsValid() && Reloaded != Loaded && Reloaded->GetDefinition().Name == "Changed", "and the next load reads the new content (the old one is not shared)" );
	C.Check( Assets.SaveEffect( "fx/fire.effect.json", FireEffect ), "(put the fire back)" );

	// 設置されたときの再生の確認用: 火の定義の識別
	G.Texture = Assets.LoadTexture( "a.png" );

	//-------------------------------------------------------------------------
	// バックエンド（本物の一覧 + 偽のアセット）
	//-------------------------------------------------------------------------
	C.Group = "effect-playback";
	const FEffectBackendRegistry Backends = FEffectBackendRegistry::CreateWithBuiltins();
	FEffectPlayParams Params;
	Params.Scale = 2.0f;
	const TSharedPtr<IEffectPlayback> Playback = Backends.Play( "fx/fire.effect.json", &Assets, Params, FTransform( FVector3( 1.0f, 2.0f, 3.0f ) ) );
	C.Check( Playback.IsValid() && Playback->GetNumParticles() == 0 && !Playback->IsFinished(), "the particle backend plays a saved effect (nothing is born before the first update)" );
	if ( Playback.IsValid() )
	{
		Playback->Update( 0.125f );
		C.Check( Playback->GetNumParticles() == 5, "five particles after the first update" );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Playback->Record( Draw );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		int32 Blends = 0, Quads = 0;
		bool bAdd = false, bPositioned = true, bSized = true, bColour = true;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Blend )
			{
				++Blends;
				bAdd = Command.Read<FDrawBlendData>().Mode == static_cast<int32>( EDrawBlend::Add );
			}
			else if ( Command.Type == EDrawCommand::Quad3D )
			{
				const FDrawQuad3DData D = Command.Read<FDrawQuad3DData>();
				++Quads;
				bPositioned = bPositioned && Near3( FVector3( D.Pos[0], D.Pos[1], D.Pos[2] ), FVector3( 1.0f, 2.0f, 3.0f ) );
				bSized = bSized && D.Width == 2.0f && D.Height == 2.0f && D.PivotX == 0.5f && D.PivotY == 0.5f && D.Orient == static_cast<int32>( EDrawQuadOrient::Camera ) && D.Handle >= 0;
				bColour = bColour && D.Color.R == 157 && D.Color.G == 78 && D.Color.B == 39 && D.Color.A == 255;
			}
		} );
		C.Check( Blends == 1 && bAdd && Quads == 5, "Record writes one Blend (Add) for the emitter and one Quad3D for each particle (%d blends, %d quads)", Blends, Quads );
		C.Check( bPositioned && bSized, "each quad is at the effect's position, with the size multiplied by the play scale (1 x 2), centred, facing the camera, with an image" );
		C.Check( bColour, "an additive particle's colour is darkened by its alpha, rounded (200, 100, 50 at alpha 200 -> 157, 78, 39), and drawn fully opaque" );
		// 止めて、全部消す
		Playback->Stop( true );
		C.Check( Playback->GetNumParticles() == 0 && Playback->IsFinished(), "Stop immediately clears it" );
		FDrawBuffer Empty;
		FDrawContext EmptyDraw( Empty );
		Playback->Record( EmptyDraw );
		C.Check( Empty.Words.empty(), "and nothing is recorded" );
	}
	C.Check( !Backends.Play( "fx/none.effect.json", &Assets, FEffectPlayParams(), FTransform() ).IsValid() && !Backends.Play( "fx/fire.effect.json", nullptr, FEffectPlayParams(), FTransform() ).IsValid(), "a missing effect, or no asset manager: nothing is played" );
	// 見えない粒子（アルファ 0・大きさ 0）は、四角形も、ブレンドの指定も出さない
	const TSharedPtr<IEffectPlayback> Invisible = Backends.Play( "fx/invisible.effect.json", &Assets, FEffectPlayParams(), FTransform() );
	if ( Invisible.IsValid() )
	{
		Invisible->Update( 0.125f );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Invisible->Record( Draw );
		C.Check( Invisible->GetNumParticles() == 5 && Buffer.Words.empty(), "particles with alpha 0 or size 0 are alive but record nothing (no quads and no blend change; %d particles)", Invisible->GetNumParticles() );
	}
	else C.Check( false, "the invisible effect plays" );
	// 画像が読めなくても再生される（組み込みの丸が代わりになる）
	const TSharedPtr<IEffectPlayback> LostPlayback = Backends.Play( "fx/lost.effect.json", &Assets, FEffectPlayParams(), FTransform() );
	if ( LostPlayback.IsValid() )
	{
		LostPlayback->Update( 0.125f );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		LostPlayback->Record( Draw );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		C.Check( Log.Count( EDrawCommand::Quad3D ) == 1, "an emitter whose image cannot be loaded is drawn with the built-in soft dot" );
	}
	else C.Check( false, "an effect with an unreadable image still plays" );

	//-------------------------------------------------------------------------
	// 部品（CEffectComponent）を直接付ける
	//-------------------------------------------------------------------------
	C.Group = "effect-component";
	G.Standalone = Scene.SpawnObject<CGameObject>();
	G.Standalone->AddComponent<CTransformComponent>()->SetLocalTransform( FTransform( FVector3( 4.0f, 5.0f, 6.0f ) ) );
	CEffectComponent* Fx = G.Standalone->AddComponent<CEffectComponent>( "fx/fire.effect.json" );
	C.Check( Fx != nullptr && Fx->GetPath() == "fx/fire.effect.json" && !Fx->GetPlayback().IsValid() && !Fx->IsPlaying() && !Fx->IsFinished() && G.Standalone->GetData<FEffectPlayer>() == nullptr, "before the first tick nothing is playing, and it is not finished" );
	Fx->Tick( 0.125f );
	const TSharedPtr<IEffectPlayback> First = Fx->GetPlayback();
	const FEffectPlayer* Player = G.Standalone->GetData<FEffectPlayer>();
	C.Check( First.IsValid() && First->GetNumParticles() == 5 && Fx->IsPlaying() && Player != nullptr && Player->Playback == First && Player->bAutoDraw, "the first tick starts it on its own (auto play), and puts the drawing data on the object" );
	Fx->Tick( 0.125f );
	C.Check( Fx->GetPlayback() == First && First->GetNumParticles() == 5, "later ticks keep the same playback" );
	Fx->Stop();
	Fx->Tick( 0.125f );
	C.Check( Fx->GetPlayback() == First && !Fx->IsFinished(), "Stop keeps what is alive (the particles live long)" );
	C.Check( Fx->Play() && Fx->GetPlayback() != First && Fx->GetPlayback()->GetNumParticles() == 0 && G.Standalone->GetData<FEffectPlayer>() != nullptr && G.Standalone->GetData<FEffectPlayer>()->Playback == Fx->GetPlayback(), "Play starts a fresh one, also after Stop, and the drawing data follows it" );
	const TSharedPtr<IEffectPlayback> Fresh = Fx->GetPlayback();
	Fx->Tick( 0.125f );
	C.Check( Fx->GetPlayback() == Fresh && Fx->GetPlayback()->GetNumParticles() == 5, "which makes its five particles on the next tick, as the same playback (it is not started a second time by the auto play)" );
	Fx->SetPath( "fx/none.effect.json" );
	C.Check( !Fx->Play() && !Fx->GetPlayback().IsValid() && G.Standalone->GetData<FEffectPlayer>() != nullptr && !G.Standalone->GetData<FEffectPlayer>()->Playback.IsValid(), "Play of a missing effect fails, and leaves nothing playing and nothing to draw" );
	Fx->SetPath( "fx/fire.effect.json" );
	C.Check( Fx->Play() && Fx->IsPlaying(), "存在しないファイルで失敗したあとも、有効なエフェクトを再生できる" );
	Fx->Tick( 0.125f );
	// 空のパスでも、失敗前に再生していたものと描画の参照を手放す。
	Fx->SetPath( FString() );
	C.Check( !Fx->Play() && Fx->IsFinished() && !Fx->GetPlayback().IsValid() && G.Standalone->GetData<FEffectPlayer>() != nullptr && !G.Standalone->GetData<FEffectPlayer>()->Playback.IsValid(), "空のパスでの Play は失敗し、古いエフェクトの再生と描画の参照も残さない" );
	Fx->SetPath( "fx/fire.effect.json" );

	// 位置に追従する（1 つのオブジェクトに付けるエフェクトの部品は 1 つ。いくつも出したいときは、子のオブジェクトに付ける）
	G.Follower = Scene.SpawnObject<CGameObject>();
	G.Follower->AddComponent<CTransformComponent>()->SetLocalTransform( FTransform( FVector3( 4.0f, 5.0f, 6.0f ) ) );
	CEffectComponent* Follow = G.Follower->AddComponent<CEffectComponent>( "fx/mixed.effect.json" );
	Follow->Tick( 0.125f );
	if ( const TSharedPtr<IEffectPlayback> P = Follow->GetPlayback(); P.IsValid() )
	{
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		P->Record( Draw );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		bool bAtObject = Log.Count( EDrawCommand::Quad3D ) == 5;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Quad3D )
			{
				const FDrawQuad3DData D = Command.Read<FDrawQuad3DData>();
				bAtObject = bAtObject && Near3( FVector3( D.Pos[0], D.Pos[1], D.Pos[2] ), FVector3( 4.0f, 5.0f, 6.0f ) );
			}
		} );
		C.Check( bAtObject, "particles are born at the object's world position (4, 5, 6)" );
	}
	else C.Check( false, "the mixed effect plays" );

	// 動くオブジェクトに付いて、そのときの位置に出る（1 回ごとに 1 つ出る流れを、動かしながら 3 回）
	G.Streamer = Scene.SpawnObject<CGameObject>();
	CTransformComponent* StreamTransform = G.Streamer->AddComponent<CTransformComponent>();
	CEffectComponent* Flow = G.Streamer->AddComponent<CEffectComponent>( "fx/stream.effect.json" );
	StreamTransform->SetLocalTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) );
	Flow->Tick( 0.125f );
	StreamTransform->SetLocalTransform( FTransform( FVector3( 5.0f, 0.0f, 0.0f ) ) );
	Flow->Tick( 0.125f );
	StreamTransform->SetLocalTransform( FTransform( FVector3( 9.0f, 0.0f, 0.0f ) ) );
	Flow->Tick( 0.125f );
	// 3 個の位置を明示更新で作ったあと発生を止め、長い更新でも個数が変わらないことを確認する。
	Flow->Stop( false );
	Flow->Tick( 0.25f );
	if ( const TSharedPtr<IEffectPlayback> P = Flow->GetPlayback(); P.IsValid() )
	{
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		P->Record( Draw );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		TArray<float32> Xs;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Quad3D ) Xs.Add( Command.Read<FDrawQuad3DData>().Pos[0] );
		} );
		C.Check( Xs.Num() == 3 && NMath::Abs( Xs[0] - 1.0f ) < 1.0e-4f && NMath::Abs( Xs[1] - 5.0f ) < 1.0e-4f && NMath::Abs( Xs[2] - 9.0f ) < 1.0e-4f, "each tick's particle is born where the object is at that tick (x = 1, 5, 9; %d particles)", Xs.Num() );
	}
	else C.Check( false, "the stream plays" );

	// 使い切り: 終わったら、オブジェクトごと消える
	G.Popper = Scene.SpawnObject<CGameObject>();
	G.Popper->AddComponent<CTransformComponent>();
	CEffectComponent* Pop1 = G.Popper->AddComponent<CEffectComponent>( "fx/pop.effect.json" );
	Pop1->SetDestroyOwnerWhenFinished( true );
	Pop1->Tick( 0.125f );
	const bool bBorn = Pop1->GetPlayback().IsValid() && Pop1->GetPlayback()->GetNumParticles() == 1;
	Pop1->Tick( 0.125f );
	const bool bStillThere = Pop1->IsPlaying() && !G.Popper->IsPendingDestroy();
	Pop1->Tick( 0.125f );
	C.Check( bBorn && bStillThere && Pop1->IsFinished() && !Pop1->GetPlayback().IsValid() && G.Popper->IsPendingDestroy(), "a one-shot effect with 'destroy when finished' destroys its object when the last particle is gone (and not before)" );
	G.Kept = Scene.SpawnObject<CGameObject>();
	G.Kept->AddComponent<CTransformComponent>();
	CEffectComponent* Keep = G.Kept->AddComponent<CEffectComponent>( "fx/pop.effect.json" );
	Keep->Tick( 0.125f );
	Keep->Tick( 0.125f );
	Keep->Tick( 0.125f );
	C.Check( Keep->IsFinished() && !G.Kept->IsPendingDestroy() && G.Kept->GetData<FEffectPlayer>() != nullptr && !G.Kept->GetData<FEffectPlayer>()->Playback.IsValid(), "without that setting, the object stays when the effect ends, with nothing left to draw" );

	// 部品を取り外すと、描画のデータは空になる（次のフレームのあとで確認）
	G.Detached = Scene.SpawnObject<CGameObject>();
	G.Detached->AddComponent<CTransformComponent>();
	CEffectComponent* Gone = G.Detached->AddComponent<CEffectComponent>( "fx/fire.effect.json" );
	Gone->Tick( 0.125f );
	C.Check( Gone->GetPlayback().IsValid() && G.Detached->GetData<FEffectPlayer>() != nullptr && G.Detached->GetData<FEffectPlayer>()->Playback == Gone->GetPlayback(), "(a component to be removed is playing)" );
	Gone->Destroy();

	//-------------------------------------------------------------------------
	// ワールドの Effect 部品
	//-------------------------------------------------------------------------
	C.Group = "effect-world";
	FWorldDefinition World;
	World.Name = "Effects";
	FWorldObject FireObject = MakeEffectObject( 1, "Fire", "fx/fire.effect.json", FVector3( 0.0f, 0.0f, 0.0f ) );
	FireObject.Effect.GetValue().Scale = 2.0f;
	FireObject.Effect.GetValue().Seed = 9;
	FireObject.Effect.GetValue().Loop = 1;
	FWorldObject Negative = MakeEffectObject( 6, "Negative", "fx/fire.effect.json", FVector3( 30.0f, 0.0f, 0.0f ) );
	Negative.Effect.GetValue().Seed = -5;
	Negative.Effect.GetValue().bAutoPlay = false;
	FWorldObject Manual = MakeEffectObject( 3, "Manual", "fx/fire.effect.json", FVector3( 20.0f, 0.0f, 0.0f ) );
	Manual.Effect.GetValue().bAutoPlay = false;
	Manual.Effect.GetValue().Speed = 0.5f;
	Manual.Effect.GetValue().Loop = 0;
	for ( const FWorldObject& Object : { FireObject, MakeEffectObject( 2, "Mixed", "fx/mixed.effect.json", FVector3( 10.0f, 0.0f, 0.0f ) ), Manual, MakeEffectObject( 4, "Foreign", "fx/boom.efk", FVector3::Zero ), MakeEffectObject( 5, "NoFile", "", FVector3::Zero ), Negative } ) World.Objects.Add( Object );
	World.NextId = 7;
	FWorldBindings Bindings;
	FWorldSpawnOptions Options;
	Options.Registry = &Registry;
	G.Level = NWorld::Spawn( Scene, MakeShared<FWorldDefinition>( World ), Bindings, Options );
	const FWorldSpawnReport& Report = G.Level.GetReport();
	C.Check( Report.NumSpawned == 6 && Report.Warnings.Num() == 2, "six objects are placed, with two warnings: a kind of file no backend handles, and an effect without a file (%d placed, %d warnings)", Report.NumSpawned, Report.Warnings.Num() );
	bool bForeign = false, bNoFile = false;
	for ( const FString& W : Report.Warnings )
	{
		bForeign = bForeign || ( W.Contains( "fx/boom.efk" ) && W.Contains( "backend" ) );
		bNoFile = bNoFile || W.Contains( "no file" );
	}
	C.Check( bForeign && bNoFile, "and each says which: the foreign file named, and 'no file'" );
	const TSharedPtr<CGameObject> FireHost = G.Level.FindByName( "Fire" );
	const CEffectComponent* FireComponent = FireHost.IsValid() ? FireHost->GetComponent<CEffectComponent>() : nullptr;
	C.Check( FireComponent != nullptr && FireComponent->GetPath() == "fx/fire.effect.json" && FireComponent->GetParams().Scale == 2.0f && FireComponent->GetParams().Seed == 9u && FireComponent->GetParams().Loop == 1 && FireComponent->GetParams().Speed == 1.0f,
			 "the Effect component's file, scale, seed and loop reach the component" );
	const TSharedPtr<CGameObject> ManualHost = G.Level.FindByName( "Manual" );
	const CEffectComponent* ManualComponent = ManualHost.IsValid() ? ManualHost->GetComponent<CEffectComponent>() : nullptr;
	C.Check( ManualComponent != nullptr && ManualComponent->GetParams().Speed == 0.5f && ManualComponent->GetParams().Loop == 0 && ManualComponent->GetParams().Seed == 0u && ManualComponent->GetParams().Scale == 1.0f, "the speed and 'no loop' reach the component too (the seed 0 stays 0, the scale 1)" );
	const TSharedPtr<CGameObject> NegativeHost = G.Level.FindByName( "Negative" );
	const CEffectComponent* NegativeComponent = NegativeHost.IsValid() ? NegativeHost->GetComponent<CEffectComponent>() : nullptr;
	C.Check( NegativeComponent != nullptr && NegativeComponent->GetParams().Seed == 0u, "a negative seed (which validation refuses) becomes 0, not a huge number" );
	const TSharedPtr<CGameObject> ForeignHost = G.Level.FindByName( "Foreign" );
	C.Check( ForeignHost.IsValid() && ForeignHost->GetComponent<CEffectComponent>() == nullptr, "no component is made for a file no backend handles" );
}

void NWorldTest::RunEffectSceneCheck( FChecker& C, CScene& Scene, CAssetManager& Assets, const FCommandLog& Log )
{
	(void)Scene;
	FEffectSceneState& G = GState;
	C.Group = "effect-draw";
	const TSharedPtr<CGameObject> FireHost = G.Level.FindByName( "Fire" );
	const TSharedPtr<CGameObject> MixedHost = G.Level.FindByName( "Mixed" );
	const TSharedPtr<CGameObject> ManualHost = G.Level.FindByName( "Manual" );
	const CEffectComponent* FireComponent = FireHost.IsValid() ? FireHost->GetComponent<CEffectComponent>() : nullptr;
	const CEffectComponent* MixedComponent = MixedHost.IsValid() ? MixedHost->GetComponent<CEffectComponent>() : nullptr;
	CEffectComponent* ManualComponent = ManualHost.IsValid() ? ManualHost->GetComponent<CEffectComponent>() : nullptr;

	C.Check( FireComponent != nullptr && FireComponent->GetPlayback().IsValid() && FireComponent->GetPlayback()->GetNumParticles() == 5 && MixedComponent != nullptr && MixedComponent->GetPlayback().IsValid() && MixedComponent->GetPlayback()->GetNumParticles() == 5,
			 "objects placed with auto play are playing a few frames later: 5 + 5 particles" );
	C.Check( ManualComponent != nullptr && !ManualComponent->GetPlayback().IsValid() && ManualHost->GetData<FEffectPlayer>() == nullptr, "and the one with auto play off has not started, and has nothing to draw" );

	// 描画システムが記録したコマンド（このフレームの分）
	int32 NumQuads = 0, NumAdd = 0, NumAlpha = 0, NumFire = 0, NumSmoke = 0, NumSpark = 0, NumFollower = 0;
	bool bFireOk = true, bSmokeOk = true, bSparkOk = true;
	const int32 TextureHandle = G.Texture.IsValid() ? G.Texture->GetHandle() : -2;
	Log.ForEach( [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type == EDrawCommand::Blend )
		{
			const int32 Mode = Command.Read<FDrawBlendData>().Mode;
			NumAdd += Mode == static_cast<int32>( EDrawBlend::Add ) ? 1 : 0;
			NumAlpha += Mode == static_cast<int32>( EDrawBlend::Alpha ) ? 1 : 0;
			return;
		}
		if ( Command.Type != EDrawCommand::Quad3D ) return;
		++NumQuads;
		const FDrawQuad3DData D = Command.Read<FDrawQuad3DData>();
		const FVector3 Position( D.Pos[0], D.Pos[1], D.Pos[2] );
		if ( Near3( Position, FVector3( 0.0f, 0.0f, 0.0f ) ) )
		{
			++NumFire;
			bFireOk = bFireOk && D.Width == 2.0f && D.Color.R == 157 && D.Color.G == 78 && D.Color.B == 39 && D.Color.A == 255;
		}
		else if ( Near3( Position, FVector3( 4.0f, 5.0f, 6.0f ) ) ) ++NumFollower;   // 部品を直接付けたオブジェクト（Start で作った）
		else if ( Near3( Position, FVector3( 10.0f, 0.0f, 0.0f ) ) )
		{
			if ( D.Handle == TextureHandle )
			{
				++NumSmoke;
				bSmokeOk = bSmokeOk && D.Width == 1.0f && D.Color.R == 255 && D.Color.G == 255 && D.Color.B == 255 && D.Color.A == 200;
			}
			else
			{
				++NumSpark;
				bSparkOk = bSparkOk && D.Width == 1.0f && D.Color.R == 255 && D.Color.A == 255 && D.Handle >= 0 && D.Handle != TextureHandle;
			}
		}
	} );
	C.Check( NumQuads == 18 && NumFire == 5 && NumSmoke == 3 && NumSpark == 2 && NumFollower == 5,
			 "the built-in draw system records a Quad3D for every particle of every playing effect: fire 5, smoke 3, sparks 2, the 5 of the object with a component of its own, and the 3 of the moving one (%d in all)", NumQuads );
	C.Check( NumAdd == 4 && NumAlpha == 2, "with one Blend per emitter: four additive (fire, sparks, the other object's sparks, the stream) and two alpha (the two smokes) (%d, %d)", NumAdd, NumAlpha );
	C.Check( bFireOk && bSmokeOk && bSparkOk, "each at its object's world position, with the colours and sizes of its definition (the fire at double size by its Scale)" );

	// 手動で始める
	if ( ManualComponent != nullptr )
	{
		C.Check( ManualComponent->Play() && ManualComponent->GetPlayback().IsValid() && ManualHost->GetData<FEffectPlayer>() != nullptr, "an effect placed without auto play starts when Play is called" );
		ManualComponent->Tick( 0.125f );
		C.Check( ManualComponent->GetPlayback()->GetNumParticles() == 5, "and makes its particles on the next tick (a play speed of 0.5 does not change a burst)" );
	}

	// 取り外した部品
	C.Group = "effect-component";
	C.Check( G.Detached.IsValid() && G.Detached->GetComponent<CEffectComponent>() == nullptr && G.Detached->GetData<FEffectPlayer>() != nullptr && !G.Detached->GetData<FEffectPlayer>()->Playback.IsValid(),
			 "after a component is destroyed, the object's drawing data is left empty (nothing keeps drawing)" );
	C.Check( G.Standalone.IsValid() && G.Standalone->GetComponent<CEffectComponent>() != nullptr && G.Standalone->GetData<FEffectPlayer>() != nullptr, "(the other objects keep their component and data)" );

	// 場所を動かすと、粒子の出る場所も動く（世界の空間の粒子は、動いたあとに出るものから）
	if ( FireHost.IsValid() && FireHost->GetComponent<CTransformComponent>() != nullptr )
	{
		FireHost->GetComponent<CTransformComponent>()->SetLocalTransform( FTransform( FVector3( 50.0f, 0.0f, 0.0f ) ) );
		G.FirePosition = FVector3( 50.0f, 0.0f, 0.0f );
	}
	(void)Assets;
}

void NWorldTest::RunEffectSceneFinish( FChecker& C, CScene& Scene, CAssetManager& Assets )
{
	(void)Scene;
	FEffectSceneState& G = GState;
	C.Group = "effect-cleanup";
	C.Check( G.Kept.IsValid() && G.Kept->IsAlive() && !G.Kept->IsPendingDestroy() && G.Kept->GetComponent<CEffectComponent>() != nullptr, "the object that finished without 'destroy when finished' is still there" );
	C.Check( G.Popper.IsValid() && !G.Popper->IsAlive(), "and the one with it is gone from the scene" );
	G.Level.Despawn();
	for ( TSharedPtr<CGameObject>* Object : { &G.Standalone, &G.Kept, &G.Detached, &G.Follower, &G.Popper, &G.Streamer } )
	{
		if ( Object->IsValid() && ( *Object )->IsAlive() ) ( *Object )->Destroy();
		Object->Reset();
	}
	G.Texture.Reset();
	G = FEffectSceneState();
	(void)Assets;
}

//=============================================================================
// 本物の DxLib: 粒子のピクセル
//   画面 128 x 128、正射影のカメラ（縦 8 ワールド単位 = 1 単位 16 ピクセル）。大きさ 4 の粒子は 64 ピクセル。
//   組み込みの丸の画像（加算用は色もふちで暗くなる）と、アルファ・加算の重なり方、寿命の間の大きさと色、回転、位置を、実際の絵で確かめる。
//=============================================================================
void NWorldTest::RunEffectRealTests( FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "effect-pixels";
	constexpr int Size = 128;
	Assets.SetRootDirectory( Dir );
	const int Screen = MakeScreen( Size, Size, FALSE );
	const int Soft = MakeARGB8ColorSoftImage( Size, Size );
	if ( Screen < 0 || Soft < 0 )
	{
		C.Skip( "no capture surface" );
		if ( Screen >= 0 ) DeleteGraph( Screen );
		if ( Soft >= 0 ) DeleteSoftImage( Soft );
		return;
	}
	const float32 OldNear = GetCameraNear();
	const float32 OldFar = GetCameraFar();
	int OldR = 0, OldG = 0, OldB = 0;
	GetBackgroundColor( &OldR, &OldG, &OldB );
	SetDrawScreen( Screen );

	struct FRgb
	{
		int R = 0, G = 0, B = 0;
	};
	const auto Pixel = [&]( int X, int Y )
	{
		FRgb Result;
		int A = 0;
		GetPixelSoftImage( Soft, X, Y, &Result.R, &Result.G, &Result.B, &A );
		return Result;
	};
	const auto Near = [&]( const FRgb& A, const FRgb& B, int Tolerance = 8 ) { return NMath::Abs( A.R - B.R ) <= Tolerance && NMath::Abs( A.G - B.G ) <= Tolerance && NMath::Abs( A.B - B.B ) <= Tolerance; };
	// 背景と違う（光っている）ピクセルの外接矩形
	const auto Bounds = [&]( int Background, int& OutLeft, int& OutTop, int& OutRight, int& OutBottom )
	{
		OutLeft = Size;
		OutTop = Size;
		OutRight = -1;
		OutBottom = -1;
		for ( int Y = 0; Y < Size; ++Y )
		{
			for ( int X = 0; X < Size; ++X )
			{
				const FRgb P = Pixel( X, Y );
				if ( NMath::Abs( P.R - Background ) + NMath::Abs( P.G - Background ) + NMath::Abs( P.B - Background ) <= 6 ) continue;
				OutLeft = NMath::Min( OutLeft, X );
				OutTop = NMath::Min( OutTop, Y );
				OutRight = NMath::Max( OutRight, X );
				OutBottom = NMath::Max( OutBottom, Y );
			}
		}
		return OutRight >= 0;
	};

	// 定義を保存して、バックエンドで再生して、1 枚描く（Step1 で粒子が生まれ、Step2 で動く）
	const auto Render = [&]( const FParticleEffectDefinition& Definition, const char* Name, int Background, float32 Step2 = 0.0f, const FTransform& Where = FTransform::Identity )
	{
		if ( !Assets.SaveEffect( Name, Definition ) ) return false;
		const TSharedPtr<IEffectPlayback> Playback = FEffectBackendRegistry::Global().Play( Name, &Assets, FEffectPlayParams(), Where );
		if ( !Playback.IsValid() ) return false;
		Playback->Update( 0.125f );
		if ( Step2 > 0.0f ) Playback->Update( Step2 );
		SetBackgroundColor( Background, Background, Background );
		ClearDrawScreen();
		SetCameraNearFar( 0.1f, 100.0f );
		SetupCamera_Ortho( 8.0f );
		SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		SetUseLighting( FALSE );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Playback->Record( Draw );
		Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		GetDrawScreenSoftImage( 0, 0, Size, Size, Soft );
		return true;
	};
	// 1 つの粒子を出すエミッター（大きさ 4 = 64 ピクセル、一定の色）
	const auto One = [&]( EParticleBlend Blend, const FColor32& Color, float32 SizeValue = 4.0f )
	{
		FParticleEmitterDefinition E = QuietEmitter();
		E.Bursts.Add( Burst( 0.0f, 1 ) );
		E.Blend = Blend;
		E.StartSize = { SizeValue, SizeValue };
		E.EndSize = E.StartSize;
		E.StartColor = Color;
		E.EndColor = Color;
		return E;
	};
	const auto Effect = [&]( const FParticleEmitterDefinition& E ) { FParticleEffectDefinition D; D.Emitters.Add( E ); return D; };

	//-------------------------------------------------------------------------
	// 加算の丸: 中心が明るく、ふちに向かって暗くなり、外は何も足さない
	//-------------------------------------------------------------------------
	bool bOk = Render( Effect( One( EParticleBlend::Add, FColor32( 255, 255, 255, 255 ) ) ), "px_add.effect.json", 0 );
	C.Check( bOk, "an additive soft dot is drawn" );
	const FRgb Centre = Pixel( 64, 64 );
	const FRgb Ring = Pixel( 80, 64 );
	const FRgb Rim = Pixel( 64 + 29, 64 );
	int L, T, R, B;
	const bool bBounds = Bounds( 0, L, T, R, B );
	C.Check( Centre.R >= 215 && NMath::Abs( Centre.R - Centre.G ) <= 3 && NMath::Abs( Centre.G - Centre.B ) <= 3, "on black, the middle of a white additive dot is bright and grey (%d, %d, %d)", Centre.R, Centre.G, Centre.B );
	C.Check( Ring.R >= 44 && Ring.R <= 90, "half way to the edge it is about a quarter as bright (%d)", Ring.R );
	C.Check( Rim.R <= 16 && Pixel( 64 + 40, 64 ).R == 0 && Pixel( 64, 64 - 40 ).R == 0, "near the edge it is dark (%d), and outside the quad nothing is added", Rim.R );
	C.Check( Near( Pixel( 48, 64 ), Ring, 10 ) && Near( Pixel( 64, 48 ), Ring, 10 ) && Near( Pixel( 64, 80 ), Ring, 10 ), "it is round: the four points at the same distance are about equally bright" );
	C.Check( bBounds && R - L + 1 >= 40 && R - L + 1 <= 66 && NMath::Abs( ( L + R ) / 2 - 64 ) <= 2 && NMath::Abs( ( T + B ) / 2 - 64 ) <= 2, "and it is centred on the particle's position (%d..%d, %d..%d)", L, R, T, B );

	// 色
	Render( Effect( One( EParticleBlend::Add, FColor32( 255, 0, 0, 255 ) ) ), "px_red.effect.json", 0 );
	const FRgb RedCentre = Pixel( 64, 64 );
	C.Check( RedCentre.R >= 215 && RedCentre.G <= 4 && RedCentre.B <= 4, "a red additive dot adds only red (%d, %d, %d)", RedCentre.R, RedCentre.G, RedCentre.B );
	// 加算のアルファは、暗さになる
	Render( Effect( One( EParticleBlend::Add, FColor32( 255, 255, 255, 128 ) ) ), "px_half.effect.json", 0 );
	const FRgb HalfCentre = Pixel( 64, 64 );
	C.Check( HalfCentre.R >= 100 && HalfCentre.R <= 136 && NMath::Abs( HalfCentre.R * 2 - Centre.R ) <= 24, "an additive dot with alpha 128 is about half as bright (%d against %d)", HalfCentre.R, Centre.R );
	// 重なると足される
	FParticleEmitterDefinition Pair = One( EParticleBlend::Add, FColor32( 255, 255, 255, 64 ) );
	Render( Effect( Pair ), "px_single.effect.json", 0 );
	const int Single = Pixel( 64, 64 ).R;
	Pair.Bursts[0].Count = 2;
	Render( Effect( Pair ), "px_double.effect.json", 0 );
	const int Double = Pixel( 64, 64 ).R;
	C.Check( Single >= 40 && Single <= 80 && NMath::Abs( Double - 2 * Single ) <= 12, "two particles on the same spot add up (%d, then %d)", Single, Double );

	//-------------------------------------------------------------------------
	// アルファ: 背景に重なる
	//-------------------------------------------------------------------------
	Render( Effect( One( EParticleBlend::Alpha, FColor32( 255, 255, 255, 255 ) ) ), "px_alpha.effect.json", 100 );
	const FRgb AlphaCentre = Pixel( 64, 64 );
	const FRgb AlphaRing = Pixel( 80, 64 );
	C.Check( AlphaCentre.R >= 230 && AlphaRing.R >= 120 && AlphaRing.R <= 160 && Pixel( 64 + 40, 64 ).R >= 98 && Pixel( 64 + 40, 64 ).R <= 102, "on grey (100), an opaque white soft dot covers the middle (%d), lets the background through at the edge (%d at half way), and leaves the outside alone (%d)", AlphaCentre.R, AlphaRing.R, Pixel( 64 + 40, 64 ).R );
	Render( Effect( One( EParticleBlend::Alpha, FColor32( 255, 255, 255, 128 ) ) ), "px_alpha_half.effect.json", 100 );
	const FRgb AlphaHalf = Pixel( 64, 64 );
	C.Check( AlphaHalf.R >= 160 && AlphaHalf.R <= 190, "with alpha 128 it is half way between the background and the dot (%d)", AlphaHalf.R );

	//-------------------------------------------------------------------------
	// 寿命の間の大きさと色（画像: 4 象限の四角）
	//-------------------------------------------------------------------------
	FParticleEmitterDefinition Fade = One( EParticleBlend::Alpha, FColor32( 255, 255, 255, 255 ) );
	Fade.Texture = "quad.bmp";
	Fade.Lifetime = { 2.0f, 2.0f };
	Fade.EndSize = { 0.0f, 0.0f };
	Fade.EndColor = FColor32( 255, 255, 255, 0 );
	Render( Effect( Fade ), "px_fade0.effect.json", 0 );
	const bool bNew = Bounds( 0, L, T, R, B ) && NMath::Abs( ( R - L + 1 ) - 64 ) <= 2 && NMath::Abs( ( B - T + 1 ) - 64 ) <= 2;
	const FRgb NewTopLeft = Pixel( 48, 48 );
	Render( Effect( Fade ), "px_fade1.effect.json", 0, 1.0f );
	const bool bHalf = Bounds( 0, L, T, R, B ) && NMath::Abs( ( R - L + 1 ) - 32 ) <= 2 && NMath::Abs( ( B - T + 1 ) - 32 ) <= 2 && NMath::Abs( ( L + R ) / 2 - 64 ) <= 2;
	const FRgb HalfTopLeft = Pixel( 56, 56 );
	C.Check( bNew && bHalf, "the size shrinks from 4 to 0 over the life: 64 pixels when new, 32 half way" );
	C.Check( Near( NewTopLeft, FRgb { 255, 0, 0 }, 6 ) && Near( HalfTopLeft, FRgb { 128, 0, 0 }, 10 ), "and the alpha fades 255 -> 0: the red corner is full (%d) when new and half (%d) half way", NewTopLeft.R, HalfTopLeft.R );

	//-------------------------------------------------------------------------
	// 回転（カメラの方を向く粒子の回転 = 向き固定の粒子の回転 = エフェクトを同じだけ回した向き固定の粒子）
	//-------------------------------------------------------------------------
	FParticleEmitterDefinition Spin = One( EParticleBlend::Alpha, FColor32( 255, 255, 255, 255 ), 2.0f );
	Spin.Texture = "quad.bmp";
	const auto Quadrants = [&]( FRgb Out[4] )
	{
		int Left, Top, Right, Bottom;
		if ( !Bounds( 0, Left, Top, Right, Bottom ) ) return false;
		const int W = Right - Left + 1, H = Bottom - Top + 1;
		const int Xs[2] = { Left + W / 4, Left + ( W * 3 ) / 4 };
		const int Ys[2] = { Top + H / 4, Top + ( H * 3 ) / 4 };
		for ( int i = 0; i < 4; ++i ) Out[i] = Pixel( Xs[i % 2], Ys[i / 2] );
		return true;
	};
	FRgb Plain[4] = {}, ByCamera[4] = {}, ByFixed[4] = {}, ByEffect[4] = {};
	Spin.Orient = EParticleOrient::Camera;
	Render( Effect( Spin ), "px_spin0.effect.json", 0 );
	const bool bPlain = Quadrants( Plain );
	Spin.StartAngle = { 90.0f, 90.0f };
	Render( Effect( Spin ), "px_spin_camera.effect.json", 0 );
	const bool bCamera = Quadrants( ByCamera );
	Spin.Orient = EParticleOrient::Fixed;
	Render( Effect( Spin ), "px_spin_fixed.effect.json", 0 );
	const bool bFixed = Quadrants( ByFixed );
	Spin.StartAngle = { 0.0f, 0.0f };
	Render( Effect( Spin ), "px_spin_effect.effect.json", 0, 0.0f, FTransform( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707964f ) ) );
	const bool bEffect = Quadrants( ByEffect );
	const FRgb Red { 255, 0, 0 }, Green { 0, 255, 0 }, Blue { 0, 0, 255 }, Yellow { 255, 255, 0 };
	C.Check( bPlain && Near( Plain[0], Red ) && Near( Plain[1], Green ) && Near( Plain[2], Blue ) && Near( Plain[3], Yellow ), "an unturned particle shows the image as it is: red green / blue yellow" );
	bool bSame = bCamera && bFixed && bEffect;
	for ( int i = 0; bSame && i < 4; ++i ) bSame = Near( ByCamera[i], ByFixed[i] ) && Near( ByFixed[i], ByEffect[i] );
	C.Check( bSame && !Near( ByCamera[0], Red ), "a start angle of 90 degrees turns it (the corner is no longer red), the same way for a camera-facing particle, a fixed one, and a fixed one on an effect turned by 90 degrees" );
	int SpinLeft, SpinTop, SpinRight, SpinBottom;
	Render( Effect( Spin ), "px_spin_size.effect.json", 0, 0.0f, FTransform( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707964f ) ) );
	C.Check( Bounds( 0, SpinLeft, SpinTop, SpinRight, SpinBottom ) && NMath::Abs( ( SpinRight - SpinLeft + 1 ) - 32 ) <= 2 && NMath::Abs( ( SpinBottom - SpinTop + 1 ) - 32 ) <= 2, "and it is still 32 pixels square" );

	//-------------------------------------------------------------------------
	// 位置: 動いた先に出る（x = 2 -> 画面の中央から右へ 32 ピクセル）
	//-------------------------------------------------------------------------
	FParticleEmitterDefinition Move = One( EParticleBlend::Alpha, FColor32( 255, 255, 255, 255 ), 2.0f );
	Move.Texture = "quad.bmp";
	Move.Speed = { 2.0f, 2.0f };
	Move.Direction = FVector3( 1.0f, 0.0f, 0.0f );
	Render( Effect( Move ), "px_move.effect.json", 0, 1.0f );
	C.Check( Bounds( 0, L, T, R, B ) && NMath::Abs( ( L + R ) / 2 - 96 ) <= 2 && NMath::Abs( ( T + B ) / 2 - 64 ) <= 2, "a particle that has flown 2 units to the right is 32 pixels right of the middle (%d..%d, %d..%d)", L, R, T, B );
	Render( Effect( Move ), "px_where.effect.json", 0, 1.0f, FTransform( FVector3( 0.0f, 1.0f, 0.0f ) ) );
	C.Check( Bounds( 0, L, T, R, B ) && NMath::Abs( ( L + R ) / 2 - 96 ) <= 2 && NMath::Abs( ( T + B ) / 2 - 48 ) <= 2, "and when the effect itself is 1 unit up, 16 pixels higher (%d..%d, %d..%d)", L, R, T, B );

	SetDrawScreen( DX_SCREEN_BACK );
	SetBackgroundColor( OldR, OldG, OldB );
	SetCameraNearFar( OldNear, OldFar );
	DeleteGraph( Screen );
	DeleteSoftImage( Soft );
}
