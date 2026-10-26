#include "EditorShapes.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Set.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldHierarchy.h"

namespace
{
	constexpr float32 MinBillboardSize = 1.0e-4f;

	FVector3 Abs3( const FVector3& V ) { return FVector3( NMath::Abs( V.x ), NMath::Abs( V.y ), NMath::Abs( V.z ) ); }

	float32 GetMarkerRadius( const FEditorShapeContext& Context, const FVector3& Position )
	{
		if ( Context.Camera == nullptr || !Context.Viewport.IsValid() ) return 0.25f;
		return Context.Camera->GetPixelSize( Context.Viewport, Position ) * Context.MarkerPixels;
	}

	// ローカルの箱（Min..Max）の図形。bKnown が false なら、1 m の箱の見積もり（bApproximate）
	FEditorShape MakeLocalBox( EEditorShapeRole Role, const FVector3& Min, const FVector3& Max, bool bKnown, const FTransform& WorldTransform )
	{
		FEditorShape Shape;
		Shape.Kind = EEditorShapeKind::Box;
		Shape.Role = Role;
		Shape.Center = WorldTransform.TransformPoint( ( Min + Max ) * 0.5f );
		Shape.Rotation = WorldTransform.Rotation;
		Shape.Half = ( Max - Min ) * 0.5f * Abs3( WorldTransform.Scale );
		Shape.bApproximate = !bKnown;
		return Shape;
	}

	// 入れ子のワールドを何段めまで調べているか（循環していても、止まるように）
	thread_local int32 PrefabDepth = 0;

	// 入れ子のワールド（Prefab 部品）の中身を囲む箱（入れ子のワールドの座標）。読み込めない・中身が無い・深すぎるなら false
	bool ComputePrefabBounds( const FString& Path, const FEditorShapeContext& Context, FVector3& OutMin, FVector3& OutMax )
	{
		if ( Context.Assets == nullptr || Path.IsEmpty() || PrefabDepth >= WorldMaxPrefabDepth ) return false;
		const FWorldDefinition* Nested = Context.Assets->GetWorld( Path );
		if ( Nested == nullptr || Nested->Objects.IsEmpty() ) return false;
		++PrefabDepth;
		const TArray<FTransform> Transforms = NWorld::ComputeAllWorldTransforms( *Nested );
		TArray<FEditorShape> Shapes;
		FVector3 Min( NMath::MaxFloat, NMath::MaxFloat, NMath::MaxFloat );
		FVector3 Max( -NMath::MaxFloat, -NMath::MaxFloat, -NMath::MaxFloat );
		bool bAny = false;
		for ( int32 i = 0; i < Nested->Objects.Num() && i < Transforms.Num(); ++i )
		{
			const FWorldObject& Object = Nested->Objects[i];
			if ( !Object.bEnabled ) continue;
			Min = FVector3::Min( Min, Transforms[i].Position );   // 目印（空のオブジェクト・音）は、位置だけを含める
			Max = FVector3::Max( Max, Transforms[i].Position );
			bAny = true;
			for ( const FEditorShape& Shape : NWorldEditor::GatherShapes( Object, Transforms[i], Context ) )
			{
				if ( Shape.Role != EEditorShapeRole::Marker && Shape.Role != EEditorShapeRole::Sound && Shape.Role != EEditorShapeRole::Effect ) Shapes.Add( Shape );
			}
		}
		--PrefabDepth;
		FVector3 ShapeMin, ShapeMax;
		if ( NWorldEditor::ComputeShapesBounds( Shapes, ShapeMin, ShapeMax ) )
		{
			Min = FVector3::Min( Min, ShapeMin );
			Max = FVector3::Max( Max, ShapeMax );
		}
		if ( !bAny ) return false;
		OutMin = Min;
		OutMax = Max;
		return true;
	}

