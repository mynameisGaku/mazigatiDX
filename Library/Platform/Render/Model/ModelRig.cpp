// SPDX-License-Identifier: MIT
#include "ModelRig.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr float32 UnitsPerSecond = 30.0f;

	FMatrix4x4 ToMatrix( const FImportMatrix& Source )
	{
		FMatrix4x4 Result;
		FMemory::Memcpy( Result.m, Source.M, sizeof( Source.M ) );
		return Result;
	}

	bool IsFiniteMatrix( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		return true;
	}

	// 行列を、移動・回転・拡大に分ける（鏡写し（行列式が負）のときは、X の拡大を負にして吸収する）
	struct FTrs
	{
		FVector3 Translation;
		FQuaternion Rotation;
		FVector3 Scale = FVector3( 1.0f, 1.0f, 1.0f );
	};

	FTrs Decompose( const FMatrix4x4& Matrix )
	{
		FTrs Result;
		Result.Translation = Matrix.GetTranslation();
		FVector3 AxisX = Matrix.GetAxisX();
		FVector3 AxisY = Matrix.GetAxisY();
		FVector3 AxisZ = Matrix.GetAxisZ();
		Result.Scale = FVector3( AxisX.Length(), AxisY.Length(), AxisZ.Length() );
		const float32 Determinant = AxisX.x * ( AxisY.y * AxisZ.z - AxisY.z * AxisZ.y ) - AxisX.y * ( AxisY.x * AxisZ.z - AxisY.z * AxisZ.x ) + AxisX.z * ( AxisY.x * AxisZ.y - AxisY.y * AxisZ.x );
		if ( Determinant < 0.0f )
		{
			Result.Scale.x = -Result.Scale.x;
			AxisX = AxisX * -1.0f;
		}
		const float32 LengthX = Result.Scale.x < 0.0f ? -Result.Scale.x : Result.Scale.x;
		if ( LengthX > 1.0e-8f && Result.Scale.y > 1.0e-8f && Result.Scale.z > 1.0e-8f )
		{
			Result.Rotation = FQuaternion::FromBasis( AxisX / LengthX, AxisY / Result.Scale.y, AxisZ / Result.Scale.z );
		}
		return Result;
	}

	// 骨の行列として使えるか（行列式が極端でなく、最後の列が 0, 0, 0, 1）
	bool IsAffine( const FMatrix4x4& Matrix )
	{
		return IsFiniteMatrix( Matrix ) && Matrix.m[0][3] == 0.0f && Matrix.m[1][3] == 0.0f && Matrix.m[2][3] == 0.0f && Matrix.m[3][3] == 1.0f;
	}
}

