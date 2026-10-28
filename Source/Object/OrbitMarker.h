#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class CTransformComponent;

//=============================================================================
// COrbitMarker
//   CTransformComponent を持ち、Z 軸まわりに回転する四角形。
//   子として付けた COrbitMarker は親の回転に合わせて周回する（親子関係のサンプル）。
//
//   auto Parent = Scene.SpawnObject<COrbitMarker>( FVector3( 6, 8, 0 ), 90.0f, 12, FColor32::White );
//   auto Child  = Scene.SpawnObject<COrbitMarker>( FVector3( 2, 0, 0 ), 0.0f, 6, FColor32::Yellow );
//   Child->AttachToParent( Parent.Get() );
//=============================================================================
class COrbitMarker : public CGameObject
{
public:
	COrbitMarker( const FVector3& InLocalPosition, float32 InSpinDegreesPerSecond, int32 InHalfSizePixels, const FColor32& InColor );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;
	void InspectProperties( IPropertyInspector& Inspector ) override;

private:
	FVector3 InitialLocalPosition;
	float32 SpinDegreesPerSecond;
	int32 HalfSizePixels;
	FColor32 Color;

	CTransformComponent* Transform = nullptr;
};
