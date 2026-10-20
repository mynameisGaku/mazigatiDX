#include "WorldCollision.h"

#include "Library/Framework/World/WorldHierarchy.h"

FString FWorldCollisionDecision::ToDisplayString() const
{
	const char* State = bEnabled ? "Collision on" : "Collision off";
	switch ( Reason )
	{
	case EWorldCollisionReason::NoCollider: return FString( State ) + " (no collider shape)";
	case EWorldCollisionReason::ObjectOverride: return FString( State ) + " (set on the object: " + Detail + ")";
	case EWorldCollisionReason::TagRule: return FString( State ) + " (tag rule '" + Detail + "')";
	case EWorldCollisionReason::Default: return FString( State ) + " (default)";
	}
	return FString( State );
}

FWorldCollisionDecision NWorld::ResolveCollision( const FWorldSettings& Settings, const FWorldObject& Object )
{
	FWorldCollisionDecision Decision;
	if ( !Object.Collider.IsSet() )
	{
		Decision.bEnabled = false;
		Decision.Reason = EWorldCollisionReason::NoCollider;
		return Decision;
	}

	if ( Object.Collision != EWorldCollisionMode::Auto )
	{
		Decision.bEnabled = Object.Collision == EWorldCollisionMode::On;
		Decision.Reason = EWorldCollisionReason::ObjectOverride;
		Decision.Detail = ToString( Object.Collision );
		return Decision;
	}

	for ( const FWorldCollisionRule& Rule : Settings.CollisionRules )
	{
		if ( Object.HasTag( Rule.Tag ) )
		{
			Decision.bEnabled = Rule.bCollide;
			Decision.Reason = EWorldCollisionReason::TagRule;
			Decision.Detail = Rule.Tag;
			return Decision;
		}
	}

	Decision.bEnabled = true;
	Decision.Reason = EWorldCollisionReason::Default;
	return Decision;
}

FString NWorld::GetColliderMeshPath( const FWorldObject& Object )
{
	if ( !Object.Collider.IsSet() ) return FString();
	if ( !Object.Collider.GetValue().Mesh.IsEmpty() ) return Object.Collider.GetValue().Mesh;
	if ( Object.Mesh.IsSet() ) return FString();
	return Object.Model.IsSet() ? Object.Model.GetValue().Path : FString();
}

bool NWorld::UsesEditMeshCollider( const FWorldObject& Object )
{
	return Object.Collider.IsSet() && Object.Collider.GetValue().Shape == EWorldColliderShape::Mesh && Object.Collider.GetValue().Mesh.IsEmpty() && Object.Mesh.IsSet();
}

FWorldCollisionSummary NWorld::SummarizeCollision( const FWorldDefinition& World )
{
	FWorldCollisionSummary Summary;
	Summary.NumDecidedByRule.Init( 0, World.Settings.CollisionRules.Num() );
	for ( const int32 Index : ComputeSpawnOrder( World, true ).Order )
	{
		const FWorldObject& Object = World.Objects[Index];
		++Summary.NumPlaced;
		if ( !Object.Collider.IsSet() ) continue;
		++Summary.NumWithCollider;
		const FWorldCollisionDecision Decision = ResolveCollision( World.Settings, Object );
		switch ( Decision.Reason )
		{
		case EWorldCollisionReason::ObjectOverride: ++Summary.NumByOverride; break;
		case EWorldCollisionReason::TagRule:
		{
			++Summary.NumByRule;
			// 決めた規則は、最初に一致したもの（ResolveCollision と同じ）
			for ( int32 i = 0; i < World.Settings.CollisionRules.Num(); ++i )
			{
				if ( World.Settings.CollisionRules[i].Tag == Decision.Detail )
				{
					++Summary.NumDecidedByRule[i];
					break;
				}
			}
			break;
		}
		case EWorldCollisionReason::Default: ++Summary.NumByDefault; break;
		case EWorldCollisionReason::NoCollider: break;
		}
		if ( !Decision.bEnabled )
		{
			++Summary.NumDisabled;
			continue;
		}
		++Summary.NumEnabled;
		if ( Object.Collider.GetValue().bTrigger ) ++Summary.NumTriggers;
		if ( Object.Collider.GetValue().Shape == EWorldColliderShape::Mesh ) ++Summary.NumMeshes;
	}
	return Summary;
}
