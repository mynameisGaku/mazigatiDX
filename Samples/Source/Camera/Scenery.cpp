#include "Scenery.h"

#include "Source/Common/SoftLighting.h"

namespace
{
	// 輪に並べる物の数と、輪の半径
	constexpr int32 RingCount = 10;
	constexpr float32 RingRadius = 9.0f;

	// 地面の格子の範囲と間隔（ワールドの単位）
	constexpr int32 GridHalfCount = 20;
	constexpr float32 GridStep = 2.0f;

	// 球の分割数
	constexpr int32 SphereDivisions = 24;

	FDemoObject MakeObject( const char* Name, EDemoShape Shape, const FVector3& Position, float32 Radius, const FColor32& Color )
	{
		FDemoObject Object;
		Object.Name = Name;
		Object.Shape = Shape;
		Object.Position = Position;
		Object.Radius = Radius;
		Object.Color = Color;
		return Object;
	}

	// 物を 1 つ描く
	void RecordObject( FDrawContext& CommandList, const FDemoObject& Object )
	{
		const FColor32 Specular( 90, 90, 90 );
		switch ( Object.Shape )
		{
		case EDemoShape::Sphere:
			CommandList.Sphere3D( Object.Position, Object.Radius, SphereDivisions, Object.Color, Specular );
			break;
		case EDemoShape::Cube:
		{
			const FVector3 Half( Object.Radius, Object.Radius, Object.Radius );
			CommandList.Cube3D( Object.Position - Half, Object.Position + Half, Object.Color, Specular );
			break;
		}
		case EDemoShape::Capsule:
		{
			const FVector3 Axis( 0.0f, Object.Radius * 1.2f, 0.0f );
			CommandList.Capsule3D( Object.Position - Axis, Object.Position + Axis, Object.Radius, SphereDivisions, Object.Color, Specular );
			break;
		}
		}
	}
}

TArray<FDemoObject> NScenery::MakeObjects()
{
	TArray<FDemoObject> Objects;
	const FColor32 Colors[RingCount] = {
		FColor32( 230, 60, 60 ), FColor32( 240, 150, 50 ), FColor32( 240, 220, 60 ), FColor32( 70, 200, 90 ), FColor32( 50, 190, 190 ),
		FColor32( 70, 110, 230 ), FColor32( 150, 90, 220 ), FColor32( 230, 110, 180 ), FColor32( 235, 235, 235 ), FColor32( 150, 150, 160 ),
	};
	const char* const Names[RingCount] = { "Red sphere", "Orange cube", "Yellow capsule", "Green sphere", "Teal cube", "Blue capsule", "Purple sphere", "Pink cube", "White capsule", "Gray sphere" };
	const EDemoShape Shapes[3] = { EDemoShape::Sphere, EDemoShape::Cube, EDemoShape::Capsule };
	const float32 Radii[RingCount] = { 1.0f, 1.0f, 0.7f, 1.2f, 0.9f, 0.8f, 1.0f, 1.1f, 0.7f, 0.9f };
	for ( int32 Index = 0; Index < RingCount; ++Index )
	{
		const float32 Angle = static_cast<float32>( Index ) / static_cast<float32>( RingCount ) * NMath::PI * 2.0f;
		const EDemoShape Shape = Shapes[Index % 3];
		// 地面に載せる（カプセルは、軸の分だけ高く）
		const float32 Height = Shape == EDemoShape::Capsule ? Radii[Index] * 2.2f : Radii[Index];
		Objects.Add( MakeObject( Names[Index], Shape, FVector3( NMath::Sin( Angle ) * RingRadius, Height, NMath::Cos( Angle ) * RingRadius ), Radii[Index], Colors[Index] ) );
	}
	Objects.Add( MakeObject( "Pillar", EDemoShape::Capsule, FVector3( 0.0f, 3.4f, 0.0f ), 1.0f, FColor32( 220, 190, 120 ) ) );

	// 遠くの物（カメラが回ると、視錐台に入ったり、出たりする）
	Objects.Add( MakeObject( "Far east", EDemoShape::Sphere, FVector3( 32.0f, 1.5f, 2.0f ), 1.5f, FColor32( 120, 240, 255 ) ) );
	Objects.Add( MakeObject( "Far west", EDemoShape::Sphere, FVector3( -32.0f, 1.5f, -4.0f ), 1.5f, FColor32( 255, 200, 120 ) ) );
	Objects.Add( MakeObject( "Far north", EDemoShape::Sphere, FVector3( 4.0f, 1.5f, 34.0f ), 1.5f, FColor32( 200, 255, 140 ) ) );
	return Objects;
}

void NScenery::RecordGround( FDrawContext& CommandList )
{
	const float32 Extent = static_cast<float32>( GridHalfCount ) * GridStep;
	const FColor32 Grid( 74, 84, 104 );
	for ( int32 Index = -GridHalfCount; Index <= GridHalfCount; ++Index )
	{
		if ( Index == 0 )
		{
			continue;
		}
		const float32 Position = static_cast<float32>( Index ) * GridStep;
		CommandList.Line3D( FVector3( -Extent, 0.0f, Position ), FVector3( Extent, 0.0f, Position ), Grid );
		CommandList.Line3D( FVector3( Position, 0.0f, -Extent ), FVector3( Position, 0.0f, Extent ), Grid );
	}
	// 軸（X は赤、Z は青）
	CommandList.Line3D( FVector3( -Extent, 0.0f, 0.0f ), FVector3( Extent, 0.0f, 0.0f ), FColor32( 200, 70, 70 ) );
	CommandList.Line3D( FVector3( 0.0f, 0.0f, -Extent ), FVector3( 0.0f, 0.0f, Extent ), FColor32( 70, 100, 220 ) );
}

int32 NScenery::RecordObjects( FDrawContext& CommandList, const TArray<FDemoObject>& Objects, const FFrustum* Cull, int32 Highlighted )
{
	// 光が当たらない裏側も、真っ黒にならない、やわらかい光
	FSoftLightingScope SoftLighting( CommandList );
	int32 NumDrawn = 0;
	for ( int32 Index = 0; Index < Objects.Num(); ++Index )
	{
		const FDemoObject& Object = Objects[Index];
		if ( Cull != nullptr && !Cull->IntersectsSphere( Object.Position, Object.GetBoundingRadius() ) )
		{
			continue;
		}
		RecordObject( CommandList, Object );
		++NumDrawn;
		if ( Index == Highlighted )
		{
			// 指している物の周りに、ワイヤーの球
			CommandList.Sphere3D( Object.Position, Object.GetBoundingRadius() * 1.1f, 16, FColor32( 255, 255, 255 ), FColor32::Black, false );
		}
	}
	return NumDrawn;
}
