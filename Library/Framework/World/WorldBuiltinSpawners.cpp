#include <algorithm>

#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Audio/AudioComponents.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Render/Billboard.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Framework/World/WorldSpawner.h"

//=============================================================================
// Framework が持つ部品の設置（Model / Billboard / Mesh / Sound / Effect）
//   物理（Collider）は、Framework が Physics を知らないように、Physics 側が登録する（NPhysicsWorld::RegisterSpawners）。
//=============================================================================
namespace
{
	void SpawnModel( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Model.IsSet() ) return;
		const FWorldModel& Spec = Context.Definition.Model.GetValue();
		if ( Context.Assets == nullptr )
		{
			Context.Warn( "the model '" + Spec.Path + "' was not placed: the asset manager is not running" );
			return;
		}
		if ( Spec.Path.IsEmpty() )
		{
			Context.Warn( "the model has no file" );
			return;
		}
		const TSharedPtr<CModel> Model = Context.Assets->LoadModel( Spec.Path );
		if ( !Model.IsValid() )
		{
			Context.Warn( "the model '" + Spec.Path + "' could not be loaded" );
			return;
		}

		// アニメーション集があれば、その設定（クリップ・補正）を使う。無い、または読めなければ、モデルに入っているアニメーションをそのまま
		FModelAnimator Animator;
		if ( !Spec.Animations.IsEmpty() )
		{
			if ( const TSharedPtr<CModelAnimationAsset> Set = Context.Assets->LoadModelAnimations( Spec.Animations ); Set.IsValid() ) Animator = Set->CreateAnimatorFor( Model );
			else Context.Warn( "the animation set '" + Spec.Animations + "' could not be loaded; the model's own animations are used" );
		}
		if ( !Animator.Instance.IsValid() )
		{
			Animator = FModelAnimator( Model->CreateInstance() );
			Animator.AddClipsFrom( Model );
		}
		if ( !Animator.Instance.IsValid() )
		{
			Context.Warn( "the model '" + Spec.Path + "' cannot be shown (it has no mesh?)" );
			return;
		}

		Animator.Speed = Spec.Speed;
		if ( !Spec.Clip.IsEmpty() && !Animator.Play( Spec.Clip, 0.0f ) ) Context.Warn( "the model has no clip named '" + Spec.Clip + "'" );
		Context.Object.AddData( Animator );
	}

	void SpawnBillboard( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Billboard.IsSet() ) return;
		const FWorldBillboard& Spec = Context.Definition.Billboard.GetValue();
		if ( Context.Assets == nullptr )
		{
			Context.Warn( "the billboard '" + Spec.Texture + "' was not placed: the asset manager is not running" );
			return;
		}
		const TSharedPtr<CTexture> Texture = Spec.Texture.IsEmpty() ? TSharedPtr<CTexture>() : Context.Assets->LoadTexture( Spec.Texture );
		if ( !Texture.IsValid() )
		{
			Context.Warn( "the billboard's image '" + Spec.Texture + "' could not be loaded" );
			return;
		}

		bool bRectApplied = true;
		const FBillboard Board = NWorld::MakeBillboard( Spec, Texture, &bRectApplied );
		if ( !bRectApplied ) Context.Warn( "the billboard's rectangle could not be used (the image size is not known yet, or the rectangle is empty)" );
		Context.Object.AddData( Board );
	}

	void SpawnMesh( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Mesh.IsSet() ) return;
		const FEditMesh& Mesh = Context.Definition.Mesh.GetValue();
		if ( Mesh.GetNumTriangles() == 0 )
		{
			Context.Warn( "the mesh has no usable faces and was not placed" );
			return;
		}
		const FMeshRenderer Renderer = NWorld::MakeMeshRenderer( Mesh, [&Context]( const FString& Path )
		{
			const TSharedPtr<CTexture> Texture = Context.Assets != nullptr ? Context.Assets->LoadTexture( Path ) : TSharedPtr<CTexture>();
			if ( !Texture.IsValid() ) Context.Warn( "the mesh material's image '" + Path + "' could not be loaded (the material is drawn with its colour only)" );
			return Texture;
		} );
		if ( !Renderer.IsValid() )
		{
			Context.Warn( "the mesh could not be made (the asset manager is not running, or the mesh is too big)" );
			return;
		}
		if ( Renderer.HasEffectPreparationFailures() ) Context.Warn( "有効な材質効果の画像または描画用データを準備できず、該当slotは非表示になります" );
		Context.Object.AddData( Renderer );
	}

	void SpawnSound( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Sound.IsSet() ) return;
		const FWorldSound& Spec = Context.Definition.Sound.GetValue();
		if ( Context.Assets == nullptr )
		{
			Context.Warn( "the sound '" + Spec.Path + "' was not placed: the asset manager is not running" );
			return;
		}
		const TSharedPtr<CSound> Sound = Spec.Path.IsEmpty() ? TSharedPtr<CSound>() : Context.Assets->LoadSound( Spec.Path );
		if ( !Sound.IsValid() )
		{
			Context.Warn( "the sound '" + Spec.Path + "' could not be loaded" );
			return;
		}

		CAudioSourceComponent* Source = Context.Object.AddComponent<CAudioSourceComponent>( Sound );
		EAudioBus Bus = EAudioBus::Ambient;
		if ( !TryParse( Spec.Bus, Bus ) ) Context.Warn( "the sound's bus '" + Spec.Bus + "' is unknown; Ambient is used" );
		Source->SetBus( Bus );
		Source->SetVolume( Spec.Volume );
		Source->SetPitch( Spec.Pitch );
		Source->SetLoop( Spec.bLoop );
		Source->SetRange( Spec.MinDistance, Spec.MaxDistance );
		Source->SetAutoPlay( Spec.bAutoPlay );
	}

	void SpawnEffect( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Effect.IsSet() ) return;
		const FWorldEffect& Spec = Context.Definition.Effect.GetValue();
		if ( Context.Assets == nullptr )
		{
			Context.Warn( "the effect '" + Spec.Path + "' was not placed: the asset manager is not running" );
			return;
		}
		if ( Spec.Path.IsEmpty() )
		{
			Context.Warn( "the effect has no file" );
			return;
		}
		// 読み込めるかは、始めるとき（Play）に分かる。ここでは、扱えるバックエンドがあるかだけを見る
		if ( FEffectBackendRegistry::Global().Find( Spec.Path ) == nullptr )
		{
			Context.Warn( "the effect '" + Spec.Path + "' was not placed: no effect backend handles this kind of file" );
			return;
		}

		CEffectComponent* Effect = Context.Object.AddComponent<CEffectComponent>( Spec.Path );
		FEffectPlayParams Params;
		Params.Scale = Spec.Scale;
		Params.Speed = Spec.Speed;
		Params.Seed = static_cast<uint32>( std::max( Spec.Seed, 0 ) );
		Params.Loop = Spec.Loop;
		Effect->SetParams( Params );
		Effect->SetAutoPlay( Spec.bAutoPlay );
	}
}

void NWorld::RegisterBuiltinSpawners( FWorldSpawnerRegistry& Registry )
{
	Registry.Add( "Model", SpawnModel );
	Registry.Add( "Billboard", SpawnBillboard );
	Registry.Add( "Mesh", SpawnMesh );
	Registry.Add( "Sound", SpawnSound );
	Registry.Add( "Effect", SpawnEffect );
}
