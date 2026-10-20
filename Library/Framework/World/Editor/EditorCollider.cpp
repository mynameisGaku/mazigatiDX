#include "EditorCollider.h"

#include <algorithm>

#include "Library/Framework/World/WorldCollision.h"

EEditorColliderDisplay NWorldEditor::ClassifyColliderDisplay( const FWorldSettings& Settings, const FWorldObject& Object )
{
	// Collider が無ければ、ResolveCollision が「付かない」と答える（そのあとは、Collider がある場合だけ）
	const FWorldCollisionDecision Decision = NWorld::ResolveCollision( Settings, Object );
	const bool bForced = Decision.Reason == EWorldCollisionReason::ObjectOverride;
	if ( !Decision.bEnabled ) return bForced ? EEditorColliderDisplay::OffForced : EEditorColliderDisplay::Off;
	if ( Object.Collider.GetValue().bTrigger ) return EEditorColliderDisplay::Trigger;
	return bForced ? EEditorColliderDisplay::SolidForced : EEditorColliderDisplay::Solid;
}

const char* NWorldEditor::GetColliderDisplayLabel( EEditorColliderDisplay Display )
{
	switch ( Display )
	{
	case EEditorColliderDisplay::Solid: return "collides";
	case EEditorColliderDisplay::SolidForced: return "collides (set on the object)";
	case EEditorColliderDisplay::Trigger: return "trigger";
	case EEditorColliderDisplay::Off: return "collision off";
	case EEditorColliderDisplay::OffForced: return "collision off (set on the object)";
	}
	return "";
}

FWorldCollider NWorldEditor::FitColliderToBounds( EWorldColliderShape Shape, const FVector3& Min, const FVector3& Max, const FWorldCollider& Base )
{
	constexpr float32 MinSize = 1.0e-3f;
	const FVector3 Size = FVector3::Max( Max - Min, FVector3( MinSize, MinSize, MinSize ) );
	const FVector3 Center = ( Min + Max ) * 0.5f;

	FWorldCollider Result = Base;
	Result.Shape = Shape;
	switch ( Shape )
	{
	case EWorldColliderShape::Box:
		Result.Size = Size;
		Result.Offset = Center;
		break;
	case EWorldColliderShape::Sphere:
		Result.Radius = std::max( { Size.x, Size.y, Size.z } ) * 0.5f;
		Result.Offset = Center;
		break;
	case EWorldColliderShape::Capsule:
		Result.Radius = std::max( Size.x, Size.z ) * 0.5f;
		Result.Height = std::max( Size.y, Result.Radius * 2.0f );
		Result.Offset = Center;
		break;
	case EWorldColliderShape::Mesh:
		break;
	}
	return Result;
}

bool NWorldEditor::FitColliderToModel( TWorldDocument& Document, int32 ObjectId, EWorldColliderShape Shape, const IEditorAssetInfo& Assets )
{
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr ) return false;
	const FWorldCollider Base = Object->Collider.IsSet() ? Object->Collider.GetValue() : FWorldCollider();

	FWorldCollider Fitted;
	if ( Shape == EWorldColliderShape::Mesh )
	{
		// 三角形は、実行時に、モデルから取る。Model（または Mesh のファイル）が要る
		Fitted = Base;
		Fitted.Shape = EWorldColliderShape::Mesh;
		FWorldObject WithMesh = *Object;
		WithMesh.Collider = Fitted;
		if ( NWorld::GetColliderMeshPath( WithMesh ).IsEmpty() && !NWorld::UsesEditMeshCollider( WithMesh ) ) return false;
	}
	else
	{
		// 大きさの元は、三角形の元と同じ順（Collider の Mesh のファイル > Mesh 部品 > Model のファイル）
		FVector3 Min, Max;
		bool bKnown = false;
		if ( !Base.Mesh.IsEmpty() ) bKnown = Assets.GetModelBounds( Base.Mesh, Min, Max );
		else if ( Object->Mesh.IsSet() ) bKnown = Object->Mesh.GetValue().GetBounds( Min, Max );
		else if ( Object->Model.IsSet() && !Object->Model.GetValue().Path.IsEmpty() ) bKnown = Assets.GetModelBounds( Object->Model.GetValue().Path, Min, Max );
		if ( !bKnown ) return false;
		Fitted = FitColliderToBounds( Shape, Min, Max, Base );
		Fitted.Mesh = FString();
	}
	NWorldEdit::EditObject( Document, ObjectId )->Collider = Fitted;
	return true;
}