bool NModelRig::Build( const FImportedScene& Scene, FModelRig& Out, FString& Error )
{
	if ( Scene.Nodes.Num() > 65536 || Scene.Meshes.Num() > 4096 )
	{
		Error = "Invalid rig size";
		return false;
	}
	FModelRig Rig;
	const int32 NumNodes = Scene.Nodes.Num();
	Rig.Parent.SetNum( NumNodes );
	Rig.BaseLocal.SetNum( NumNodes );
	Rig.NodeNames.SetNum( NumNodes );
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		const FImportedNode& Node = Scene.Nodes[Index];
		if ( Node.Parent < -1 || Node.Parent >= NumNodes || Node.Parent == Index )
		{
			Error = "Invalid parent frame";
			return false;
		}
		Rig.Parent[Index] = Node.Parent;
		Rig.BaseLocal[Index] = ToMatrix( Node.Local );
		Rig.NodeNames[Index] = Node.Name;
		if ( !IsAffine( Rig.BaseLocal[Index] ) )
		{
			Error = "Non-affine or non-finite frame transform";
			return false;
		}
		if ( !Node.Name.IsEmpty() && !Rig.NodeByName.Contains( Node.Name ) )
		{
			Rig.NodeByName.Add( Node.Name, Index );
		}
	}
	// 親が先に来る並び（循環は失敗）
	TArray<uint8> State;
	State.SetNum( NumNodes, 0 );
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		TArray<int32> Chain;
		int32 Current = Index;
		while ( Current >= 0 && State[Current] != 2 )
		{
			if ( State[Current] == 1 )
			{
				Error = "Cyclic frame hierarchy";
				return false;
			}
			State[Current] = 1;
			Chain.Add( Current );
			Current = Rig.Parent[Current];
		}
		while ( !Chain.IsEmpty() )
		{
			const int32 Frame = Chain.Pop();
			State[Frame] = 2;
			Rig.Order.Add( Frame );
		}
	}
	Rig.Meshes.SetNum( Scene.Meshes.Num() );
	for ( int32 MeshIndex = 0; MeshIndex < Scene.Meshes.Num(); ++MeshIndex )
	{
		const FImportedMesh& Source = Scene.Meshes[MeshIndex];
		FRigMesh& Mesh = Rig.Meshes[MeshIndex];
		if ( Source.Node < -1 || Source.Node >= NumNodes || Source.Normals.Num() != Source.Positions.Num() )
		{
			Error = "Invalid mesh node or normals";
			return false;
		}
		Mesh.Node = Source.Node;
		Mesh.Positions = Source.Positions;
		Mesh.Normals = Source.Normals;
		if ( Source.bHasUnsupportedSkinning || Source.bHasBlendShapes || Source.bHasVertexCache )
		{
			Error = "Unsupported deformation (blend shapes, vertex cache or non-linear skinning)";
			return false;
		}
		if ( !Source.Clusters.IsEmpty() )
		{
			const int32 NumVertices = Source.Positions.Num();
			TArray<TArray<FRigInfluenceList>> Unused;
			TArray<int32> Count;
			Count.SetNum( NumVertices, 0 );
			for ( int32 Cluster = 0; Cluster < Source.Clusters.Num(); ++Cluster )
			{
				const FImportedCluster& Input = Source.Clusters[Cluster];
				if ( Input.BoneNode < 0 || Input.BoneNode >= NumNodes || Input.Vertices.Num() != Input.Weights.Num() )
				{
					Error = "Invalid skin cluster";
					return false;
				}
				for ( int32 Vertex : Input.Vertices )
				{
					if ( Vertex < 0 || Vertex >= NumVertices )
					{
						Error = "Invalid skin vertex";
						return false;
					}
					++Count[Vertex];
				}
				Mesh.ClusterBone.Add( Input.BoneNode );
				Mesh.ClusterOffset.Add( ToMatrix( Input.Offset ) );
			}
			Mesh.Influences.Start.SetNum( NumVertices + 1, 0 );
			for ( int32 Vertex = 0; Vertex < NumVertices; ++Vertex )
			{
				Mesh.Influences.Start[Vertex + 1] = Mesh.Influences.Start[Vertex] + Count[Vertex];
			}
			const int32 Total = Mesh.Influences.Start[NumVertices];
			Mesh.Influences.Cluster.SetNum( Total, 0 );
			Mesh.Influences.Weight.SetNum( Total, 0.0f );
			TArray<int32> Fill;
			Fill.SetNum( NumVertices, 0 );
			for ( int32 Cluster = 0; Cluster < Source.Clusters.Num(); ++Cluster )
			{
				const FImportedCluster& Input = Source.Clusters[Cluster];
				for ( int32 Index = 0; Index < Input.Vertices.Num(); ++Index )
				{
					const int32 Vertex = Input.Vertices[Index];
					const int32 Slot = Mesh.Influences.Start[Vertex] + Fill[Vertex]++;
					Mesh.Influences.Cluster[Slot] = Cluster;
					Mesh.Influences.Weight[Slot] = Input.Weights[Index];
				}
			}
			Mesh.bSkinned = true;
		}
	}
	Rig.Shapes.Reserve( Scene.Shapes.Num() );
	for ( const FImportedShape& Source : Scene.Shapes )
	{
		FRigShape Shape;
		Shape.Name = Source.Name;
		for ( const FImportedShapeMesh& Part : Source.Meshes )
		{
			if ( Part.Mesh < 0 || Part.Mesh >= Rig.Meshes.Num() || Part.Vertices.Num() != Part.Offsets.Num() )
			{
				Error = "Invalid shape";
				return false;
			}
			const int32 NumVertices = Rig.Meshes[Part.Mesh].Positions.Num();
			for ( const int32 Vertex : Part.Vertices )
			{
				if ( Vertex < 0 || Vertex >= NumVertices )
				{
					Error = "Invalid shape vertex";
					return false;
				}
			}
			FRigShapeMesh Mesh;
			Mesh.Mesh = Part.Mesh;
			Mesh.Vertices = Part.Vertices;
			Mesh.Offsets = Part.Offsets;
			Shape.Meshes.Add( MoveTemp( Mesh ) );
		}
		Rig.Shapes.Add( MoveTemp( Shape ) );
	}
	Rig.Clips = Scene.Animations;
	for ( const FImportedAnimation& Clip : Rig.Clips )
	{
		if ( Clip.NumKeys < 0 || !NMath::IsFinite( Clip.Fps ) || Clip.Fps <= 0.0f )
		{
			Error = "Invalid animation clip";
			return false;
		}
		for ( const FImportedTrack& Track : Clip.Tracks )
		{
			if ( Track.Keys.Num() != Clip.NumKeys )
			{
				Error = "Animation track key count does not match the clip";
				return false;
			}
		}
	}
	Out = MoveTemp( Rig );
	return true;
}

