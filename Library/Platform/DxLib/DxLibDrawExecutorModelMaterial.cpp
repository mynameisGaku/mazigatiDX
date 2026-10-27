// SPDX-License-Identifier: NOASSERTION
#include "DxLibDrawExecutor.h"

#include <algorithm>

#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Math/Matrix4x4.h"

namespace
{
	// モデル材質効果命令の診断分類。
	DEFINE_LOG_CATEGORY_STATIC( LogModelMaterialCommand, Log );

	// commandの12個の値をDxLib行列へ写す。
	MATRIX ToModelMaterialMatrix( const float32 ( &Values )[12] )
	{
		MATRIX Result{};
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 3; ++Column ) Result.m[Row][Column] = Values[Row * 3 + Column];
			Result.m[Row][3] = Row == 3 ? 1.0f : 0.0f;
		}
		return Result;
	}

	// frame自身と親frameの表示状態を調べ、取得失敗はfalseで返す。
	bool GetModelMaterialFrameVisibility( int32 Model, int32 FrameIndex, int32 FrameCount, bool& OutVisible )
	{
		OutVisible = false;
		if ( FrameIndex < 0 || FrameIndex >= FrameCount ) return false;
		// 親を循環参照せず調べたframe番号。
		TArray<int32> Visited;
		int32 Current = FrameIndex;
		bool bVisible = true;
		while ( Current >= 0 )
		{
			if ( Current >= FrameCount || Visited.Contains( Current ) ) return false;
			// 現在frame自身のnative可視値。
			const int32 FrameVisible = MV1GetFrameVisible( Model, Current );
			if ( FrameVisible != FALSE && FrameVisible != TRUE ) return false;
			if ( FrameVisible == FALSE ) bVisible = false;
			Visited.Add( Current );
			// 親frameまたはroot終端番号。
			const int32 Parent = MV1GetFrameParent( Model, Current );
			if ( Parent == -2 ) { OutVisible = bVisible; return true; }
			if ( Parent < 0 || Parent >= FrameCount ) return false;
			Current = Parent;
		}
		return false;
	}

	// model中のframeが指定meshを実際に所有するか確認する。
	bool IsModelMaterialFrameMesh( int32 Model, int32 Frame, int32 Mesh )
	{
		const int32 FrameMeshCount = MV1GetFrameMeshNum( Model, Frame );
		if ( FrameMeshCount <= 0 ) return false;
		for ( int32 Index = 0; Index < FrameMeshCount; ++Index )
		{
			if ( MV1GetFrameMesh( Model, Frame, Index ) == Mesh ) return true;
		}
		return false;
	}

	// 選択meshを隠した範囲の終了時に元の可視値を戻す。
	struct FModelMaterialVisibilityRestore
	{
		// 状態変更する個体model handle。
		int32 Model = -1;
		// 元のmesh番号と可視値。
		TArray<int32> Meshes;
		TArray<int32> Visible;
		// 元のmesh可視値をすでに戻したかを示す状態。
		bool bRestored = false;

		void Restore()
		{
			if ( bRestored ) return;
			for ( int32 Index = 0; Index < Meshes.Num(); ++Index ) MV1SetMeshVisible( Model, Meshes[Index], Visible[Index] );
			bRestored = true;
		}
		// 一時非表示にする前に対象meshが表示されていたか返す。
		bool WasVisible( int32 Mesh ) const
		{
			for ( int32 Index = 0; Index < Meshes.Num(); ++Index ) if ( Meshes[Index] == Mesh ) return Visible[Index] != FALSE;
			return false;
		}
		~FModelMaterialVisibilityRestore() { Restore(); }
	};

}

