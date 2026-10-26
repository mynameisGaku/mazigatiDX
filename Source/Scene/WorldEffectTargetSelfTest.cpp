// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectTargetSelfTest.h"

#include <cmath>   // std::isfinite: フレームワークに同等の関数がまだ無い
#include <limits>  // std::numeric_limits<float32>::quiet_NaN: フレームワークに NaN の定数がまだ無い

#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Framework/World/WorldValidate.h"
#include "Library/Math/MathUtils.h"
#include "WorldEffectSelfTestSupport.h"

using namespace NEffectTest;
using namespace NWorldMeshTest;

namespace
{
	// 動的終点の指定を持つ Effect 部品だけのオブジェクトを作る。
	FWorldObject MakeTargetObject( int32 Id, const char* Name, int32 Target, const FVector3& Offset = FVector3::Zero )
	{
		// 作るオブジェクト。
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		// 部品の設定。
		FWorldEffect Effect;
		Effect.Path = "Effects/bolt.effect.json";
		Effect.Target = Target;
		Effect.TargetOffset = Offset;
		Object.Effect = Effect;
		return Object;
	}

	// 部品を持たない、位置だけのオブジェクトを作る。
	FWorldObject MakePlainObject( int32 Id, const char* Name, const FVector3& Position )
	{
		// 作るオブジェクト。
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Transform.Position = Position;
		return Object;
	}

	// 並べたオブジェクトから、次の Id を合わせたワールドを作る。
	FWorldDefinition MakeTargetWorld( std::initializer_list<FWorldObject> Objects )
	{
		// 作るワールド。
		FWorldDefinition World;
		World.Name = "Target";
		for ( const FWorldObject& Object : Objects )
		{
			World.Objects.Add( Object );
			World.NextId = NMath::Max( World.NextId, Object.Id + 1 );
		}
		return World;
	}

