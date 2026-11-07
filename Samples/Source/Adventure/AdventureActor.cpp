#include "AdventureActor.h"

#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/World/Runtime/WorldSpawner.h"

FVector3 CAdventureActor::GetPlaceholderPosition() const
{
	const CGameObject* Parent = GetParent();
	const CTransformComponent* Transform = Parent != nullptr ? Parent->GetComponent<CTransformComponent>() : nullptr;
	return Transform != nullptr ? Transform->GetWorldTransform().Position : FVector3::Zero;
}

int32 CAdventureActor::GetPlaceholderId() const
{
	const CGameObject* Parent = GetParent();
	const FWorldObjectRef* Ref = Parent != nullptr ? const_cast<CGameObject*>( Parent )->GetData<FWorldObjectRef>() : nullptr;
	return Ref != nullptr ? Ref->ObjectId : 0;
}
