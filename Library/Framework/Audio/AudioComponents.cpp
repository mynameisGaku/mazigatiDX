#include "AudioComponents.h"

#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Object/GameObject.h"

//=============================================================================
// CAudioListenerComponent
//=============================================================================
void CAudioListenerComponent::Tick( float32 DeltaTime )
{
	(void)DeltaTime;
	CGameObject* Owner = GetOwner();
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	CTransformComponent* Transform = Owner != nullptr ? Owner->GetComponent<CTransformComponent>() : nullptr;
	if ( Audio == nullptr || Transform == nullptr )
	{
		return;
	}

	const FTransform World = Transform->GetWorldTransform();
	Audio->SetListener( World.Position, World.Rotation.GetForward(), World.Rotation.GetUp() );
}

//=============================================================================
// CAudioSourceComponent
//=============================================================================
void CAudioSourceComponent::Play()
{
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	CGameObject* Owner = GetOwner();
	if ( Audio == nullptr || !Sound.IsValid() || IsPlaying() )
	{
		return;
	}

	CTransformComponent* Transform = Owner != nullptr ? Owner->GetComponent<CTransformComponent>() : nullptr;
	FSoundPlayParams PlayParams = Params;
	PlayParams.bSpatial = true;
	PlayParams.Position = Transform != nullptr ? Transform->GetWorldPosition() : FVector3::Zero;
	Handle = Audio->Play( Sound, PlayParams );
}

void CAudioSourceComponent::Stop( float32 FadeOut )
{
	if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
	{
		Audio->Stop( Handle, FadeOut );
	}
	Handle = FSoundHandle();
	bAutoPlay = false;   // 止めたものを、自動で鳴らし直さない
}

bool CAudioSourceComponent::IsPlaying() const
{
	const CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	return Audio != nullptr && Audio->IsPlaying( Handle );
}

void CAudioSourceComponent::OnAttach()
{
	// 位置はシーンに入ってから決まるので、自動再生は最初の Tick で行う
}

void CAudioSourceComponent::OnOwnerSceneChanged( CScene* NewScene )
{
	if ( NewScene == nullptr )
	{
		Stop();
	}
}

void CAudioSourceComponent::OnDetach()
{
	if ( CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>() )
	{
		Audio->Stop( Handle, 0.0f );
	}
	Handle = FSoundHandle();
}

void CAudioSourceComponent::Tick( float32 DeltaTime )
{
	(void)DeltaTime;
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Audio == nullptr )
	{
		return;
	}

	if ( bAutoPlay && !bStarted )
	{
		bStarted = true;
		Play();
	}

	// 鳴っている間、オブジェクトの位置に追従する
	if ( Handle.IsValid() )
	{
		CGameObject* Owner = GetOwner();
		if ( CTransformComponent* Transform = Owner != nullptr ? Owner->GetComponent<CTransformComponent>() : nullptr )
		{
			Audio->SetPosition( Handle, Transform->GetWorldPosition() );
		}
	}
}
