#include <algorithm>

#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldHierarchy.h"

//=============================================================================
// ワールドエディタ: エフェクトのプレビュー
//   Effect 部品を持つ（有効な）オブジェクトごとに、実行時と同じバックエンド（EffectBackend.h）で再生して、視界に出す。
//   - 部品の値（ファイル・拡大・速さ・シード・繰り返し）を変えると、最初からやり直す。AutoPlay の値には関係なく、いつも再生する（見るため）。
//   - 繰り返さないエフェクトは、終わって少し待ってから、また最初から（何度でも見られる）。
//   - 入れ子のワールド（Prefab）の中のエフェクトは、再生しない（その .world を開いて確かめる）。
//=============================================================================
namespace
{
	constexpr float32 RestartDelay = 0.75f;   // 終わってから、やり直すまでの秒
	constexpr float32 MaxStep = 0.1f;         // 1 フレームで進める時間の上限（止まっていたあとに、粒子が飛ばないように）

	// 再生をやり直す条件の識別（部品の値と、やり直しの押された回数）
	uint64 HashEffect( const FWorldEffect& Effect, uint32 Serial )
	{
		uint64 Hash = 1469598103934665603ull;
		const auto Mix = [&]( const void* Data, usize Size )
		{
			const uint8* Bytes = static_cast<const uint8*>( Data );
			for ( usize i = 0; i < Size; ++i )
			{
				Hash ^= Bytes[i];
				Hash *= 1099511628211ull;
			}
		};
		for ( int32 i = 0; i < Effect.Path.Len(); ++i ) Mix( &Effect.Path[i], 1 );
		Mix( &Effect.Scale, sizeof( Effect.Scale ) );
		Mix( &Effect.Speed, sizeof( Effect.Speed ) );
		Mix( &Effect.Seed, sizeof( Effect.Seed ) );
		Mix( &Effect.Loop, sizeof( Effect.Loop ) );
		Mix( &Effect.Target, sizeof( Effect.Target ) );
		Mix( &Effect.TargetOffset, sizeof( Effect.TargetOffset ) );
		Mix( &Serial, sizeof( Serial ) );
		return Hash;
	}

	FEffectPlayParams MakeParams( const FWorldEffect& Effect )
	{
		FEffectPlayParams Params;
		Params.Scale = Effect.Scale;
		Params.Speed = Effect.Speed;
		Params.Seed = static_cast<uint32>( NMath::Max( Effect.Seed, 0 ) );
		Params.Loop = Effect.Loop;
		return Params;
	}
}

void NWorldEditorUI::UpdateEffectPreviews( FWorldEditorState& S, float32 DeltaTime )
{
	CAssetManager* Assets = GetAssets();
	if ( !S.bPlayEffects || Assets == nullptr )
	{
		S.EffectPreviews.Reset();
		return;
	}
	const FWorldDefinition& World = S.Session.Document.Get();
	const float32 Step = NMath::Clamp( DeltaTime, 0.0f, MaxStep );
	TSet<int32> Seen;
	TArray<FTransform> Transforms;
	for ( int32 Index = 0; Index < World.Objects.Num(); ++Index )
	{
		const FWorldObject& Object = World.Objects[Index];
		if ( !Object.bEnabled || !Object.Effect.IsSet() || Object.Effect.GetValue().Path.IsEmpty() ) continue;
		if ( Transforms.IsEmpty() ) Transforms = NWorld::ComputeAllWorldTransforms( World );
		const FWorldEffect& Spec = Object.Effect.GetValue();
		Seen.Add( Object.Id );

		FWorldEditorState::FEffectPreview& Preview = S.EffectPreviews.FindOrAdd( Object.Id );
		const uint64 Hash = HashEffect( Spec, S.EffectRestartSerial );
		bool bStart = Preview.Hash != Hash;
		if ( !bStart && Preview.Playback.IsValid() && Preview.Playback->IsFinished() )
		{
			Preview.FinishedTime += Step;
			bStart = Preview.FinishedTime >= RestartDelay;
		}
		if ( bStart )
		{
			Preview.Hash = Hash;
			Preview.FinishedTime = 0.0f;
			// 読み込めなければ、Playback は空のまま（値を変えるか、やり直しを押すまで、もう一度は試さない）
			Preview.Playback = FEffectBackendRegistry::Global().Play( Spec.Path, Assets, MakeParams( Spec ), Transforms[Index] );
		}
		if ( Preview.Playback.IsValid() )
		{
			Preview.Playback->SetTransform( Transforms[Index] );
			if ( Spec.Target != WorldNoId )
			{
				const int32 TargetIndex = World.FindIndexById( Spec.Target );
				if ( TargetIndex != INDEX_NONE && TargetIndex < Transforms.Num() && NMath::IsFinite( Spec.TargetOffset.x ) && NMath::IsFinite( Spec.TargetOffset.y ) && NMath::IsFinite( Spec.TargetOffset.z ) && Spec.Target != Object.Id )
				{
					const FVector3 TargetPosition = Transforms[TargetIndex].TransformPoint( Spec.TargetOffset );
					if ( NMath::IsFinite( TargetPosition.x ) && NMath::IsFinite( TargetPosition.y ) && NMath::IsFinite( TargetPosition.z ) ) Preview.Playback->SetTargetPosition( TargetPosition );
					else Preview.Playback->ClearTargetPosition();
				}
				else Preview.Playback->ClearTargetPosition();
			}
			else Preview.Playback->ClearTargetPosition();
			Preview.Playback->Update( Step );
		}
	}

	// 無くなった・無効になったオブジェクトの再生は捨てる
	TArray<int32> Stale;
	for ( const auto& Pair : S.EffectPreviews )
	{
		if ( !Seen.Contains( Pair.Key ) ) Stale.Add( Pair.Key );
	}
	for ( const int32 Id : Stale ) S.EffectPreviews.Remove( Id );
}

void NWorldEditorUI::RecordEffectPreviews( FWorldEditorState& S, FDrawContext& Draw )
{
	for ( const auto& Pair : S.EffectPreviews )
	{
		if ( Pair.Value.Playback.IsValid() ) Pair.Value.Playback->Record( Draw );
	}
}
