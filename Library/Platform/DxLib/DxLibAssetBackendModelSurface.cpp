// SPDX-License-Identifier: NOASSERTION
#include "DxLibAssetBackend.h"

#include <cmath>
#include <limits>
#include <utility>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 一時複製だけを所有し、失敗経路でもnative model handleを解放する。
	struct FTemporaryModel
	{
		// 解放するnative model handle。
		int32 Handle = -1;
		~FTemporaryModel() { if ( Handle >= 0 ) MV1DeleteModel( Handle ); }
		// 一時modelを解放し、native失敗を呼出側へ返す。
		bool Finish()
		{
			if ( Handle < 0 ) return false;
			const int32 ReleasingHandle = Handle;
			Handle = -1;
			return MV1DeleteModel( ReleasingHandle ) >= 0;
		}
	};

	// 設定した参照面を所有し、未完了の経路でもnative bufferを終了する。
	struct FReferenceMeshScope
	{
		// 参照面を設定した一時model handle。
		int32 Model = -1;
		// 参照面がまだ設定中か。
		bool bActive = false;
		~FReferenceMeshScope() { if ( bActive ) MV1TerminateReferenceMesh( Model, -1, TRUE, FALSE ); }
		// 参照面を終了し、native終了失敗を呼出側へ返す。
		bool Finish()
		{
			if ( !bActive ) return false;
			bActive = false;
			return MV1TerminateReferenceMesh( Model, -1, TRUE, FALSE ) >= 0;
		}
	};

	// 位置・法線・UVがすべて有限かを調べる。
	bool IsFiniteVertex( const FMeshVertex& Vertex )
	{
		return NMath::IsFinite( Vertex.Position[0] ) && NMath::IsFinite( Vertex.Position[1] ) && NMath::IsFinite( Vertex.Position[2] ) && NMath::IsFinite( Vertex.Normal[0] ) && NMath::IsFinite( Vertex.Normal[1] ) && NMath::IsFinite( Vertex.Normal[2] ) && NMath::IsFinite( Vertex.U ) && NMath::IsFinite( Vertex.V );
	}

	// 同じframe・mesh・材質の連続範囲ならsectionを伸ばし、それ以外は新設する。
	void AppendSection( FStaticModelSurfaceData& Data, const MV1_REF_POLYGON& Polygon )
	{
		// 現在のindex列末尾に対応するsection。
		FStaticModelSurfaceSection* Previous = Data.Sections.IsEmpty() ? nullptr : &Data.Sections.Last();
		if ( Previous != nullptr && Previous->SourceFrameIndex == Polygon.FrameIndex && Previous->SourceMeshIndex == Polygon.MeshIndex && Previous->MaterialIndex == Polygon.MaterialIndex )
		{
			Previous->NumIndices += 3;
			return;
		}
		// 新しい連続triangle範囲。
		const int32 SectionIndex = Data.Sections.AddDefaulted();
		// 追加した範囲のsection値。
		FStaticModelSurfaceSection& Section = Data.Sections[SectionIndex];
		Section.FirstIndex = Data.Indices.Num() - 3;
		Section.NumIndices = 3;
		Section.MaterialIndex = Polygon.MaterialIndex;
		Section.SourceFrameIndex = Polygon.FrameIndex;
		Section.SourceMeshIndex = Polygon.MeshIndex;
	}
}