	// 検証結果のうち、指定の種類・重大度・文言に一致するものを数える。
	int32 CountWorldIssues( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, EWorldIssueSeverity Severity )
	{
		// 一致した個数。
		int32 Count = 0;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.Code == Code && Issue.Severity == Severity ) ++Count;
		}
		return Count;
	}

	// 終点の指定が JSON に出ない既定値と、書いた値が往復することを確かめる。
	void TestTargetJson( NAssetTest::FChecker& C )
	{
		C.Group = "effect-target-world-json";
		// 終点を指定しない定義の JSON。
		const NlohmannJson PlainJson = MakeTargetWorld( { MakeTargetObject( 1, "Plain", WorldNoId ) } ).ToJson();
		// 読み取り専用の窓口。
		const FJson PlainView( PlainJson );
		C.Check( PlainView["Objects"][0]["Components"]["Effect"].size() == 1 && !PlainView["Objects"][0]["Components"]["Effect"].contains( "Target" ) && !PlainView["Objects"][0]["Components"]["Effect"].contains( "TargetOffset" ), "既定の終点指定は Target / TargetOffset を書かず、既存の .world と同じ形のままにする" );

		// 終点を指定した定義と、その JSON。
		const FWorldDefinition Targeted = MakeTargetWorld( { MakeTargetObject( 1, "Caster", 2, FVector3( 0.0f, 1.5f, 0.25f ) ), MakePlainObject( 2, "Target", FVector3( 5.0f, 0.0f, 0.0f ) ) } );
		const NlohmannJson TargetedJson = Targeted.ToJson();
		// 読み取り専用の窓口。
		const FJson TargetedView( TargetedJson );
		C.Check( TargetedView["Objects"][0]["Components"]["Effect"]["Target"] == 2 && TargetedView["Objects"][0]["Components"]["Effect"]["TargetOffset"].size() == 3 && TargetedView["Objects"][0]["Components"]["Effect"]["TargetOffset"][1] == 1.5, "指定した Target と TargetOffset を Components.Effect へ書く" );
		// 読み戻した定義と、その警告。
		FWorldDefinition Back;
		TArray<FString> Warnings;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( TargetedJson.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty() && Back.Objects.Num() == 2 && Back.Objects[0].Effect.IsSet() && Back.Objects[0].Effect.GetValue().Target == 2 && Near3( Back.Objects[0].Effect.GetValue().TargetOffset, FVector3( 0.0f, 1.5f, 0.25f ) ) && Back.ToJson() == TargetedJson, "書いた終点指定は警告なしで読み戻せ、もう一度書いても同じになる" );

		// 型違い・範囲外の値を含む JSON から読んだ定義。
		FWorldDefinition Odd;
		TArray<FString> OddWarnings;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Effect\":{\"Path\":\"a.effect.json\",\"Target\":\"two\",\"TargetOffset\":[1,2]}}},{\"Id\":2,\"Components\":{\"Effect\":{\"Path\":\"a.effect.json\",\"Target\":99999999999,\"TargetOffset\":[0,0,\"x\"]}}},{\"Id\":3,\"Components\":{\"Effect\":{\"Path\":\"a.effect.json\",\"Target\":2.5}}}]}" ), Odd, &OddWarnings ) && Odd.Objects.Num() == 3, "終点指定の型が壊れた .world も読める" );
		if ( Odd.Objects.Num() != 3 ) return;
		// 型違いの二つの値が、使えない印のまま残っているか。
		bool bInvalidKept = true;
		for ( const FWorldObject& Object : Odd.Objects )
		{
			bInvalidKept = bInvalidKept && Object.Effect.IsSet() && Object.Effect.GetValue().Target == -1;
		}
		C.Check( bInvalidKept && !std::isfinite( Odd.Objects[0].Effect.GetValue().TargetOffset.x ) && !std::isfinite( Odd.Objects[1].Effect.GetValue().TargetOffset.x ), "文字列・範囲外・小数の Target は -1、型違いの TargetOffset は有限でない値として残り、勝手に既定値へ直さない" );
		// 壊れた値をもう一度書いて読んだ定義。
		FWorldDefinition OddBack;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( Odd.ToJson().dump().c_str() ), OddBack ) && OddBack.Objects.Num() == 3 && OddBack.Objects[0].Effect.GetValue().Target == -1 && !std::isfinite( OddBack.Objects[0].Effect.GetValue().TargetOffset.x ) && OddBack.ToJson().dump() == Odd.ToJson().dump(), "壊れた値も書き戻しと読み直しで、書き出した内容が変わらない（NaN は JSON 値として等しくならないため、文字列で比べる）" );
	}

	// 自己参照・存在しない対象・有限でないずれを、エラーとして報告する。
	void TestTargetValidation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-target-world-validate";
		// 正しい指定を含む定義。
		const FWorldDefinition Good = MakeTargetWorld( { MakeTargetObject( 1, "Caster", 2, FVector3( 0.0f, 1.0f, 0.0f ) ), MakePlainObject( 2, "Target", FVector3::Zero ) } );
		C.Check( NWorld::ValidateWorld( Good, nullptr ).IsEmpty(), "存在する別のオブジェクトを指す終点指定は問題なし" );
		// 自分自身を指す定義。
		const FWorldDefinition Self = MakeTargetWorld( { MakeTargetObject( 1, "Self", 1 ) } );
		// 存在しない Id を指す定義。
		const FWorldDefinition Missing = MakeTargetWorld( { MakeTargetObject( 1, "Missing", 9 ) } );
		// 使えない印の Target を持つ定義。
		const FWorldDefinition Invalid = MakeTargetWorld( { MakeTargetObject( 1, "Invalid", -1 ) } );
		// 有限でないずれを持つ定義。
		const FWorldDefinition Infinite = MakeTargetWorld( { MakeTargetObject( 1, "Infinite", 2, FVector3( NMath::Infinity, 0.0f, 0.0f ) ), MakePlainObject( 2, "Target", FVector3::Zero ) } );
		C.Check( CountWorldIssues( NWorld::ValidateWorld( Self, nullptr ), EWorldIssueCode::BadEffect, EWorldIssueSeverity::Error ) == 1, "自分自身を指す終点指定は BadEffect のエラー" );
		C.Check( CountWorldIssues( NWorld::ValidateWorld( Missing, nullptr ), EWorldIssueCode::BadEffect, EWorldIssueSeverity::Error ) == 1 && CountWorldIssues( NWorld::ValidateWorld( Invalid, nullptr ), EWorldIssueCode::BadEffect, EWorldIssueSeverity::Error ) == 1, "存在しない Id と、使えない印の Target も BadEffect のエラー" );
		C.Check( CountWorldIssues( NWorld::ValidateWorld( Infinite, nullptr ), EWorldIssueCode::NonFiniteValue, EWorldIssueSeverity::Error ) == 1, "有限でない TargetOffset は NonFiniteValue のエラー" );
	}

	// 複製と貼り付けで、複製された対象を指すように Id が付け替わる。
	void TestTargetRemap( NAssetTest::FChecker& C )
	{
		C.Group = "effect-target-world-edit";
		// 文書の元。発射元(1)と標的(2)と、ほかのオブジェクト(3)。
		TWorldDocument Document;
		Document.Reset( MakeTargetWorld( { MakeTargetObject( 1, "Caster", 2 ), MakePlainObject( 2, "Target", FVector3::Zero ), MakePlainObject( 3, "Other", FVector3::Zero ) } ) );

		// 発射元だけを複製した結果の Id。
		const int32 SoloCopy = NWorldEdit::DuplicateObject( Document, 1 );
		const FWorldObject* Solo = Document.Get().FindObject( SoloCopy );
		C.Check( Solo != nullptr && Solo->Effect.IsSet() && Solo->Effect.GetValue().Target == 2, "発射元だけを複製すると、標的は元のまま（複製されていないものは付け替えない）" );

		// 発射元の下に標的を子として置いた文書。同じ根の部分木ごと複製すると、複製した標的を指す。
		TWorldDocument Nested;
		FWorldObject NestedCaster = MakeTargetObject( 1, "Caster", 2 );
		FWorldObject NestedTarget = MakePlainObject( 2, "Target", FVector3::Zero );
		NestedTarget.Parent = 1;
		Nested.Reset( MakeTargetWorld( { NestedCaster, NestedTarget } ) );
		const int32 NestedCopy = NWorldEdit::DuplicateObject( Nested, 1 );
		const FWorldObject* NestedCopiedCaster = Nested.Get().FindObject( NestedCopy );
		int32 NestedCopiedTarget = WorldNoId;
		for ( const FWorldObject& Object : Nested.Get().Objects )
		{
			if ( Object.Parent == NestedCopy ) NestedCopiedTarget = Object.Id;
		}
		C.Check( NestedCopiedCaster != nullptr && NestedCopiedCaster->Effect.IsSet() && NestedCopiedTarget != WorldNoId && NestedCopiedTarget != 2 && NestedCopiedCaster->Effect.GetValue().Target == NestedCopiedTarget, "標的が子として同じ部分木に入っているものを複製すると、複製した発射元は複製した標的を指す" );

		// 発射元と標的の Id。
		TArray<int32> Pair;
		Pair.Add( 1 );
		Pair.Add( 2 );
		// 一緒にコピーしたクリップボード。
		const NWorldEdit::FWorldClipboard Clipboard = NWorldEdit::CopyObjects( Document.Get(), Pair );
		const TArray<int32> Pasted = NWorldEdit::PasteObjects( Document, Clipboard );
		const FWorldObject* PastedCaster = nullptr;
		for ( const int32 Id : Pasted )
		{
			const FWorldObject* Candidate = Document.Get().FindObject( Id );
			if ( Candidate != nullptr && Candidate->Effect.IsSet() ) PastedCaster = Candidate;
		}
		C.Check( Pasted.Num() == 2 && PastedCaster != nullptr && PastedCaster->Effect.GetValue().Target != 2 && Document.Get().FindObject( PastedCaster->Effect.GetValue().Target ) != nullptr && Pasted.Contains( PastedCaster->Effect.GetValue().Target ), "発射元と標的を一緒に貼り付けると、貼った発射元は貼った標的を指す" );

		// 発射元だけのクリップボード。
		TArray<int32> CasterOnly;
		CasterOnly.Add( 1 );
		const TArray<int32> PastedAlone = NWorldEdit::PasteObjects( Document, NWorldEdit::CopyObjects( Document.Get(), CasterOnly ) );
		const FWorldObject* AloneCaster = PastedAlone.Num() == 1 ? Document.Get().FindObject( PastedAlone[0] ) : nullptr;
		C.Check( AloneCaster != nullptr && AloneCaster->Effect.IsSet() && AloneCaster->Effect.GetValue().Target == 2, "発射元だけを貼り付けると、標的は元の Id のまま" );
	}

	// 動的終点つきの雷を放つ、動かない長生きの発生源を作る。
	FParticleEmitterDefinition MakeBolt( FEffectPathDefinition::EKind Kind, bool bUseTarget )
	{
		// 雷にするエミッター。
		FParticleEmitterDefinition Bolt = QuietEmitter();
		Bolt.Path.Kind = Kind;
		Bolt.Path.bUseTarget = bUseTarget;
		Bolt.Path.Branches = 0;
		Bolt.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Bolt.Bursts.Add( Burst( 0.0f, 1 ) );
		Bolt.Lifetime = { 20.0f, 20.0f };
		Bolt.StartSize = Bolt.EndSize = { 0.12f, 0.12f };
		return Bolt;
	}

	// 内蔵の試し再生の状態を調べる。作成に失敗した場合は空。
	const FParticleSimulation* PreviewSimulation( const FEffectEditorState& State )
	{
		// 型を確認してから読み取る、内蔵の再生。
		const FParticlePlayback* Playback = dynamic_cast<const FParticlePlayback*>( State.Playback.Get() );
		return Playback != nullptr ? &Playback->GetSimulation() : nullptr;
	}

	// 配置時に、終点の指定が対象オブジェクトの追従へ接続されることを確かめる。
	void TestTargetSpawn( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "effect-target-world-spawn";
		// 配置する雷のアセット。
		const FString BoltPath = "effect-target/bolt.effect.json";
		C.Check( Assets.SaveEffect( BoltPath, *MakeEffect( { MakeBolt( FEffectPathDefinition::EKind::Lightning, true ) }, 0.0f, true, 73 ) ), "配置確認用の動的終点つきの雷アセットを保存する" );
		// 共有シーンを汚さない、テスト専用のシーン。
		CScene SpawnScene;
		// 発射元(1)・標的(2)・存在しない標的(3)・自分自身(4)・有限でないずれ(5)。発射元は標的より前に並べ、生成順に依存しないことも確かめる。
		FWorldDefinition World = MakeTargetWorld( { MakeTargetObject( 1, "Caster", 2, FVector3( 0.0f, 1.0f, 0.0f ) ), MakePlainObject( 2, "Target", FVector3( 5.0f, 0.0f, 0.0f ) ), MakeTargetObject( 3, "Missing", 9 ), MakeTargetObject( 4, "Self", 4 ), MakeTargetObject( 5, "Infinite", 2, FVector3( std::numeric_limits<float32>::quiet_NaN(), 0.0f, 0.0f ) ) } );
		for ( FWorldObject& Object : World.Objects )
		{
			if ( !Object.Effect.IsSet() ) continue;
			Object.Effect.GetValue().Path = BoltPath;
			Object.Effect.GetValue().bAutoPlay = false;
		}
		// 組み込みの設置関数だけが入ったレジストリ。
		const FWorldSpawnerRegistry Registry = FWorldSpawnerRegistry::CreateWithBuiltins();
		// 配置の設定。
		FWorldSpawnOptions Options;
		Options.Registry = &Registry;
		Options.Assets = &Assets;
		// 結び付けのタグを使わない配置。
		FWorldBindings Bindings;
		// 置いた結果。
		FWorldInstance Level = NWorld::Spawn( SpawnScene, MakeShared<FWorldDefinition>( World ), Bindings, Options );
		// 置き物と、その Effect 部品。
		const TSharedPtr<CGameObject> CasterHost = Level.FindById( 1 );
		const TSharedPtr<CGameObject> TargetHost = Level.FindById( 2 );
		CEffectComponent* CasterFx = CasterHost.IsValid() ? CasterHost->GetComponent<CEffectComponent>() : nullptr;
		C.Check( CasterFx != nullptr && TargetHost.IsValid() && CasterFx->GetTargetObject() == TargetHost && Near3( CasterFx->GetTargetOffset(), FVector3( 0.0f, 1.0f, 0.0f ) ), "終点に別のオブジェクトを指定した Effect は、配置後にそのオブジェクトとずれを追従先に持つ（標的が後ろに並んでいても）" );
		// 終点の解決結果。
		FVector3 Resolved = FVector3::Zero;
		C.Check( CasterFx != nullptr && CasterFx->Play() && CasterFx->TryGetTargetPosition( Resolved ) && Near3( Resolved, FVector3( 5.0f, 1.0f, 0.0f ) ), "再生すると、終点は標的の位置 + ずれ（5, 1, 0）になる" );
		// 接続されなかった三つの Effect 部品。
		CEffectComponent* MissingFx = Level.FindById( 3 ).IsValid() ? Level.FindById( 3 )->GetComponent<CEffectComponent>() : nullptr;
		CEffectComponent* SelfFx = Level.FindById( 4 ).IsValid() ? Level.FindById( 4 )->GetComponent<CEffectComponent>() : nullptr;
		CEffectComponent* InfiniteFx = Level.FindById( 5 ).IsValid() ? Level.FindById( 5 )->GetComponent<CEffectComponent>() : nullptr;
		C.Check( MissingFx != nullptr && SelfFx != nullptr && InfiniteFx != nullptr && !MissingFx->GetTargetObject().IsValid() && !SelfFx->GetTargetObject().IsValid() && !InfiniteFx->GetTargetObject().IsValid(), "存在しない標的・自分自身・有限でないずれは、追従先を接続しない" );
		// 接続しなかった理由を伝える警告。
		int32 NumTargetWarnings = 0;
		for ( const FString& Warning : Level.GetReport().Warnings )
		{
			if ( Warning.Contains( "target #" ) ) ++NumTargetWarnings;
		}
		C.Check( NumTargetWarnings == 3, "接続しなかった三つは、それぞれ警告で報告される（%d 件）", NumTargetWarnings );
		Level.Despawn();
	}

	// Effect Editor の文書に雷の終点指定があるときの試し再生の既定と、選択に依らない判定を確かめる。
	void TestEditorTargetState( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "effect-target-editor-state";
		// 板(0)と、動的終点つきの雷(1)を並べた文書。先頭の板を選んだままにする。
		FParticleEffectDefinition Document;
		Document.Name = "Target editor";
		Document.Emitters.Add( QuietEmitter() );
		Document.Emitters.Add( MakeBolt( FEffectPathDefinition::EKind::Lightning, true ) );
		C.Check( NEffectEditorUI::FindTargetPathEmitter( Document ) == &Document.Emitters[1], "動的終点つきの雷は、選択中のエミッターに関係なく文書から見つかる" );
		// 終点を使わない各種の文書。
		FParticleEffectDefinition Plain = Document;
		Plain.Emitters[1].Path.bUseTarget = false;
		FParticleEffectDefinition Ribbon = Document;
		Ribbon.Emitters[1].Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		// UseTarget が真偽値でない JSON から読んだ、型不正の印つきの文書。
		FParticleEffectDefinition Broken;
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"Path\":{\"Kind\":\"Lightning\",\"UseTarget\":\"yes\"}}]}" ), Broken ) && Broken.Emitters.Num() == 1 && Broken.Emitters[0].Path.HasInvalidUseTarget(), "UseTarget が真偽値でない JSON は、型不正の印つきで読める" );
		FParticleEffectDefinition Tree = Document;
		Tree.Emitters[1].Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		C.Check( NEffectEditorUI::FindTargetPathEmitter( Plain ) == nullptr && NEffectEditorUI::FindTargetPathEmitter( Ribbon ) == nullptr && NEffectEditorUI::FindTargetPathEmitter( Broken ) == nullptr && NEffectEditorUI::FindTargetPathEmitter( Tree ) == &Tree.Emitters[1], "終点を使わない・帯・型不正は対象にせず、樹状雷は対象にする" );

		// 開く文書。
		FEffectEditorState State;
		State.Document.Reset( Document );
		State.bInitialized = true;
		State.SelectedEmitter = 0;
		State.bTreeCameraFocusPending = true;
		NEffectEditorUI::ApplyInitialPreviewTarget( State );
		C.Check( State.bPreviewTargetEnabled && State.bPreviewTargetCameraFocusPending && !State.bTreeCameraFocusPending && !State.bEffectGeometryCameraFocusPending, "雷の終点つきの文書を開くと、試し再生の終点が有効になり、終点を含むカメラ合わせが一度だけ保留される" );
		FEffectEditorState PlainState;
		PlainState.Document.Reset( Plain );
		NEffectEditorUI::ApplyInitialPreviewTarget( PlainState );
		C.Check( !PlainState.bPreviewTargetEnabled && !PlainState.bPreviewTargetCameraFocusPending, "終点を使わない文書では、試し再生の終点を有効にしない" );

		// 保存した文書を実際に開く・新規にする。
		C.Check( Assets.SaveEffect( "effect-target/editor_bolt.effect.json", Document ), "終点つきの雷を含む文書を保存する" );
		FEffectEditorState Opened;
		C.Check( NDebugUI::OpenEffectFile( Opened, "effect-target/editor_bolt.effect.json" ) && Opened.bPreviewTargetEnabled && Opened.bPreviewTargetCameraFocusPending, "保存した文書を開くと、終点が有効になる" );
		NDebugUI::NewEffect( Opened );
		C.Check( !Opened.bPreviewTargetEnabled && !Opened.bPreviewTargetCameraFocusPending && Near3( Opened.PreviewTargetPosition, FVector3( 0.0f, 1.0f, 0.0f ) ), "新規文書へ切り替えると、終点は無効で既定位置に戻る" );
		C.Check( NDebugUI::NewEffectFromPreset( Opened, "effect-target/editor_bolt.effect.json" ) && Opened.bPreviewTargetEnabled && Opened.FilePath.IsEmpty(), "複製として開いた文書でも、終点が有効になる（保存先は空のまま）" );

		// 終点の操作の前の、文書の版と内容。
		const uint32 RevisionBefore = Opened.Document.GetRevision();
		const NlohmannJson JsonBefore = Opened.Document.Get().ToJson();
		// 再生への反映: 板を選んだままでも、雷は終点へ向かう。
		NDebugUI::UpdateEffectEditor( Opened, 0.1f );
		Opened.SelectedEmitter = 0;
		Opened.PreviewTargetPosition = FVector3( 2.0f, 1.0f, -1.0f );
		NDebugUI::UpdateEffectEditor( Opened, 0.1f );
		// 再生している内蔵のシミュレーション。
		const FParticleSimulation* Simulation = PreviewSimulation( Opened );
		C.Check( Simulation != nullptr && Simulation->bHasTarget && Near3( Simulation->TargetPosition, FVector3( 2.0f, 1.0f, -1.0f ) ), "板のエミッターを選んでいても、試し再生は雷へ終点を渡す" );
		Opened.bPreviewTargetEnabled = false;
		NDebugUI::UpdateEffectEditor( Opened, 0.1f );
		Simulation = PreviewSimulation( Opened );
		C.Check( Simulation != nullptr && !Simulation->bHasTarget, "試し再生の終点を無効にすると、終点は外れる" );
		C.Check( Opened.Document.Get().Emitters[1].Path.bUseTarget && Opened.Document.GetRevision() == RevisionBefore && Opened.Document.Get().ToJson() == JsonBefore, "終点の操作は文書を変えない（保存値の UseTarget は残り、文書の版も内容も変わらない）" );
	}

	// 試し再生の終点を、画面のクリック位置・カメラ・ガイドで扱えることを確かめる。
	void TestEditorTargetView( NAssetTest::FChecker& C )
	{
		C.Group = "effect-target-editor-view";
		// 板(0)と、動的終点つきの雷(1)を並べた文書を持つ、有効な視界つきの状態。
		FEffectEditorState State;
		FParticleEffectDefinition Document;
		Document.Emitters.Add( QuietEmitter() );
		Document.Emitters.Add( MakeBolt( FEffectPathDefinition::EKind::Lightning, true ) );
		State.Document.Reset( Document );
		State.View.Width = 800;
		State.View.Height = 600;

		// 画面の中央をクリックしたときの結果。
		const bool bPlaced = NEffectEditorUI::SetPreviewTargetFromScreenRay( State, 400.0f, 300.0f );
		// 置いた点を画面へ投影し直した位置。
		FVector2 Back;
		C.Check( bPlaced && State.bPreviewTargetEnabled && NMath::Abs( State.PreviewTargetPosition.y ) < 1.0e-3f && State.Camera.WorldToScreen( State.View, State.PreviewTargetPosition, Back ) && NMath::Abs( Back.x - 400.0f ) < 1.0f && NMath::Abs( Back.y - 300.0f ) < 1.0f, "画面の位置をクリックすると、その位置の水平面（y = 0）上へ終点を置き、有効にする" );
		// 画面外・非有限の位置。
		const FVector3 BeforeFailure = State.PreviewTargetPosition;
		C.Check( !NEffectEditorUI::SetPreviewTargetFromScreenRay( State, -1.0f, 10.0f ) && !NEffectEditorUI::SetPreviewTargetFromScreenRay( State, 10.0f, 600.0f ) && !NEffectEditorUI::SetPreviewTargetFromScreenRay( State, std::numeric_limits<float32>::quiet_NaN(), 10.0f ) && Near3( State.PreviewTargetPosition, BeforeFailure ), "画面の外・非有限の位置では置かず、終点を変えない" );
		// 終点を使わない文書では置けない。
		FEffectEditorState PlainState;
		PlainState.Document.Reset( FParticleEffectDefinition() );
		PlainState.View = State.View;
		C.Check( !NEffectEditorUI::SetPreviewTargetFromScreenRay( PlainState, 400.0f, 300.0f ) && !PlainState.bPreviewTargetEnabled, "終点を使う雷が無い文書では、クリックしても終点を置かない" );

		// カメラ合わせ。
		State.PreviewTargetPosition = FVector3( 6.0f, 2.0f, 0.0f );
		State.bPreviewTargetEnabled = true;
		C.Check( NEffectEditorUI::FocusCameraOnPreviewTarget( State, State.View ) && Near3( State.Camera.Target, FVector3( 3.0f, 1.0f, 0.0f ) ) && State.Camera.Distance > 3.0f, "終点にカメラを合わせると、発生位置と終点の中点を見て、両方が収まる距離になる" );
		// 失敗時に保つカメラ。
		const FEditorCamera Previous = State.Camera;
		State.PreviewTargetPosition = FVector3( NMath::Infinity, 0.0f, 0.0f );
		C.Check( !NEffectEditorUI::FocusCameraOnPreviewTarget( State, State.View ) && Near3( State.Camera.Target, Previous.Target ) && State.Camera.Distance == Previous.Distance, "終点が有限でないときはカメラ合わせに失敗し、元のカメラを保つ" );
		State.PreviewTargetPosition = FVector3( 6.0f, 2.0f, 0.0f );
		State.bPreviewTargetEnabled = false;
		C.Check( !NEffectEditorUI::FocusCameraOnPreviewTarget( State, State.View ), "終点が無効のときはカメラを動かさない" );

		// ガイド。
		State.bPreviewTargetEnabled = true;
		State.bShowPreviewTargetGuides = true;
		// ガイドを記録した命令。
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		NEffectEditorUI::RecordPreviewTargetGuides( State, Draw );
		// 読み取った線分。
		TArray<FDrawLine3DData> Lines;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Line3D ) Lines.Add( Command.Read<FDrawLine3DData>() );
		} );
		C.Check( Lines.Num() == 4 && Near3( FVector3( Lines[0].P0[0], Lines[0].P0[1], Lines[0].P0[2] ), FVector3::Zero ) && Near3( FVector3( Lines[0].P1[0], Lines[0].P1[1], Lines[0].P1[2] ), FVector3( 6.0f, 2.0f, 0.0f ) ), "ガイドは、発生位置から終点への接続線と、終点の三軸の目印（線 4 本）を記録する" );
		State.bShowPreviewTargetGuides = false;
		FDrawBuffer Hidden;
		FDrawContext HiddenDraw( Hidden );
		NEffectEditorUI::RecordPreviewTargetGuides( State, HiddenDraw );
		State.bShowPreviewTargetGuides = true;
		State.PreviewTargetPosition = FVector3( std::numeric_limits<float32>::quiet_NaN(), 0.0f, 0.0f );
		FDrawBuffer Invalid;
		FDrawContext InvalidDraw( Invalid );
		NEffectEditorUI::RecordPreviewTargetGuides( State, InvalidDraw );
		FDrawBuffer NoTarget;
		FDrawContext NoTargetDraw( NoTarget );
		NEffectEditorUI::RecordPreviewTargetGuides( PlainState, NoTargetDraw );
		C.Check( Hidden.Words.empty() && Invalid.Words.empty() && NoTarget.Words.empty(), "ガイドを隠したとき・終点が有限でないとき・終点を使う雷が無いときは、何も記録しない" );
	}
}

// 純粋な World の終点指定の検査。
void NWorldTest::RunEffectTargetWorldTests( NAssetTest::FChecker& C )
{
	TestTargetJson( C );
	TestTargetValidation( C );
	TestTargetRemap( C );
}

// 配置とエディタの検査。
void NWorldTest::RunEffectTargetSceneEditorTests( NAssetTest::FChecker& C, CAssetManager& Assets )
{
	TestTargetSpawn( C, Assets );
	TestEditorTargetState( C, Assets );
	TestEditorTargetView( C );
}
