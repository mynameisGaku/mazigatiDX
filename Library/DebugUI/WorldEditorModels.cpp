// SPDX-License-Identifier: NOASSERTION


#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorModelPreview.h"
#include "Library/DebugUI/WorldEditorModelPreviewKey.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Asset/ModelAnimationAsset.h"
#include "Library/Framework/World/WorldHierarchy.h"

namespace
{
	// floatのbit列を比較し、同じNaN設定でも毎frame再構築しない。
	bool SameFloatBits( float32 Left, float32 Right )
	{
		return FMemory::Memcmp( &Left, &Right, sizeof( float32 ) ) == 0;
	}

	// bindingの存在と全保存値を比較し、不正値も安定した失敗設定として扱う。
	bool SameBinding( const TOptional<FWorldModelMaterialEffect>& Left, const TOptional<FWorldModelMaterialEffect>& Right )
	{
		if ( Left.IsSet() != Right.IsSet() ) return false;
		if ( !Left.IsSet() ) return true;
		const FWorldModelMaterialEffect& LeftValue = Left.GetValue(); // 比較する左側binding値
		const FWorldModelMaterialEffect& RightValue = Right.GetValue(); // 比較する右側binding値
		const bool bLeftValid = LeftValue.IsValidSettings(); // 左側bindingの設定有効性
		const bool bRightValid = RightValue.IsValidSettings(); // 右側bindingの設定有効性
		if ( bLeftValid != bRightValid ) return false;
		if ( bLeftValid ) return LeftValue.IsSameAs( RightValue );
		return LeftValue.ToJson().dump() == RightValue.ToJson().dump();
	}

	// model path、animation、clip、speed、bindingが同じか調べる。
	bool SameDefinition( const FWorldModel& Left, const FWorldModel& Right )
	{
		return Left.Path == Right.Path && Left.Animations == Right.Animations && Left.Clip == Right.Clip && SameFloatBits( Left.Speed, Right.Speed ) && SameBinding( Left.MaterialEffect, Right.MaterialEffect );
	}

	// 有効な材質効果がある失敗には対象材質番号を添える。
	FString MakePreviewFailure( const FWorldModel& Definition, const char* Reason )
	{
		if ( Definition.MaterialEffect.IsSet() && Definition.MaterialEffect.GetValue().Effect.Material.bEnabled ) return FString::Printf( "材質%dの効果を使えません: %s", Definition.MaterialEffect.GetValue().MaterialIndex, Reason );
		return Reason;
	}

	// model個体のsourceと設定を更新し、失敗も同じ入力で再試行しない値にする。
	void BuildModelPreview( FWorldEditorModelPreview& Preview, const FWorldModel& Definition, const TSharedPtr<CModel>& Source, const bool bSourceReady, const IDrawExecutor* Executor, const bool bExecutorSupported, FAssetEditorInfo* Info, CAssetManager* Assets )
	{
		Preview.Animator.ClearMaterialEffect();
		Preview.Animator = FModelAnimator();
		Preview.Source = Source;
		Preview.Definition = Definition;
		Preview.Executor = Executor;
		Preview.bExecutorSupported = bExecutorSupported;
		Preview.bSourceReady = bSourceReady;
		Preview.bBuilt = true;
		Preview.FailureReason.Empty();

		if ( !Source.IsValid() )
		{
			Preview.FailureReason = MakePreviewFailure( Definition, "モデルを読み込めません" );
			return;
		}
		if ( !bSourceReady )
		{
			Preview.FailureReason = MakePreviewFailure( Definition, "モデルの読み込み完了を待っています" );
			return;
		}

		if ( !Definition.Animations.IsEmpty() && Assets != nullptr )
		{
			const TSharedPtr<CModelAnimationAsset> AnimationSet = Assets->LoadModelAnimations( Definition.Animations ); // 保存指定のanimation asset
			if ( AnimationSet.IsValid() ) Preview.Animator = AnimationSet->CreateAnimatorFor( Source );
		}
		if ( !Preview.Animator.Instance.IsValid() )
		{
			Preview.Animator = FModelAnimator( Source->CreateInstance() );
			if ( Preview.Animator.Instance.IsValid() ) Preview.Animator.AddClipsFrom( Source );
		}
		if ( !Preview.Animator.Instance.IsValid() )
		{
			Preview.FailureReason = MakePreviewFailure( Definition, "モデル個体を作成できません" );
			return;
		}

		Preview.Animator.Speed = Definition.Speed;
		if ( !Definition.Clip.IsEmpty() && !Preview.Animator.Play( Definition.Clip, 0.0f ) ) Preview.FailureReason = "指定されたanimation clipがありません: " + Definition.Clip;

		if ( !Definition.MaterialEffect.IsSet() || !Definition.MaterialEffect.GetValue().Effect.Material.bEnabled ) return;
		const FWorldModelMaterialEffect& Binding = Definition.MaterialEffect.GetValue(); // 有効な材質効果binding
		if ( !Binding.IsValidSettings() ) Preview.FailureReason = FString::Printf( "材質%dの効果設定が不正です", Binding.MaterialIndex );
		else if ( !NAssetService::IsMainThread() ) Preview.FailureReason = FString::Printf( "材質%dの効果準備はメインスレッドで行う必要があります", Binding.MaterialIndex );
		else if ( Executor == nullptr ) Preview.FailureReason = FString::Printf( "材質%dの効果を描く実行先がありません", Binding.MaterialIndex );
		else if ( !bExecutorSupported ) Preview.FailureReason = FString::Printf( "描画先が材質%dの効果に対応していません", Binding.MaterialIndex );
		else if ( Preview.Animator.Instance->GetNumAttached() > 0 ) Preview.FailureReason = FString::Printf( "材質%dの効果はanimationが結び付いたmodelに使えません", Binding.MaterialIndex );
		else if ( Info == nullptr || !Preview.Animator.PrepareMaterialEffect( Binding.MaterialIndex, Binding.Effect, [Info]( const FString& Path ) { return Info->GetTexture( Path ); }, *Executor, Binding.bTwoSided ) ) Preview.FailureReason = FString::Printf( "材質%dの面情報または効果画像を準備できません", Binding.MaterialIndex );
		else Preview.FailureReason.Empty();
	}