	// 編集メッシュの大きさ（箱）の図形。面がなければ、1 m の箱
	FEditorShape MakeEditMeshBox( EEditorShapeRole Role, const FEditMesh& Mesh, const FTransform& WorldTransform )
	{
		FVector3 Min( -0.5f, 0.0f, -0.5f );
		FVector3 Max( 0.5f, 1.0f, 0.5f );
		const bool bKnown = Mesh.GetBounds( Min, Max );
		return MakeLocalBox( Role, Min, Max, bKnown, WorldTransform );
	}

	// モデルの大きさ（箱）の図形。大きさが分からなければ、1 m の箱（bApproximate）
	FEditorShape MakeBoundsBox( EEditorShapeRole Role, const FString& ModelPath, const FTransform& WorldTransform, const FEditorShapeContext& Context )
	{
		FVector3 Min( -0.5f, 0.0f, -0.5f );
		FVector3 Max( 0.5f, 1.0f, 0.5f );
		bool bKnown = false;
		if ( Context.Assets != nullptr && !ModelPath.IsEmpty() )
		{
			FVector3 AssetMin, AssetMax;
			if ( Context.Assets->GetModelBounds( ModelPath, AssetMin, AssetMax ) )
			{
				Min = AssetMin;
				Max = AssetMax;
				bKnown = true;
			}
		}
		FEditorShape Shape;
		Shape.Kind = EEditorShapeKind::Box;
		Shape.Role = Role;
		Shape.Center = WorldTransform.TransformPoint( ( Min + Max ) * 0.5f );
		Shape.Rotation = WorldTransform.Rotation;
		Shape.Half = ( Max - Min ) * 0.5f * Abs3( WorldTransform.Scale );
		Shape.bApproximate = !bKnown;
		return Shape;
	}

	FEditorShape MakeMarker( EEditorShapeRole Role, const FVector3& Position, const FEditorShapeContext& Context )
	{
		FEditorShape Shape;
		Shape.Kind = EEditorShapeKind::Sphere;
		Shape.Role = Role;
		Shape.Center = Position;
		Shape.Radius = GetMarkerRadius( Context, Position );
		return Shape;
	}
}

void NWorldEditor::ComputeBillboardAxes( EWorldBillboardMode Mode, const FTransform& WorldTransform, const FVector3& CameraEye, const FVector3& CameraRight, const FVector3& CameraUp,
										 FVector3& OutRight, FVector3& OutUp )
{
	switch ( Mode )
	{
	case EWorldBillboardMode::World:
		OutRight = WorldTransform.Rotation.Rotate( FVector3::Right );
		OutUp = WorldTransform.Rotation.Rotate( FVector3::Up );
		break;
	case EWorldBillboardMode::CameraY:
	{
		// 水平だけで、カメラの方を向く。真上・真下から見ているときは、カメラの横向きを使う
		FVector3 Forward( WorldTransform.Position.x - CameraEye.x, 0.0f, WorldTransform.Position.z - CameraEye.z );
		OutUp = FVector3::Up;
		if ( Forward.LengthSquared() < 1.0e-8f ) OutRight = CameraRight;
		else OutRight = FVector3::Cross( OutUp, Forward.Normalized() );
		break;
	}
	case EWorldBillboardMode::Camera:
	default:
	{
		// オブジェクトの Roll を、視線まわりの回転にする（FBillboard::Record と、実行側（DrawQuad3D）と同じ）
		const float32 Angle = WorldTransform.Rotation.ToEuler().z;
		const float32 Cos = NMath::Cos( Angle );
		const float32 Sin = NMath::Sin( Angle );
		OutRight = CameraRight * Cos + CameraUp * Sin;
		OutUp = CameraUp * Cos + CameraRight * -Sin;
		break;
	}
	}
}

