#include "TransformComponent.h"

#include "Library/Core/Interfaces/Inspector/IPropertyInspector.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/MathUtils.h"

DEFINE_LOG_CATEGORY_STATIC( LogTransform, Log )

namespace
{
	float32 SafeDivide( float32 A, float32 B )
	{
		return B != 0.0f ? A / B : 0.0f;
	}
}

#pragma region Binding

void CTransformComponent::BindToWorld( FTransformWorld& InWorld )
{
	if ( World != nullptr ) return;

	World = &InWorld;
	Slot = InWorld.Allocate( GetOwner(), DetachedLocal );
	Node = &InWorld.GetNode( Slot );
	if ( GetOwner()->TransformSlot == INDEX_NONE ) GetOwner()->SetTransformSlotInternal( Slot );
}

void CTransformComponent::UnbindFromWorld()
{
	if ( World == nullptr ) return;

	DetachedLocal = Node->Local;   // 外れたあとも値は残る
	CGameObject* Owner = GetOwner();
	if ( Owner != nullptr && Owner->TransformSlot == Slot )
	{
		// 同じオブジェクトに別の CTransformComponent があれば、そちらの番号に切り替える
		Owner->SetTransformSlotInternal( INDEX_NONE );
		for ( int32 i = 0; i < Owner->GetNumComponents(); ++i )
		{
			const CTransformComponent* Other = Cast<CTransformComponent>( Owner->GetComponentAt( i ) );
			if ( Other != nullptr && Other != this && Other->World != nullptr )
			{
				Owner->SetTransformSlotInternal( Other->Slot );
				break;
			}
		}
	}
	World->Free( Slot );
	World = nullptr;
	Slot = INDEX_NONE;
	Node = nullptr;
}

void CTransformComponent::OnAttach()
{
	if ( CScene* Scene = GetOwner()->GetScene() ) BindToWorld( Scene->GetTransformWorld() );
}

void CTransformComponent::OnOwnerSceneChanged( CScene* NewScene )
{
	if ( NewScene != nullptr ) BindToWorld( NewScene->GetTransformWorld() );
}

void CTransformComponent::OnDetach()
{
	UnbindFromWorld();
}

void CTransformComponent::OnDestroyRequested()
{
	// 取り外しが予約されたら、親としては数えない（以前の GetComponent が、予約済みを飛ばしていたのと同じ）
	if ( World == nullptr ) return;
	Node->bPendingDestroy = true;
	World->MarkHierarchyDirty();
}

#pragma endregion

#pragma region Local

void CTransformComponent::SetLocalTransform( const FTransform& InLocal )
{
	if ( World != nullptr ) World->SetLocal( Slot, InLocal );
	else DetachedLocal = InLocal;
}

void CTransformComponent::SetLocalPosition( const FVector3& Position )
{
	if ( World == nullptr )
	{
		DetachedLocal.Position = Position;
		return;
	}
	Node->Local.Position = Position;
	World->MarkDirty( Slot );
}

void CTransformComponent::SetLocalRotation( const FQuaternion& Rotation )
{
	if ( World == nullptr )
	{
		DetachedLocal.Rotation = Rotation;
		return;
	}
	Node->Local.Rotation = Rotation;
	World->MarkDirty( Slot );
}

void CTransformComponent::SetLocalScale( const FVector3& Scale )
{
	if ( World == nullptr )
	{
		DetachedLocal.Scale = Scale;
		return;
	}
	Node->Local.Scale = Scale;
	World->MarkDirty( Slot );
}

#pragma endregion

#pragma region World

const CTransformComponent* CTransformComponent::FindParentTransform() const
{
	if ( GetOwner() == nullptr ) return nullptr;

	for ( const CGameObject* Ancestor = GetOwner()->GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent() )
	{
		if ( const CTransformComponent* Transform = Ancestor->GetComponent<CTransformComponent>() ) return Transform;
	}
	return nullptr;
}

FTransform CTransformComponent::GetWorldTransform() const
{
	// シーンの中なら、常に最新のワールド値（古ければここで計算される）。シーンに入る前は親を持てないので、ローカルがそのままワールド
	return World != nullptr ? World->GetWorld( Slot ) : DetachedLocal;
}

void CTransformComponent::SetWorldPosition( const FVector3& WorldPosition )
{
	const CTransformComponent* ParentTransform = FindParentTransform();
	if ( ParentTransform == nullptr )
	{
		SetLocalPosition( WorldPosition );
		return;
	}

	const FTransform Parent = ParentTransform->GetWorldTransform();
	if ( Parent.Scale.x == 0.0f || Parent.Scale.y == 0.0f || Parent.Scale.z == 0.0f )
	{
		LOG( LogTransform, Warning, "SetWorldPosition: the parent scale has a zero component (%s). That axis of the local position is set to 0.",
			 *Parent.Scale.ToString() );
	}
	const FVector3 Unrotated = Parent.Rotation.UnrotateVector( WorldPosition - Parent.Position );
	SetLocalPosition( FVector3( SafeDivide( Unrotated.x, Parent.Scale.x ),
								SafeDivide( Unrotated.y, Parent.Scale.y ),
								SafeDivide( Unrotated.z, Parent.Scale.z ) ) );
}

#pragma endregion

void CTransformComponent::InspectProperties( IPropertyInspector& Inspector )
{
	FTransform Edited = GetLocalTransform();
	bool bChanged = false;

	bChanged |= Inspector.Vector3( "Position", Edited.Position );

	// 回転は度数法のオイラー角で編集する
	const FVector3 EulerRad = Edited.Rotation.ToEuler();
	FVector3 EulerDeg( NMath::ToDegrees( EulerRad.x ), NMath::ToDegrees( EulerRad.y ), NMath::ToDegrees( EulerRad.z ) );
	if ( Inspector.Vector3( "Rotation", EulerDeg ) )
	{
		Edited.Rotation = FQuaternion::EulerToQuat( FVector3( NMath::ToRadians( EulerDeg.x ), NMath::ToRadians( EulerDeg.y ), NMath::ToRadians( EulerDeg.z ) ) );
		bChanged = true;
	}

	bChanged |= Inspector.Vector3( "Scale", Edited.Scale );

	// 値を書き換えたら、ワールド値が古くなるので、必ず Set を通す
	if ( bChanged ) SetLocalTransform( Edited );

	if ( FindParentTransform() != nullptr )
	{
		Inspector.Text( "World Position", GetWorldPosition().ToString() );
	}
}