// 固定payloadを検査し、残りをnative描画、対象meshだけ材質効果描画する。
void CDxLibDrawExecutor::ExecuteModelMaterial( const FDrawCommandView& Command )
{
	// commandが持つ固定payload word数。
	constexpr int32 DataWords = sizeof( FDrawModelMaterialData ) / 4;
	// malformed commandを一度だけ診断する状態。
	static bool bInvalidLogged = false;
	// Model材質効果のchunk描画失敗を一度だけ知らせる状態。
	static bool bModelMaterialDrawFailedLogged = false;
	if ( Command.NumDataWords != DataWords )
	{
		if ( !bInvalidLogged ) { LOG( LogModelMaterialCommand, Warning, "モデル材質効果命令のpayload長が不正なため描画を読み飛ばしました" ); bInvalidLogged = true; }
		return;
	}
	// command bytesから型付き固定値へ複写するheader。
	FDrawModelMaterialData Data{};
	FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
	if ( !NDrawModelMaterial::IsValid( Data ) )
	{
		if ( !bInvalidLogged ) { LOG( LogModelMaterialCommand, Warning, "モデル材質効果命令の設定値が不正なため描画を読み飛ばしました" ); bInvalidLogged = true; }
		return;
	}
	// 行列配列からDxLibのinstance変換を作る値。
	const MATRIX WorldMatrix = ToModelMaterialMatrix( Data.M );
	// 対応機能がない場合にも原モデルを通常描画する。
	const auto DrawNativeFallback = [&]()
	{
		if ( MV1GetMeshNum( Data.Handle ) <= 0 ) return;
		MV1SetMatrix( Data.Handle, WorldMatrix );
		MV1DrawModel( Data.Handle );
	};
	if ( AssetBackend == nullptr || !EffectMaterialRenderer.IsMeshAvailable() ) { DrawNativeFallback(); return; }
	// handleからbackend所有の不変mesh形状を参照するresource。
	const FDxLibModelMaterialGeometry* Geometry = AssetBackend->FindModelMaterialGeometry( Data.Geometry );
	if ( Geometry == nullptr || Geometry->Meshes.IsEmpty() ) { DrawNativeFallback(); return; }
	// modelが保持するnative mesh数。
	const int32 NativeMeshCount = MV1GetMeshNum( Data.Handle );
	// modelのframe数。
	const int32 NativeFrameCount = MV1GetFrameNum( Data.Handle );
	if ( NativeMeshCount <= 0 || NativeFrameCount <= 0 ) { DrawNativeFallback(); return; }
	// modelと通常mesh分類の有効な可視状態。
	const int32 ModelVisible = MV1GetVisible( Data.Handle );
	const int32 CategoryVisible = MV1GetMeshCategoryVisible( Data.Handle, DX_MV1_MESHCATEGORY_NORMAL );
	if ( ( ModelVisible != FALSE && ModelVisible != TRUE ) || ( CategoryVisible != FALSE && CategoryVisible != TRUE ) ) { DrawNativeFallback(); return; }
	if ( ModelVisible == FALSE ) return;
	// 通常mesh分類が表示中かをcustom部分だけへ適用する値。
	const bool bNormalCategoryVisible = CategoryVisible != FALSE;
	// command個体行列をrenderer用の数値型へ変換した値。
	const FMatrix4x4 World( WorldMatrix );
	// 先行するDxLib頂点を確定してから描画先・viewportを事前確認する。
	RenderVertex();
	// 対象meshのinstance状態とchunk描画可否をすべて確認した状態。
	bool bPreflightValid = true;
	// 元meshの裏面除去値を形状順に保存する配列。
	TArray<int32> NativeCullModes;
	// frameと親frameの合成可視状態を保存する配列。
	TArray<int32> FrameVisibility;
	NativeCullModes.Reserve( Geometry->Meshes.Num() );
	FrameVisibility.Reserve( Geometry->Meshes.Num() );
	for ( const FDxLibModelMaterialGeometry::FMesh& Mesh : Geometry->Meshes )
	{
		if ( Mesh.SourceMeshIndex < 0 || Mesh.SourceMeshIndex >= NativeMeshCount || Mesh.SourceFrameIndex < 0 || Mesh.SourceFrameIndex >= NativeFrameCount || Mesh.MaterialIndex < 0 || MV1GetMeshMaterial( Data.Handle, Mesh.SourceMeshIndex ) != Mesh.MaterialIndex || !IsModelMaterialFrameMesh( Data.Handle, Mesh.SourceFrameIndex, Mesh.SourceMeshIndex ) || Mesh.Chunks.IsEmpty() ) { bPreflightValid = false; break; }
		// 元meshの一時非表示前のraw状態。
		const int32 MeshVisible = MV1GetMeshVisible( Data.Handle, Mesh.SourceMeshIndex );
		// 同じ個体meshに適用される裏面設定。
		const int32 NativeCull = MV1GetMeshBackCulling( Data.Handle, Mesh.SourceMeshIndex );
		// frameの親まで含めた表示状態を受け取る値。
		bool bFrameVisible = false;
		if ( ( MeshVisible != FALSE && MeshVisible != TRUE ) || ( NativeCull != DX_CULLING_LEFT && NativeCull != DX_CULLING_RIGHT && NativeCull != DX_CULLING_NONE ) || !GetModelMaterialFrameVisibility( Data.Handle, Mesh.SourceFrameIndex, NativeFrameCount, bFrameVisible ) ) { bPreflightValid = false; break; }
		NativeCullModes.Add( NativeCull );
		FrameVisibility.Add( bFrameVisible ? 1 : 0 );
		for ( const FMeshGeometryChunk& Chunk : Mesh.Chunks )
		{
			if ( Chunk.Vertices.Num() < 3 || Chunk.Indices.Num() < 3 || Chunk.Indices.Num() % 3 != 0 || Chunk.Vertices.Num() > FDrawMaterialMeshData::MaxVertices || Chunk.Indices.Num() > FDrawMaterialMeshData::MaxIndices ) { bPreflightValid = false; break; }
			// chunk配列長を設定した、描画可否の一時header。
			FDrawMaterialMeshData MeshData = Data.Material;
			MeshData.NumVertices = Chunk.Vertices.Num();
			MeshData.NumIndices = Chunk.Indices.Num();
			if ( !NDrawMaterialMesh::IsValid( MeshData, Chunk.Vertices.GetData(), Chunk.Indices.GetData() ) || !EffectMaterialRenderer.CanDrawModelMesh( MeshData, &World ) ) { bPreflightValid = false; break; }
		}
		if ( !bPreflightValid ) break;
	}
	if ( !bPreflightValid ) { DrawNativeFallback(); return; }
	// model全体が非表示ならnativeとcustomどちらも描かない。
	// 選択meshの一時非表示と復元情報。
	FModelMaterialVisibilityRestore VisibilityRestore;
	VisibilityRestore.Model = Data.Handle;
	for ( const FDxLibModelMaterialGeometry::FMesh& Mesh : Geometry->Meshes )
	{
		const int32 PreviousVisible = MV1GetMeshVisible( Data.Handle, Mesh.SourceMeshIndex );
		VisibilityRestore.Meshes.Add( Mesh.SourceMeshIndex );
		VisibilityRestore.Visible.Add( PreviousVisible );
		if ( PreviousVisible != FALSE ) MV1SetMeshVisible( Data.Handle, Mesh.SourceMeshIndex, FALSE );
	}
	// command個体変換で残りのnative材質を一度描く。
	MV1SetMatrix( Data.Handle, WorldMatrix );
	MV1DrawModel( Data.Handle );
	// pack描画の可視判定が変わる前に除外modelをflushする。
	RenderVertex();
	// native状態復元後に選択材質だけshader描画する。
	VisibilityRestore.Restore();
	// Alpha/Add blendを設定する前の呼出側blend値。
	int32 PreviousBlend = DX_BLENDMODE_NOBLEND;
	// 呼出側blend強度。
	int32 PreviousBlendParam = 0;
	GetDrawBlendMode( &PreviousBlend, &PreviousBlendParam );
	const int32 TargetBlend = Data.BlendMode == static_cast<int32>( EDrawBlend::Add ) ? DX_BLENDMODE_ADD : DX_BLENDMODE_ALPHA;
	SetDrawBlendMode( TargetBlend, 255 );
	// 対象meshごとの元native culling値。
	for ( int32 MeshIndex = 0; bNormalCategoryVisible && MeshIndex < Geometry->Meshes.Num(); ++MeshIndex )
	{
		// 不変形状resourceとnative状態を対応させる要素。
		const FDxLibModelMaterialGeometry::FMesh& Mesh = Geometry->Meshes[MeshIndex];
		if ( !VisibilityRestore.WasVisible( Mesh.SourceMeshIndex ) || FrameVisibility[MeshIndex] == 0 ) continue;
		// 元meshの裏面設定。
		const int32 NativeCull = NativeCullModes[MeshIndex];
		for ( const FMeshGeometryChunk& Chunk : Mesh.Chunks )
		{
			// chunk数を反映した描画用材質値。
			FDrawMaterialMeshData MeshData = Data.Material;
			MeshData.NumVertices = Chunk.Vertices.Num();
			MeshData.NumIndices = Chunk.Indices.Num();
			// 現在chunkのGPU描画が成功したかを示す結果。
			const bool bDrawSucceeded = EffectMaterialRenderer.DrawModelMesh( MeshData, Chunk.Vertices.GetData(), Chunk.Indices.GetData(), World, NativeCull, Data.Opacity );
			if ( !bDrawSucceeded && !bModelMaterialDrawFailedLogged )
			{
				LOG( LogModelMaterialCommand, Warning, "モデル材質効果のGPU描画に失敗しました (instance %d, geometry %d, mesh %d, source mesh %d)。このchunkは描画されません。", Data.Handle, Data.Geometry, MeshIndex, Mesh.SourceMeshIndex );
				bModelMaterialDrawFailedLogged = true;
			}
		}
	}
	SetDrawBlendMode( PreviousBlend, PreviousBlendParam );
}