FEditorShape NWorldEditor::MakeColliderShape( const FWorldCollider& Collider, const FTransform& WorldTransform )
{
	const FVector3 Scale = Abs3( WorldTransform.Scale );
	const float32 MaxScale = NMath::Max( Scale.x, Scale.y, Scale.z );

	FEditorShape Shape;
	Shape.Role = EEditorShapeRole::Collider;
	Shape.Center = WorldTransform.TransformPoint( Collider.Offset );
	Shape.Rotation = WorldTransform.Rotation;
	switch ( Collider.Shape )
	{
	case EWorldColliderShape::Box:
		Shape.Kind = EEditorShapeKind::Box;
		Shape.Half = Collider.Size * 0.5f * Scale;
		break;
	case EWorldColliderShape::Sphere:
		Shape.Kind = EEditorShapeKind::Sphere;
		Shape.Radius = Collider.Radius * MaxScale;
		break;
	case EWorldColliderShape::Mesh:
		// 三角形（モデル）の大きさが要る。大きさは GatherShapes が、モデルの箱から作る。ここは、アセットを知らないときの、1 m の箱
		Shape.Kind = EEditorShapeKind::Box;
		Shape.Center = WorldTransform.TransformPoint( FVector3( 0.0f, 0.5f, 0.0f ) );
		Shape.Half = FVector3( 0.5f, 0.5f, 0.5f ) * Scale;
		Shape.bApproximate = true;
		break;
	case EWorldColliderShape::Capsule:
	{
		Shape.Kind = EEditorShapeKind::Capsule;
		const float32 HalfCylinder = NMath::Max( ( Collider.Height - Collider.Radius * 2.0f ) * 0.5f, 1.0e-3f ) * Scale.y;
		const FVector3 Axis = WorldTransform.Rotation.Rotate( FVector3::Up );
		Shape.A = Shape.Center - Axis * HalfCylinder;
		Shape.B = Shape.Center + Axis * HalfCylinder;
		Shape.Radius = Collider.Radius * NMath::Max( Scale.x, Scale.z );
		break;
	}
	}
	return Shape;
}

TArray<FEditorShape> NWorldEditor::GatherShapes( const FWorldObject& Object, const FTransform& WorldTransform, const FEditorShapeContext& Context )
{
	TArray<FEditorShape> Shapes;

	if ( Object.Model.IsSet() ) Shapes.Add( MakeBoundsBox( EEditorShapeRole::Model, Object.Model.GetValue().Path, WorldTransform, Context ) );
	if ( Object.Mesh.IsSet() ) Shapes.Add( MakeEditMeshBox( EEditorShapeRole::Model, Object.Mesh.GetValue(), WorldTransform ) );
	if ( Object.Prefab.IsSet() )
	{
		// 入れ子のワールド: 中身を囲む箱。中身が分からなければ、1 m の箱（bApproximate）
		FVector3 Min( -0.5f, 0.0f, -0.5f );
		FVector3 Max( 0.5f, 1.0f, 0.5f );
		const bool bKnown = ComputePrefabBounds( Object.Prefab.GetValue().Path, Context, Min, Max );
		Shapes.Add( MakeLocalBox( EEditorShapeRole::Model, Min, Max, bKnown, WorldTransform ) );
	}

	if ( Object.Billboard.IsSet() )
	{
		const FWorldBillboard& Billboard = Object.Billboard.GetValue();
		const float32 Width = Billboard.Size.x * NMath::Abs( WorldTransform.Scale.x );
		const float32 Height = Billboard.Size.y * NMath::Abs( WorldTransform.Scale.y );
		if ( Width > MinBillboardSize && Height > MinBillboardSize )
		{
			FVector3 Right = WorldTransform.Rotation.Rotate( FVector3::Right );
			FVector3 Up = WorldTransform.Rotation.Rotate( FVector3::Up );
			if ( Context.Camera != nullptr )
			{
				ComputeBillboardAxes( Billboard.Mode, WorldTransform, Context.Camera->GetEye(), Context.Camera->GetRight(), Context.Camera->GetUp(), Right, Up );
			}
			FEditorShape Shape;
			Shape.Kind = EEditorShapeKind::Quad;
			Shape.Role = EEditorShapeRole::Billboard;
			// 基準点が、オブジェクトの位置に来る（実行側と同じ）
			Shape.Center = WorldTransform.Position + Right * ( ( 0.5f - Billboard.Pivot.x ) * Width ) + Up * ( ( Billboard.Pivot.y - 0.5f ) * Height );
			Shape.HalfRight = Right * ( Width * 0.5f );
			Shape.HalfUp = Up * ( Height * 0.5f );
			Shapes.Add( Shape );
		}
	}

	if ( Object.Sound.IsSet() ) Shapes.Add( MakeMarker( EEditorShapeRole::Sound, WorldTransform.Position, Context ) );
	if ( Object.Effect.IsSet() ) Shapes.Add( MakeMarker( EEditorShapeRole::Effect, WorldTransform.Position, Context ) );

	if ( Object.Collider.IsSet() )
	{
		// メッシュの当たり判定は、三角形を取るモデルの箱で、選ぶ・囲む（三角形の枠線は、AppendMeshWireLines で別に出す）
		if ( NWorld::UsesEditMeshCollider( Object ) ) Shapes.Add( MakeEditMeshBox( EEditorShapeRole::Collider, Object.Mesh.GetValue(), WorldTransform ) );
		else if ( Object.Collider.GetValue().Shape == EWorldColliderShape::Mesh ) Shapes.Add( MakeBoundsBox( EEditorShapeRole::Collider, NWorld::GetColliderMeshPath( Object ), WorldTransform, Context ) );
		else Shapes.Add( MakeColliderShape( Object.Collider.GetValue(), WorldTransform ) );
	}

	// 見えるものが何も無いオブジェクトは、目印で選べるようにする
	if ( Shapes.IsEmpty() ) Shapes.Add( MakeMarker( EEditorShapeRole::Marker, WorldTransform.Position, Context ) );
	return Shapes;
}