void NModelRig::InitPose( const FModelRig& Rig, FModelPose& Pose )
{
	Pose = FModelPose();
	Pose.bOverride.SetNum( Rig.GetNumNodes(), 0 );
	Pose.Override.SetNum( Rig.GetNumNodes() );
	Pose.FrameHidden.SetNum( Rig.GetNumNodes(), 0 );
	Pose.MeshHidden.SetNum( Rig.Meshes.Num(), 0 );
	Pose.ShapeRate.SetNum( Rig.Shapes.Num(), 0.0f );
}

float32 NModelRig::GetClipLength( const FModelRig& Rig, int32 Clip )
{
	if ( Clip < 0 || Clip >= Rig.Clips.Num() )
	{
		return 0.0f;
	}
	const FImportedAnimation& Animation = Rig.Clips[Clip];
	return Animation.NumKeys > 1 ? static_cast<float32>( Animation.NumKeys - 1 ) * UnitsPerSecond / Animation.Fps : 0.0f;
}

int32 NModelRig::Attach( const FModelRig& Rig, FModelPose& Pose, const TSharedPtr<const FModelRig>& Source, int32 Clip )
{
	if ( !Source.IsValid() || Clip < 0 || Clip >= Source->Clips.Num() )
	{
		return -1;
	}
	int32 Slot = -1;
	for ( int32 Index = 0; Index < Pose.Attaches.Num(); ++Index )
	{
		if ( !Pose.Attaches[Index].bUsed )
		{
			Slot = Index;
			break;
		}
	}
	if ( Slot < 0 )
	{
		if ( Pose.Attaches.Num() >= 64 )
		{
			return -1;
		}
		Slot = Pose.Attaches.Add( FModelAttach() );
	}
	FModelAttach& Attach = Pose.Attaches[Slot];
	Attach = FModelAttach();
	Attach.bUsed = true;
	Attach.Source = Source;
	Attach.Clip = Clip;
	Attach.NodeTrack.SetNum( Rig.GetNumNodes(), -1 );
	Attach.FrameRate.SetNum( Rig.GetNumNodes(), -1.0f );
	const FImportedAnimation& Animation = Source->Clips[Clip];
	for ( int32 Track = 0; Track < Animation.Tracks.Num(); ++Track )
	{
		const int32 SourceNode = Animation.Tracks[Track].Node;
		if ( SourceNode < 0 || SourceNode >= Source->NodeNames.Num() )
		{
			continue;
		}
		if ( const int32* Target = Rig.NodeByName.Find( Source->NodeNames[SourceNode] ) )
		{
			if ( Attach.NodeTrack[*Target] < 0 )
			{
				Attach.NodeTrack[*Target] = Track;
			}
		}
	}
	++Pose.Version;
	return Slot;
}