// 元modelの変形・画像所有を変えず、静的reference meshを独立した値データへ写す。
bool CDxLibAssetBackend::GetStaticModelSurface( int32 Model, FStaticModelSurfaceData& Out )
{
	// 元handleの存在と非同期読み込み完了を確認する。
	if ( Model < 0 || CheckHandleASyncLoad( Model ) != FALSE ) return false;
	// 変形meshを持つ元モデルは静的snapshotの対象外。
	const int32 MeshCount = MV1GetMeshNum( Model );
	const int32 FrameCount = MV1GetFrameNum( Model );
	const int32 MaterialCount = MV1GetMaterialNum( Model );
	const int32 TriangleListCount = MV1GetTriangleListNum( Model );
	const int32 TextureCount = MV1GetTextureNum( Model );
	if ( MeshCount <= 0 || FrameCount <= 0 || MaterialCount <= 0 || TriangleListCount <= 0 || TextureCount < 0 || MV1GetAnimNum( Model ) != 0 || MV1GetShapeNum( Model ) != 0 ) return false;
	// すべて通常1frame頂点のtriangle listであることを確認する。
	for ( int32 ListIndex = 0; ListIndex < TriangleListCount; ++ListIndex )
	{
		if ( MV1GetTriangleListVertexType( Model, ListIndex ) != DX_MV1_VERTEX_TYPE_1FRAME ) return false;
	}
	// 参照mesh用の複製handle。元modelのmatrix・参照bufferには触れない。
	FTemporaryModel Temporary;
	Temporary.Handle = MV1DuplicateModel( Model );
	if ( Temporary.Handle < 0 || MV1SetMatrix( Temporary.Handle, MGetIdent() ) < 0 ) return false;
	// 元modelのframe階層だけを反映する参照mesh。
	FReferenceMeshScope ReferenceScope;
	ReferenceScope.Model = Temporary.Handle;
	if ( MV1SetupReferenceMesh( Temporary.Handle, -1, TRUE, FALSE ) < 0 ) return false;
	ReferenceScope.bActive = true;
	// DxLibが一時model内に保持する参照mesh配列。
	const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Temporary.Handle, -1, TRUE, FALSE );
	if ( Reference.PolygonNum <= 0 || Reference.VertexNum <= 0 || Reference.Polygons == nullptr || Reference.Vertexs == nullptr || Reference.PolygonNum > TNumericLimits<int32>::Max() / 3 ) return false;
	// 失敗時にOutを変えないよう、全結果を局所値へ組み立てる。
	FStaticModelSurfaceData Surface;
	Surface.Vertices.Reserve( Reference.VertexNum );
	Surface.Indices.Reserve( Reference.PolygonNum * 3 );
	Surface.Sections.Reserve( Reference.PolygonNum );
	Surface.Materials.Reserve( MaterialCount );
	// 元頂点の座標・法線・UV0・RGBAを参照順のまま写す。
	for ( int32 VertexIndex = 0; VertexIndex < Reference.VertexNum; ++VertexIndex )
	{
		// 現在のnative reference頂点。
		const MV1_REF_VERTEX& Source = Reference.Vertexs[VertexIndex];
		// framework形式へ値を写す頂点。
		const int32 VertexArrayIndex = Surface.Vertices.AddDefaulted();
		// 今追加した頂点値。
		FMeshVertex& Vertex = Surface.Vertices[VertexArrayIndex];
		Vertex.Position[0] = Source.Position.x;
		Vertex.Position[1] = Source.Position.y;
		Vertex.Position[2] = Source.Position.z;
		Vertex.Normal[0] = Source.Normal.x;
		Vertex.Normal[1] = Source.Normal.y;
		Vertex.Normal[2] = Source.Normal.z;
		Vertex.U = Source.TexCoord[0].u;
		Vertex.V = Source.TexCoord[0].v;
		Vertex.Color[0] = Source.DiffuseColor.r;
		Vertex.Color[1] = Source.DiffuseColor.g;
		Vertex.Color[2] = Source.DiffuseColor.b;
		Vertex.Color[3] = Source.DiffuseColor.a;
		if ( !IsFiniteVertex( Vertex ) ) return false;
	}
	// 全元材質のdiffuse色とborrowed画像handleを記録する。
	for ( int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex )
	{
		// DxLibから取得した拡散色。
		const COLOR_F Diffuse = MV1GetMaterialDifColor( Model, MaterialIndex );
		if ( !NMath::IsFinite( Diffuse.r ) || !NMath::IsFinite( Diffuse.g ) || !NMath::IsFinite( Diffuse.b ) || !NMath::IsFinite( Diffuse.a ) ) return false;
		// 元model内のdiffuse map texture index。
		const int32 TextureIndex = MV1GetMaterialDifMapTexture( Model, MaterialIndex );
		if ( TextureIndex < -1 || ( TextureIndex >= 0 && TextureIndex >= TextureCount ) ) return false;
		// 未使用材質を含めたdiffuse画像handle。handleは元modelから借りる。
		int32 GraphHandle = -1;
		if ( TextureIndex >= 0 )
		{
			GraphHandle = MV1GetTextureGraphHandle( Model, TextureIndex );
			// 読み込み済み画像の寸法。
			int Width = 0;
			int Height = 0;
			if ( GraphHandle < 0 || GetGraphSize( GraphHandle, &Width, &Height ) != 0 || Width <= 0 || Height <= 0 ) return false;
		}
		// framework材質配列へコピーする値。
		const int32 MaterialArrayIndex = Surface.Materials.AddDefaulted();
		// 今追加した材質値。
		FStaticModelSurfaceMaterial& Material = Surface.Materials[MaterialArrayIndex];
		Material.Diffuse[0] = Diffuse.r;
		Material.Diffuse[1] = Diffuse.g;
		Material.Diffuse[2] = Diffuse.b;
		Material.Diffuse[3] = Diffuse.a;
		Material.BorrowedDiffuseTexture = GraphHandle;
	}
	// 元triangle順と向きを保ち、隣接する同一frame・mesh・材質だけをsectionへまとめる。
	for ( int32 PolygonIndex = 0; PolygonIndex < Reference.PolygonNum; ++PolygonIndex )
	{
		// 現在のnative reference polygon。
		const MV1_REF_POLYGON& Polygon = Reference.Polygons[PolygonIndex];
		if ( Polygon.VIndexTarget != 0 || Polygon.FrameIndex >= FrameCount || Polygon.MeshIndex >= MeshCount || Polygon.MaterialIndex >= MaterialCount ) return false;
		for ( int32 Corner = 0; Corner < 3; ++Corner )
		{
			// 三角形が参照する元頂点番号。
			const int32 VertexIndex = Polygon.VIndex[Corner];
			if ( VertexIndex < 0 || VertexIndex >= Reference.VertexNum ) return false;
			Surface.Indices.Add( static_cast<uint32>( VertexIndex ) );
		}
		AppendSection( Surface, Polygon );
	}
	if ( Surface.Indices.Num() != Reference.PolygonNum * 3 || Surface.Sections.IsEmpty() || !ReferenceScope.Finish() || !Temporary.Finish() ) return false;
	Out = MoveTemp( Surface );
	return true;
}