namespace
{
	// 軸に垂直な、互いに直交する 2 つの単位ベクトル
	void MakePerpendicular( const FVector3& Axis, FVector3& OutU, FVector3& OutV )
	{
		const FVector3 Helper = NMath::Abs( Axis.y ) < 0.9f ? FVector3::Up : FVector3::Right;
		OutU = FVector3::Cross( Helper, Axis ).Normalized();
		OutV = FVector3::Cross( Axis, OutU ).Normalized();
	}

	// Center のまわりの、U と V の平面の円弧（開始角から終了角まで）
	void AppendArc( TArray<FVector3>& OutLines, const FVector3& Center, const FVector3& U, const FVector3& V, float32 Radius, float32 StartAngle, float32 EndAngle, int32 Segments )
	{
		FVector3 Previous = Center + ( U * NMath::Cos( StartAngle ) + V * NMath::Sin( StartAngle ) ) * Radius;
		for ( int32 i = 1; i <= Segments; ++i )
		{
			const float32 Angle = StartAngle + ( EndAngle - StartAngle ) * static_cast<float32>( i ) / static_cast<float32>( Segments );
			const FVector3 Point = Center + ( U * NMath::Cos( Angle ) + V * NMath::Sin( Angle ) ) * Radius;
			OutLines.Add( Previous );
			OutLines.Add( Point );
			Previous = Point;
		}
	}
}

