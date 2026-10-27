// SPDX-License-Identifier: MIT
#include "EffectComponent.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Object/GameObject.h"

//=============================================================================
// CEffectComponent
//=============================================================================
void CEffectComponent::SetTargetPosition( const FVector3& InWorldPosition )
{
	TargetObject.Reset();
	TargetOffset = FVector3::Zero;
	bUseTargetObject = false;
	ExplicitTargetPosition = InWorldPosition;
	bHasExplicitTargetPosition = true;
	SyncPlaybackTarget();
}

void CEffectComponent::SetTargetObject( const TSharedPtr<CGameObject>& InTarget, const FVector3& InLocalOffset )
{
	if ( !InTarget.IsValid() )
	{
		ClearTarget();
		return;
	}
	TargetObject = InTarget;
	TargetOffset = InLocalOffset;
	bUseTargetObject = true;
	ExplicitTargetPosition = FVector3::Zero;
	bHasExplicitTargetPosition = false;
	SyncPlaybackTarget();
}

void CEffectComponent::ClearTarget()
{
	TargetObject.Reset();
	TargetOffset = FVector3::Zero;
	ExplicitTargetPosition = FVector3::Zero;
	bUseTargetObject = false;
	bHasExplicitTargetPosition = false;
	SyncPlaybackTarget();
}

bool CEffectComponent::TryGetTargetPosition( FVector3& OutWorldPosition ) const
{
	if ( bUseTargetObject )
	{
		// 弱参照から生存確認のために一時取得する追従先オブジェクト。
		const TSharedPtr<CGameObject> Pinned = TargetObject.Pin();
		if ( !Pinned.IsValid() || !Pinned->IsAlive() ) return false;
		// 追従先のワールド姿勢を持つ変換コンポーネント。
		const CTransformComponent* TargetTransform = Pinned->GetComponent<CTransformComponent>();
		if ( TargetTransform == nullptr ) return false;
		OutWorldPosition = TargetTransform->GetWorldTransform().TransformPoint( TargetOffset );
		return true;
	}
	if ( bHasExplicitTargetPosition )
	{
		OutWorldPosition = ExplicitTargetPosition;
		return true;
	}
	return false;
}

void CEffectComponent::SyncPlaybackTarget()
{
	if ( !Playback.IsValid() ) return;
	// 再生窓口へ渡す解決済みのワールド終点座標。
	FVector3 WorldTarget = FVector3::Zero;
	if ( TryGetTargetPosition( WorldTarget ) ) Playback->SetTargetPosition( WorldTarget );
	else Playback->ClearTargetPosition();
}

bool CEffectComponent::Play()
{
	CGameObject* Owner = GetOwner();
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Owner == nullptr || Assets == nullptr ) return false;

	Release();
	bStarted = true;   // 自動の開始は、もう済み（Play のあとの最初の Tick で、もう一度始めない。Stop で止めたあとでも、Play すれば、また動く）
	if ( Path.IsEmpty() ) return false;
	const FEffectBackendRegistry& Registry = Backends != nullptr ? *Backends : FEffectBackendRegistry::Global();
	CTransformComponent* Transform = Owner->GetComponent<CTransformComponent>();
	Playback = Registry.Play( Path, Assets, Params, Transform != nullptr ? Transform->GetWorldTransform() : FTransform() );
	if ( !Playback.IsValid() ) return false;
	SyncPlaybackTarget();

	// 描画システムが見るデータ。シーンに入っていなければ、入ってから（Tick で）付ける
	if ( Owner->IsAlive() ) Owner->GetOrAddData<FEffectPlayer>().Playback = Playback;
	return true;
}

void CEffectComponent::Stop( bool bImmediate )
{
	bAutoPlay = false;   // 止めたものを、自動で始め直さない
	if ( Playback.IsValid() ) Playback->Stop( bImmediate );
}

bool CEffectComponent::IsPlaying() const
{
	return Playback.IsValid() && !Playback->IsFinished();
}

bool CEffectComponent::IsFinished() const
{
	return bStarted && ( !Playback.IsValid() || Playback->IsFinished() );
}

void CEffectComponent::Release()
{
	Playback.Reset();
	if ( CGameObject* Owner = GetOwner() )
	{
		if ( FEffectPlayer* Player = Owner->GetData<FEffectPlayer>() ) Player->Playback.Reset();
	}
}

void CEffectComponent::OnDetach()
{
	Release();
	CComponent::OnDetach();
}

void CEffectComponent::OnOwnerSceneChanged( CScene* NewScene )
{
	if ( NewScene == nullptr ) Release();
}

void CEffectComponent::Tick( float32 DeltaTime )
{
	CGameObject* Owner = GetOwner();
	if ( Owner == nullptr ) return;

	if ( bAutoPlay && !bStarted )
	{
		bStarted = true;
		Play();
	}
	if ( !Playback.IsValid() ) return;

	// 描画システムのデータは、シーンの中でだけ付けられる。無ければここで付ける（Play がシーンの外で呼ばれた場合）
	FEffectPlayer& Player = Owner->GetOrAddData<FEffectPlayer>();
	Player.Playback = Playback;

	if ( CTransformComponent* Transform = Owner->GetComponent<CTransformComponent>() ) Playback->SetTransform( Transform->GetWorldTransform() );
	SyncPlaybackTarget();
	Playback->Update( DeltaTime );

	if ( Playback->IsFinished() )
	{
		Release();
		if ( bDestroyOwnerWhenFinished ) Owner->Destroy();
	}
}