	// 一つのworldを走査し、objectId経路ごとにmodel previewを更新する。
	void UpdateWorldModelPreviews( FWorldEditorState& State, const FWorldDefinition& World, TArray<int32>& ObjectIds, TArray<FString>& PrefabStack, int32 PrefabDepth, TSet<FWorldEditorModelPreviewKey>& Seen, float32 DeltaTime, const IDrawExecutor* Executor, bool bExecutorSupported )
	{
		FAssetEditorInfo* Info = State.AssetInfo.Get(); // pathごとのasset cache
		CAssetManager* Assets = NWorldEditorUI::GetAssets(); // world animation assetを読むmanager
		for ( const FWorldObject& Object : World.Objects )
		{
			if ( !Object.bEnabled ) continue;
			ObjectIds.Add( Object.Id );
			if ( Object.Model.IsSet() )
			{
				FWorldEditorModelPreviewKey Key; // rootからこのmodelまでのidentity
				Key.ObjectIds = ObjectIds;
				Seen.Add( Key );
				FWorldEditorModelPreview& Preview = State.ModelPreviews.FindOrAdd( Key );
				const FWorldModel& Definition = Object.Model.GetValue(); // このobjectが保存したmodel設定
				const TSharedPtr<CModel> Source = Info != nullptr ? Info->GetModelSource( Definition.Path ) : TSharedPtr<CModel>(); // 共有instanceを触らずmodel sourceを読む
				const bool bSourceReady = Source.IsValid() && Source->IsReady(); // 非同期load完了状態
				const bool bRebuild = !Preview.bBuilt || !SameDefinition( Preview.Definition, Definition ) || Preview.Source.Get() != Source.Get() || Preview.Executor != Executor || Preview.bExecutorSupported != bExecutorSupported || Preview.bSourceReady != bSourceReady; // 構成またはowner変更
				if ( bRebuild ) BuildModelPreview( Preview, Definition, Source, bSourceReady, Executor, bExecutorSupported, Info, Assets );
				if ( Preview.Animator.Instance.IsValid() ) Preview.Animator.Update( DeltaTime );
			}

			if ( Object.Prefab.IsSet() && Info != nullptr && PrefabDepth < WorldMaxPrefabDepth )
			{
				const FString Path = Object.Prefab.GetValue().Path; // nested world path
				if ( !Path.IsEmpty() )
				{
					const FString Normalized = NWorld::NormalizeAssetPath( Path ); // cycle検知に使う正規path
					bool bCycle = false; // active recursion stackとの重複
					for ( const FString& ActivePath : PrefabStack ) bCycle = bCycle || ActivePath == Normalized;
					if ( !bCycle )
					{
						if ( const FWorldDefinition* Contents = Info->GetWorld( Path ) )
						{
							PrefabStack.Add( Normalized );
							UpdateWorldModelPreviews( State, *Contents, ObjectIds, PrefabStack, PrefabDepth + 1, Seen, DeltaTime, Executor, bExecutorSupported );
							PrefabStack.RemoveAt( PrefabStack.Num() - 1 );
						}
					}
				}
			}
			ObjectIds.RemoveAt( ObjectIds.Num() - 1 );
		}
	}
}

void NWorldEditorUI::UpdateModelMaterialPreviews( FWorldEditorState& S, float32 DeltaTime, const IDrawExecutor* Executor )
{
	if ( !NAssetService::IsMainThread() ) return;
	TSet<FWorldEditorModelPreviewKey> Seen; // このframeに有効なmodel object経路
	TArray<int32> ObjectIds; // nested prefabまでのobject経路
	TArray<FString> PrefabStack; // cycle検知用のactive normalized path
	const bool bExecutorSupported = Executor != nullptr && Executor->SupportsModelMaterialEffects(); // update段階で一度だけ能力を調べる
	UpdateWorldModelPreviews( S, S.Session.Document.Get(), ObjectIds, PrefabStack, 0, Seen, DeltaTime, Executor, bExecutorSupported );
	PruneModelPreviews( S, Seen );
}

bool NWorldEditorUI::RecordModelObject( const FWorldEditorState& S, const FWorldEditorModelPreviewKey& Key, const FTransform& Placed, FDrawContext& Draw )
{
	if ( Key.ObjectIds.IsEmpty() ) return false;
	const FWorldEditorModelPreview* Preview = S.ModelPreviews.Find( Key ); // update段階で準備した個体view
	if ( Preview == nullptr || !Preview->bBuilt || !Preview->Animator.Instance.IsValid() ) return false;
	Preview->Animator.Record( Draw, Placed.ToMatrix() );
	return true;
}

void NWorldEditorUI::PruneModelPreviews( FWorldEditorState& S, const TSet<FWorldEditorModelPreviewKey>& Seen )
{
	TArray<FWorldEditorModelPreviewKey> Gone; // disabled・削除・cycleで見えなくなった個体
	for ( const auto& Pair : S.ModelPreviews ) if ( !Seen.Contains( Pair.Key ) ) Gone.Add( Pair.Key );
	for ( const FWorldEditorModelPreviewKey& Key : Gone ) S.ModelPreviews.Remove( Key );
}