void NModelRig::Detach( FModelPose& Pose, int32 AttachIndex )
{
	if ( AttachIndex >= 0 && AttachIndex < Pose.Attaches.Num() && Pose.Attaches[AttachIndex].bUsed )
	{
		Pose.Attaches[AttachIndex] = FModelAttach();
		++Pose.Version;
	}
}

FMatrix4x4 NModelRig::SampleTrack( const FImportedAnimation& Clip, int32 Track, float32 Time )
{
	if ( Track < 0 || Track >= Clip.Tracks.Num() || Clip.Tracks[Track].Keys.IsEmpty() )
	{
		return FMatrix4x4();
	}
	const TArray<FImportMatrix>& Keys = Clip.Tracks[Track].Keys;
	const float32 Position = NMath::Clamp( ( NMath::IsFinite( Time ) ? Time : 0.0f ) / UnitsPerSecond * Clip.Fps, 0.0f, static_cast<float32>( Keys.Num() - 1 ) );
	const int32 First = static_cast<int32>( Position );
	const int32 Second = NMath::Min( First + 1, Keys.Num() - 1 );
	const float32 Alpha = Position - static_cast<float32>( First );
	if ( First == Second || Alpha <= 0.0f )
	{
		return ToMatrix( Keys[First] );
	}
	TArray<FMatrix4x4> Matrices;
	Matrices.Add( ToMatrix( Keys[First] ) );
	Matrices.Add( ToMatrix( Keys[Second] ) );
	TArray<float32> Weights;
	Weights.Add( 1.0f - Alpha );
	Weights.Add( Alpha );
	return BlendMatrices( Matrices, Weights );
}

FMatrix4x4 NModelRig::BlendMatrices( const TArray<FMatrix4x4>& Matrices, const TArray<float32>& Weights )
{
	float64 Sum = 0.0;
	for ( float32 Weight : Weights )
	{
		Sum += Weight;
	}
	if ( Matrices.IsEmpty() || Matrices.Num() != Weights.Num() || !( Sum > 0.0 ) )
	{
		return FMatrix4x4();
	}
	if ( Matrices.Num() == 1 )
	{
		return Matrices[0];
	}
	FVector3 Translation( 0.0f, 0.0f, 0.0f );
	FVector3 Scale( 0.0f, 0.0f, 0.0f );
	float64 Quaternion[4] = { 0.0, 0.0, 0.0, 0.0 };
	FQuaternion Reference;
	for ( int32 Index = 0; Index < Matrices.Num(); ++Index )
	{
		const float32 Weight = static_cast<float32>( Weights[Index] / Sum );
		const FTrs Item = Decompose( Matrices[Index] );
		Translation = Translation + Item.Translation * Weight;
		Scale = Scale + Item.Scale * Weight;
		FQuaternion Rotation = Item.Rotation;
		if ( Index == 0 )
		{
			Reference = Rotation;
		}
		else if ( FQuaternion::Dot( Reference, Rotation ) < 0.0f )
		{
			Rotation = FQuaternion( -Rotation.x, -Rotation.y, -Rotation.z, -Rotation.w ); // 向きを揃える
		}
		Quaternion[0] += static_cast<float64>( Rotation.x ) * Weight;
		Quaternion[1] += static_cast<float64>( Rotation.y ) * Weight;
		Quaternion[2] += static_cast<float64>( Rotation.z ) * Weight;
		Quaternion[3] += static_cast<float64>( Rotation.w ) * Weight;
	}
	FQuaternion Blended( static_cast<float32>( Quaternion[0] ), static_cast<float32>( Quaternion[1] ), static_cast<float32>( Quaternion[2] ), static_cast<float32>( Quaternion[3] ) );
	Blended = Blended.Normalized();
	return FMatrix4x4::TRS( Translation, Blended, Scale );
}