void NWorldEditor::AppendShapeWireLines( const FEditorShape& Shape, int32 CircleSegments, TArray<FVector3>& OutLines )
{
	const int32 Segments = NMath::Max( CircleSegments, 4 );
	switch ( Shape.Kind )
	{
	case EEditorShapeKind::Box:
	{
		FVector3 Corners[8];
		for ( int32 Corner = 0; Corner < 8; ++Corner )
		{
			const FVector3 Local( ( Corner & 1 ) ? Shape.Half.x : -Shape.Half.x, ( Corner & 2 ) ? Shape.Half.y : -Shape.Half.y, ( Corner & 4 ) ? Shape.Half.z : -Shape.Half.z );
			Corners[Corner] = Shape.Center + Shape.Rotation.Rotate( Local );
		}
		// 角の番号の 1 ビットだけ違う 2 点を結ぶ（12 本）
		for ( int32 Corner = 0; Corner < 8; ++Corner )
		{
			for ( int32 Bit = 0; Bit < 3; ++Bit )
			{
				if ( Corner & ( 1 << Bit ) ) continue;
				OutLines.Add( Corners[Corner] );
				OutLines.Add( Corners[Corner | ( 1 << Bit )] );
			}
		}
		break;
	}
	case EEditorShapeKind::Sphere:
		AppendArc( OutLines, Shape.Center, FVector3::Right, FVector3::Up, Shape.Radius, 0.0f, NMath::TWO_PI, Segments );
		AppendArc( OutLines, Shape.Center, FVector3::Up, FVector3::Forward, Shape.Radius, 0.0f, NMath::TWO_PI, Segments );
		AppendArc( OutLines, Shape.Center, FVector3::Right, FVector3::Forward, Shape.Radius, 0.0f, NMath::TWO_PI, Segments );
		break;
	case EEditorShapeKind::Capsule:
	{
		const FVector3 Axis = Shape.B - Shape.A;
		const float32 Length = Axis.Length();
		if ( Length < 1.0e-6f )
		{
			FEditorShape Sphere = Shape;
			Sphere.Kind = EEditorShapeKind::Sphere;
			Sphere.Center = Shape.A;
			AppendShapeWireLines( Sphere, Segments, OutLines );
			break;
		}
		const FVector3 Direction = Axis / Length;
		FVector3 U, V;
		MakePerpendicular( Direction, U, V );
		// 両端の円と、筋 4 本
		AppendArc( OutLines, Shape.A, U, V, Shape.Radius, 0.0f, NMath::TWO_PI, Segments );
		AppendArc( OutLines, Shape.B, U, V, Shape.Radius, 0.0f, NMath::TWO_PI, Segments );
		const FVector3 Rim[4] = { U, V, -U, -V };
		for ( const FVector3& Side : Rim )
		{
			OutLines.Add( Shape.A + Side * Shape.Radius );
			OutLines.Add( Shape.B + Side * Shape.Radius );
		}
		// 両端の半円（U 方向と V 方向の 2 枚ずつ）。先端は軸の外向き
		const int32 HalfSegments = NMath::Max( Segments / 2, 2 );
		AppendArc( OutLines, Shape.B, U, Direction, Shape.Radius, 0.0f, NMath::PI, HalfSegments );
		AppendArc( OutLines, Shape.B, V, Direction, Shape.Radius, 0.0f, NMath::PI, HalfSegments );
		AppendArc( OutLines, Shape.A, U, -Direction, Shape.Radius, 0.0f, NMath::PI, HalfSegments );
		AppendArc( OutLines, Shape.A, V, -Direction, Shape.Radius, 0.0f, NMath::PI, HalfSegments );
		break;
	}
	case EEditorShapeKind::Quad:
	{
		const FVector3 Corners[4] = { Shape.Center + Shape.HalfRight + Shape.HalfUp, Shape.Center + Shape.HalfRight - Shape.HalfUp,
									  Shape.Center - Shape.HalfRight - Shape.HalfUp, Shape.Center - Shape.HalfRight + Shape.HalfUp };
		for ( int32 i = 0; i < 4; ++i )
		{
			OutLines.Add( Corners[i] );
			OutLines.Add( Corners[( i + 1 ) % 4] );
		}
		break;
	}
	}
}

