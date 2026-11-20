// SPDX-License-Identifier: MIT
#include "MaterialSurfaceSnapshot.h"
#include "Library/Framework/Material/Surface/Private/MaterialSurfaceSnapshotData.h"
#include "Library/Framework/Material/MaterialSceneConstants.h"
#include "Library/Framework/Render/Scene/SceneDrawList.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Mesh/ShaderMeshSnapshot.h"

namespace
{
	// 深度と合成を記録する。分類は呼び出し側で検査する。
	void RecordClass( FDrawContext& Draw, ESceneDrawClass Class )
	{
		Draw.Depth( Class == ESceneDrawClass::Translucent ? FDepthState::ReadOnly() : FDepthState::Normal() );
		Draw.Blend( Class == ESceneDrawClass::Translucent ? EDrawBlend::Alpha : EDrawBlend::Opaque );
	}

	// 有限な行列と色を両シェーダー段に記録する。不正な行列では追記しない。
	bool RecordObject( FDrawContext& Draw, const FMatrix4x4& World, const FColor32& Tint )
	{
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				if ( !NMath::IsFinite( World.m[Row][Column] ) ) { return false; }
			}
		}
		FMaterialObjectConstants Object; // 既存Surfaceと同じcbObject。
		Object.World = World;
		Object.Color = FVector4( Tint.r / 255.0f, Tint.g / 255.0f, Tint.b / 255.0f, Tint.a / 255.0f );
		Draw.ShaderConstants( EShaderStage::Vertex, 5, Object );
		Draw.ShaderConstants( EShaderStage::Pixel, 5, Object );
		return true;
	}
}

bool FMaterialSurfaceSnapshot::IsValid() const { return Data.IsValid(); }

bool FMaterialSurfaceSnapshot::TryGetDrawClass( ESceneDrawClass& Out ) const
{
	if ( !Data ) { return false; }
	switch ( Data->Blend )
	{
	case EMaterialSurfaceBlend::Opaque: Out = ESceneDrawClass::Opaque; return true;
	case EMaterialSurfaceBlend::Masked: Out = ESceneDrawClass::Masked; return true;
	case EMaterialSurfaceBlend::Translucent: Out = ESceneDrawClass::Translucent; return true;
	default: return false;
	}
}

bool FMaterialSurfaceSnapshot::AcceptsDrawClass( ESceneDrawClass Class ) const
{
	ESceneDrawClass Fixed = ESceneDrawClass::Opaque; // 定義で分類が指定されている場合だけ参照する。
	if ( TryGetDrawClass( Fixed ) ) { return Fixed == Class; }
	return Data && ( Class == ESceneDrawClass::Opaque || Class == ESceneDrawClass::Translucent );
}

bool FMaterialSurfaceSnapshot::BuildMeshCommands( FDrawBuffer& Out, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint ) const
{
	// 32bit環境でも個数からの長さ計算があふれない範囲へ先に制限する。
	if ( !Data || !Vertices || !Indices || NumVertices < 1 || NumVertices > NDrawCommand::MaxWords / static_cast<int32>( sizeof( FDrawShaderVertex3D ) / 4 ) || NumIndices < 3 || NumIndices > NDrawCommand::MaxWords * 2 || NumIndices % 3 != 0 ) { return false; }
	FDrawBuffer Mesh; // 完全な頂点と添字を確認してから出力を置き換える。
	FDrawContext Draw( Mesh ); // 既存の定数配置と可変長頂点の記録を使う。
	if ( !RecordObject( Draw, World, Tint ) ) { return false; }
	if ( !Draw.PrimitiveIndexedShader3D( Vertices, NumVertices, Indices, NumIndices, EDrawPrimitive::Triangles ) ) { return false; }
	FDrawBuffer Complete; // 固定設定、個体と形状、解除を一括して所有する。
	Complete.Words = Data->BeginWords;
	Complete.Words.Append( Mesh.Words );
	Complete.Words.Append( Data->EndWords );
	Out = MoveTemp( Complete );
	return true;
}

bool FMaterialSurfaceSnapshot::AddMesh( CSceneDrawList& List, ESceneDrawClass Class, const FVector3& WorldCenter, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint ) const
{
	FDrawBuffer Commands; // この呼出しだけの個体定数と形状。
	if ( !AcceptsDrawClass( Class ) || !BuildMeshCommands( Commands, Vertices, NumVertices, Indices, NumIndices, World, Tint ) ) { return false; }
	FDrawBuffer Item; // 材質の標準的な合成と深度も項目内で固定する。
	FDrawContext Draw( Item ); // 既存の状態設定を使う。
	RecordClass( Draw, Class );
	Item.Words.Append( Commands.Words );
	return List.Add( { Item.Words.GetData(), Item.Words.Num() }, Class, WorldCenter, Data );
}

bool FMaterialSurfaceSnapshot::AddMesh( CSceneDrawList& List, ESceneDrawClass Class, const FVector3& WorldCenter, const FShaderMeshSnapshot& Mesh, const FMatrix4x4& World, const FColor32& Tint ) const
{
	if ( !Data || !Mesh.IsValid() || !AcceptsDrawClass( Class ) ) { return false; }
	FDrawBuffer Item; // 形状以外の個体と材質の命令を所有する。
	FDrawContext Draw( Item ); // 既存の状態設定を使う。
	RecordClass( Draw, Class );
	Item.Words.Append( Data->BeginWords );
	if ( !RecordObject( Draw, World, Tint ) ) { return false; }
	Item.Words.Add( NDrawCommand::MakeHeader( EDrawCommand::SharedShaderMesh3D, 0, 1 ) );
	Item.Words.Append( Data->EndWords );
	return List.Add( { Item.Words.GetData(), Item.Words.Num(), &Mesh }, Class, WorldCenter, Data );
}

bool FMaterialSurfaceSnapshot::AddMesh( CSceneDrawList& List, const FVector3& WorldCenter, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint ) const
{
	ESceneDrawClass Class; // 固定材質に分類があれば記録する。
	return TryGetDrawClass( Class ) && AddMesh( List, Class, WorldCenter, Vertices, NumVertices, Indices, NumIndices, World, Tint );
}

bool FMaterialSurfaceSnapshot::AddMesh( CSceneDrawList& List, const FVector3& WorldCenter, const FShaderMeshSnapshot& Mesh, const FMatrix4x4& World, const FColor32& Tint ) const
{
	ESceneDrawClass Class; // 固定材質に分類があれば記録する。
	return TryGetDrawClass( Class ) && AddMesh( List, Class, WorldCenter, Mesh, World, Tint );
}