void NModelRig::EvaluateLocals( const FModelRig& Rig, const FModelPose& Pose, TArray<FMatrix4x4>& OutLocal )
{
	const int32 NumNodes = Rig.GetNumNodes();
	OutLocal.SetNum( NumNodes );
	TArray<FMatrix4x4> Items;
	TArray<float32> Weights;
	for ( int32 Node = 0; Node < NumNodes; ++Node )
	{
		if ( Node < Pose.bOverride.Num() && Pose.bOverride[Node] )
		{
			OutLocal[Node] = Pose.Override[Node];
			continue;
		}
		Items.Reset();
		Weights.Reset();
		float64 Sum = 0.0;
		for ( const FModelAttach& Attach : Pose.Attaches )
		{
			if ( !Attach.bUsed || !Attach.Source.IsValid() || Node >= Attach.NodeTrack.Num() || Attach.NodeTrack[Node] < 0 )
			{
				continue;
			}
			const float32 Rate = Attach.FrameRate[Node] >= 0.0f ? Attach.FrameRate[Node] : Attach.Rate;
			if ( !( Rate > 0.0f ) || !NMath::IsFinite( Rate ) )
			{
				continue;
			}
			Items.Add( SampleTrack( Attach.Source->Clips[Attach.Clip], Attach.NodeTrack[Node], Attach.Time ) );
			Weights.Add( Rate );
			Sum += Rate;
		}
		if ( Items.IsEmpty() )
		{
			OutLocal[Node] = Rig.BaseLocal[Node];
			continue;
		}
		if ( Sum < 1.0 )
		{
			Items.Add( Rig.BaseLocal[Node] ); // 足りないぶんは、元の姿
			Weights.Add( static_cast<float32>( 1.0 - Sum ) );
		}
		OutLocal[Node] = BlendMatrices( Items, Weights );
	}
}

void NModelRig::ComputeGlobals( const FModelRig& Rig, const TArray<FMatrix4x4>& Local, TArray<FMatrix4x4>& OutGlobal )
{
	OutGlobal.SetNum( Rig.GetNumNodes() );
	for ( int32 Node : Rig.Order )
	{
		const int32 Parent = Rig.Parent[Node];
		OutGlobal[Node] = Parent < 0 ? Local[Node] : Local[Node] * OutGlobal[Parent];
	}
}

bool NModelRig::EvaluateShapes( const FModelRig& Rig, const FModelPose& Pose, TArray<TArray<FVector3>>& OutPositions )
{
	OutPositions.Reset();
	OutPositions.SetNum( Rig.Meshes.Num() );
	bool bAny = false;
	const int32 NumShapes = NMath::Min( Rig.Shapes.Num(), Pose.ShapeRate.Num() );
	for ( int32 ShapeIndex = 0; ShapeIndex < NumShapes; ++ShapeIndex )
	{
		const float32 Rate = Pose.ShapeRate[ShapeIndex];
		if ( !( Rate != 0.0f ) ) // 0 と NaN は、効かない
		{
			continue;
		}
		for ( const FRigShapeMesh& Part : Rig.Shapes[ShapeIndex].Meshes )
		{
			TArray<FVector3>& Positions = OutPositions[Part.Mesh];
			if ( Positions.IsEmpty() )
			{
				Positions = Rig.Meshes[Part.Mesh].Positions; // 最初に触れたとき、基準の位置を写す
			}
			for ( int32 Index = 0; Index < Part.Vertices.Num(); ++Index )
			{
				const int32 Vertex = Part.Vertices[Index];
				Positions[Vertex] = Positions[Vertex] + Part.Offsets[Index] * Rate;
				bAny = true;
			}
		}
	}
	return bAny;
}