bool NWorldEditor::ComputeShapesBounds( const TArray<FEditorShape>& Shapes, FVector3& OutMin, FVector3& OutMax )
{
	if ( Shapes.IsEmpty() ) return false;
	FVector3 Min( NMath::MaxFloat, NMath::MaxFloat, NMath::MaxFloat );
	FVector3 Max( -NMath::MaxFloat, -NMath::MaxFloat, -NMath::MaxFloat );
	const auto Include = [&]( const FVector3& P )
	{
		Min = FVector3::Min( Min, P );
		Max = FVector3::Max( Max, P );
	};
	for ( const FEditorShape& Shape : Shapes )
	{
		switch ( Shape.Kind )
		{
		case EEditorShapeKind::Box:
			for ( int32 Corner = 0; Corner < 8; ++Corner )
			{
				const FVector3 Local( ( Corner & 1 ) ? Shape.Half.x : -Shape.Half.x, ( Corner & 2 ) ? Shape.Half.y : -Shape.Half.y, ( Corner & 4 ) ? Shape.Half.z : -Shape.Half.z );
				Include( Shape.Center + Shape.Rotation.Rotate( Local ) );
			}
			break;
		case EEditorShapeKind::Sphere:
			Include( Shape.Center - FVector3( Shape.Radius ) );
			Include( Shape.Center + FVector3( Shape.Radius ) );
			break;
		case EEditorShapeKind::Capsule:
			Include( Shape.A - FVector3( Shape.Radius ) );
			Include( Shape.A + FVector3( Shape.Radius ) );
			Include( Shape.B - FVector3( Shape.Radius ) );
			Include( Shape.B + FVector3( Shape.Radius ) );
			break;
		case EEditorShapeKind::Quad:
			Include( Shape.Center + Shape.HalfRight + Shape.HalfUp );
			Include( Shape.Center + Shape.HalfRight - Shape.HalfUp );
			Include( Shape.Center - Shape.HalfRight + Shape.HalfUp );
			Include( Shape.Center - Shape.HalfRight - Shape.HalfUp );
			break;
		}
	}
	OutMin = Min;
	OutMax = Max;
	return true;
}

FVector3 NWorldEditor::GetShapeCenter( const FEditorShape& Shape )
{
	return Shape.Kind == EEditorShapeKind::Capsule ? ( Shape.A + Shape.B ) * 0.5f : Shape.Center;
}

bool NWorldEditor::AppendEditMeshWireLines( const FEditMesh& Mesh, const FTransform& Transform, int32 MaxFaces, TArray<FVector3>& OutLines )
{
	const int32 NumUsed = NMath::Min( Mesh.GetNumFaces(), NMath::Max( MaxFaces, 0 ) );
	TSet<int64> Done;   // 描いた縁（小さい頂点の番号 << 32 | 大きい番号）
	for ( int32 Face = 0; Face < NumUsed; ++Face )
	{
		if ( !Mesh.IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i )
		{
			const int32 A = Indices[i];
			const int32 B = Indices[( i + 1 ) % Indices.Num()];
			const int64 Key = ( static_cast<int64>( NMath::Min( A, B ) ) << 32 ) | static_cast<int64>( NMath::Max( A, B ) );
			if ( !Done.Add( Key ) ) continue;
			OutLines.Add( Transform.TransformPoint( Mesh.Vertices[A] ) );
			OutLines.Add( Transform.TransformPoint( Mesh.Vertices[B] ) );
		}
	}
	return NumUsed == Mesh.GetNumFaces();
}

bool NWorldEditor::AppendMeshWireLines( const FEditorCollisionMesh& Mesh, const FTransform& Transform, int32 MaxTriangles, TArray<FVector3>& OutLines )
{
	const int32 NumTriangles = Mesh.GetNumTriangles();
	const int32 NumUsed = NMath::Min( NumTriangles, NMath::Max( MaxTriangles, 0 ) );
	TSet<int64> Done;   // 描いた辺（小さい番号 << 32 | 大きい番号）。共有している辺を 2 度描かない
	const auto Vertex = [&]( uint32 Index ) { return Mesh.Vertices.IsValidIndex( static_cast<int32>( Index ) ) ? Mesh.Vertices[static_cast<int32>( Index )] : FVector3::Zero; };
	for ( int32 Triangle = 0; Triangle < NumUsed; ++Triangle )
	{
		for ( int32 Edge = 0; Edge < 3; ++Edge )
		{
			const uint32 A = Mesh.Indices[Triangle * 3 + Edge];
			const uint32 B = Mesh.Indices[Triangle * 3 + ( Edge + 1 ) % 3];
			const int64 Key = ( static_cast<int64>( NMath::Min( A, B ) ) << 32 ) | static_cast<int64>( NMath::Max( A, B ) );
			if ( !Done.Add( Key ) ) continue;
			OutLines.Add( Transform.TransformPoint( Vertex( A ) ) );
			OutLines.Add( Transform.TransformPoint( Vertex( B ) ) );
		}
	}
	return NumUsed == NumTriangles;
}