void NModelRig::Deform( const FModelRig& Rig, const TArray<FMatrix4x4>& Globals, const TArray<FRigVertexSource>& Sources, FDrawVertex3D* Vertices, int32 NumVertices, IJobSystem* Jobs, const TArray<TArray<FVector3>>* Morphed )
{
	if ( NumVertices > Sources.Num() )
	{
		NumVertices = Sources.Num();
	}
	// メッシュごとに、クラスターの行列（Offset * 骨の行列）を先に作る
	TArray<TArray<FMatrix4x4>> ClusterMatrices;
	ClusterMatrices.SetNum( Rig.Meshes.Num() );
	for ( int32 MeshIndex = 0; MeshIndex < Rig.Meshes.Num(); ++MeshIndex )
	{
		const FRigMesh& Mesh = Rig.Meshes[MeshIndex];
		ClusterMatrices[MeshIndex].Reserve( Mesh.ClusterBone.Num() );
		for ( int32 Cluster = 0; Cluster < Mesh.ClusterBone.Num(); ++Cluster )
		{
			ClusterMatrices[MeshIndex].Add( Mesh.ClusterOffset[Cluster] * Globals[Mesh.ClusterBone[Cluster]] );
		}
	}
	// 頂点の範囲を変形する。各頂点は自分の出力（Vertices[Index]）だけを書き、読むものは変えないので、範囲を分けて並列に呼べる。
	const auto DeformRange = [&]( int32 Begin, int32 End )
	{
		for ( int32 Index = Begin; Index < End; ++Index )
		{
			const FRigVertexSource& Source = Sources[Index];
			if ( Source.Mesh < 0 || Source.Mesh >= Rig.Meshes.Num() )
			{
				continue;
			}
			const FRigMesh& Mesh = Rig.Meshes[Source.Mesh];
			if ( Source.Vertex < 0 || Source.Vertex >= Mesh.Positions.Num() )
			{
				continue;
			}
			const bool bMorphed = Morphed != nullptr && Source.Mesh < Morphed->Num() && !( *Morphed )[Source.Mesh].IsEmpty();
			const FVector3& Rest = bMorphed ? ( *Morphed )[Source.Mesh][Source.Vertex] : Mesh.Positions[Source.Vertex];
			const FVector3& RestNormal = Mesh.Normals[Source.Vertex];
			FVector3 Position( 0.0f, 0.0f, 0.0f );
			FVector3 Normal( 0.0f, 0.0f, 0.0f );
			bool bDone = false;
			if ( Mesh.bSkinned )
			{
				const int32 SlotBegin = Mesh.Influences.Start[Source.Vertex];
				const int32 SlotEnd = Mesh.Influences.Start[Source.Vertex + 1];
				const TArray<FMatrix4x4>& Clusters = ClusterMatrices[Source.Mesh]; // このメッシュのクラスターの行列。
				float32 Total = 0.0f;
				for ( int32 Slot = SlotBegin; Slot < SlotEnd; ++Slot )
				{
					const float32 Weight = Mesh.Influences.Weight[Slot];
					if ( !( Weight > 0.0f ) )
					{
						continue;
					}
					const FMatrix4x4& Matrix = Clusters[Mesh.Influences.Cluster[Slot]];
					Position = Position + Matrix.TransformPoint( Rest ) * Weight;
					Normal = Normal + Matrix.TransformVector( RestNormal ) * Weight;
					Total += Weight;
				}
				if ( Total > 0.0f )
				{
					Position = Position / Total; // 重みの合計が 1 でなくても、位置がずれないように
					bDone = true;
				}
			}
			if ( !bDone )
			{
				const FMatrix4x4 Matrix = Mesh.Node < 0 ? FMatrix4x4() : Globals[Mesh.Node];
				Position = Matrix.TransformPoint( Rest );
				Normal = Matrix.TransformVector( RestNormal );
			}
			Normal = Normal.Normalized();
			Vertices[Index].Position[0] = Position.x;
			Vertices[Index].Position[1] = Position.y;
			Vertices[Index].Position[2] = Position.z;
			Vertices[Index].Normal[0] = Normal.x;
			Vertices[Index].Normal[1] = Normal.y;
			Vertices[Index].Normal[2] = Normal.z;
		}
	};
	constexpr int32 BatchSize = 4096; // 1 つの範囲の頂点の数の下限（小さいモデルは、ワーカーを起こさずに、呼んだスレッドだけで変形する）。
	if ( Jobs && NumVertices > BatchSize )
	{
		Jobs->ParallelForRange( NumVertices, BatchSize, "Model.Deform", DeformRange );
	}
	else
	{
		DeformRange( 0, NumVertices );
	}
}
