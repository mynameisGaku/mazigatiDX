// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "Library/Framework/Render/DrawContext.h"
#include "WorldEffectMeshCommandSelfTest.h"
#include "WorldSelfTestSupport.h"

namespace
{
	// 同じ有効な頂点meshを各命令検証で作る。
	void MakeValidMesh( FDrawMaterialMeshData& Data, TArray<FMeshVertex>& Vertices, TArray<uint32>& Indices )
	{
		Data = FDrawMaterialMeshData();
		Data.Texture = 31;
		Data.NumVertices = 4;
		Data.NumIndices = 6;
		Data.Material.MaskTexture = 14;
		Data.Material.UVScale[0] = 1.25f;
		Data.Material.UVScale[1] = 0.75f;
		Data.Material.MaskScale = 2.0f;
		Data.Material.EdgeWidth = 0.125f;
		Data.Material.EdgeEmission = 3.0f;
		Data.Material.EdgeColor = { 10, 20, 30, 40 };
		Data.Material.bDissolve = 1;
		Data.UVOffset[0] = -0.125f;
		Data.UVOffset[1] = 0.25f;
		Data.Dissolve = 0.375f;
		Data.Emission = 2.5f;
		Data.DistortionTexture = 29;
		Data.DistortionStrength = 0.5f;
		Data.DistortionUVScale[0] = 0.8f;
		Data.DistortionUVScale[1] = 1.4f;
		Data.DistortionUVOffset[0] = 0.1f;
		Data.DistortionUVOffset[1] = -0.2f;
		Data.bTwoSided = 1;
		Vertices.SetNum( 4 );
		Vertices[0].Position[0] = -1.0f;
		Vertices[0].Position[1] = 0.0f;
		Vertices[0].Position[2] = 2.0f;
		Vertices[0].U = 0.0f;
		Vertices[0].V = 0.0f;
		Vertices[0].Color[0] = 20;
		Vertices[0].Color[1] = 30;
		Vertices[0].Color[2] = 40;
		Vertices[0].Color[3] = 50;
		Vertices[1].Position[0] = 1.0f;
		Vertices[1].Position[1] = 0.0f;
		Vertices[1].Position[2] = 2.0f;
		Vertices[1].U = 1.0f;
		Vertices[1].V = 0.0f;
		Vertices[1].Color[0] = 60;
		Vertices[1].Color[1] = 70;
		Vertices[1].Color[2] = 80;
		Vertices[1].Color[3] = 90;
		Vertices[2].Position[0] = 1.0f;
		Vertices[2].Position[1] = 2.0f;
		Vertices[2].Position[2] = 2.0f;
		Vertices[2].U = 1.0f;
		Vertices[2].V = 1.0f;
		Vertices[2].Color[0] = 100;
		Vertices[2].Color[1] = 110;
		Vertices[2].Color[2] = 120;
		Vertices[2].Color[3] = 130;
		Vertices[3].Position[0] = -1.0f;
		Vertices[3].Position[1] = 2.0f;
		Vertices[3].Position[2] = 2.0f;
		Vertices[3].U = 0.0f;
		Vertices[3].V = 1.0f;
		Vertices[3].Color[0] = 140;
		Vertices[3].Color[1] = 150;
		Vertices[3].Color[2] = 160;
		Vertices[3].Color[3] = 170;
		Indices = { 0, 1, 2, 0, 2, 3 };
	}

	// 記録列から対象型の個数と最後の命令を得る。
	int32 CountCommands( const FDrawBuffer& Buffer, EDrawCommand Type, FDrawCommandView& OutCommand )
	{
		// 指定型の命令数。
		int32 Count = 0;
		if ( Buffer.Words.empty() ) return Count;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != Type ) return;
			OutCommand = Command;
			++Count;
		} );
		return Count;
	}

	// 未対応実行先の能力既定値を調べる最小実装。
	class FUnsupportedMeshExecutor final : public IDrawExecutor
	{
	public:
		// 試験用実行先は命令を処理しない。
		void Execute( const FDrawSpan& Span ) override { (void)Span; }
	};

	// 材質meshの命令形式・原子的記録・既存形式保持を確認する。
	void TestMeshCommand( NAssetTest::FChecker& C )
	{
		C.Group = "material-mesh-command";
		C.Check( sizeof( FDrawMaterialMeshData ) == 96 && std::is_trivially_copyable_v<FDrawMaterialMeshData> && sizeof( FMeshVertex ) == 36, "固定部96byteと頂点36byteをtrivially-copyable payloadに保つ" );
		C.Check( static_cast<uint8>( EDrawCommand::Mesh3D ) == 19 && static_cast<uint8>( EDrawCommand::Ribbon3D ) == 20 && static_cast<uint8>( EDrawCommand::MaterialRibbon3D ) == 22 && static_cast<uint8>( EDrawCommand::DistortedMaterialRibbon3D ) == 24 && static_cast<uint8>( EDrawCommand::MaterialMesh3D ) == 25, "既存描画番号を変えずMaterialMesh3Dだけ末尾に追加する" );
		// 能力未実装の既定実行先。
		FUnsupportedMeshExecutor Executor;
		C.Check( !Executor.SupportsEffectMaterialMesh(), "未対応の描画実行先は材質mesh能力をfalseで返す" );
		// 有効な平面meshの固定設定と配列。
		FDrawMaterialMeshData Data;
		// 平面の4頂点。
		TArray<FMeshVertex> Vertices;
		// 平面を構成する6 index。
		TArray<uint32> Indices;
		MakeValidMesh( Data, Vertices, Indices );
		C.Check( NDrawMaterialMesh::IsValidHeader( Data ) && NDrawMaterialMesh::IsValid( Data, Vertices.GetData(), Indices.GetData() ), "有効な平面材質meshを検証する" );
		// 平面命令を所有記録するbuffer。
		FDrawBuffer Buffer;
		// 所有記録を行う窓口。
		FDrawContext Draw( Buffer );
		Draw.MaterialMesh3D( Data, Vertices.GetData(), Indices.GetData() );
		// 記録後に固定部と可変payloadを読むview。
		FDrawCommandView Recorded;
		// 新形式が記録された数。
		const int32 RecordedCount = CountCommands( Buffer, EDrawCommand::MaterialMesh3D, Recorded );
		// header24wordと頂点・index全体の予測長。
		const int32 ExpectedWords = static_cast<int32>( sizeof( Data ) / 4 + Vertices.Num() * sizeof( FMeshVertex ) / 4 + Indices.Num() );
		C.Check( RecordedCount == 1 && Recorded.NumDataWords == ExpectedWords, "材質meshはheader・全頂点・全indexを正確な長さで記録する（count=%d words=%d expected=%d）", RecordedCount, Recorded.NumDataWords, ExpectedWords );
		if ( RecordedCount == 1 && Recorded.NumDataWords == ExpectedWords )
		{
			// command内のheaderを値として読み戻す。
			const FDrawMaterialMeshData StoredData = Recorded.Read<FDrawMaterialMeshData>();
			// 固定部直後の頂点byte列。
			const uint32* StoredVertices = Recorded.Data + sizeof( StoredData ) / 4;
			// 頂点列に続くindex byte列。
			const uint32* StoredIndices = StoredVertices + Vertices.Num() * sizeof( FMeshVertex ) / 4;
			C.Check( std::memcmp( &StoredData, &Data, sizeof( Data ) ) == 0 && std::memcmp( StoredVertices, Vertices.GetData(), static_cast<size_t>( Vertices.Num() ) * sizeof( FMeshVertex ) ) == 0 && std::memcmp( StoredIndices, Indices.GetData(), static_cast<size_t>( Indices.Num() ) * sizeof( uint32 ) ) == 0, "材質mesh命令は固定設定・world頂点・indexを入力どおりにコピーする" );
		}
		// 入力元を変えても記録済み命令が保持する全word。
		const std::vector<uint32> OwnedSnapshot = Buffer.Words;
		Vertices[0].Position[0] = 99.0f;
		Indices[0] = 3;
		Data.Texture = 777;
		C.Check( Buffer.Words == OwnedSnapshot, "記録後に入力header・頂点・indexを変えても命令payloadを保持する" );
		Vertices.Empty();
		Indices.Empty();
		C.Check( Buffer.Words == OwnedSnapshot, "入力配列を解放しても記録済み命令は全payloadを保持する" );

		// 既存命令を先に積み、新meshの記録前後でbyteが保たれるか調べる。
		// 既存描画payloadを保存するbuffer。
		FDrawBuffer LegacyBuffer;
		// 旧命令を記録する窓口。
		FDrawContext LegacyDraw( LegacyBuffer );
		LegacyDraw.Quad3D( FDrawQuad3DData{} );
		LegacyDraw.Mesh3D( 3, 4, 5, 6, 9, 7, FMatrix4x4::Identity, true );
		// 旧Ribbon命令で比較する2点。
		TArray<FDrawRibbonData::FPoint> LegacyPoints;
		LegacyPoints.SetNum( 2 );
		LegacyDraw.Ribbon3D( LegacyPoints.GetData(), LegacyPoints.Num(), 8, 1.0f );
		// 新命令追記前の既存payload全体。
		const std::vector<uint32> LegacySnapshot = LegacyBuffer.Words;
		MakeValidMesh( Data, Vertices, Indices );
		LegacyDraw.MaterialMesh3D( Data, Vertices.GetData(), Indices.GetData() );
		// 旧命令のwordが追記後も一致するか。
		const bool bLegacyBytesUnchanged = LegacyBuffer.Words.size() >= LegacySnapshot.size() && std::memcmp( LegacyBuffer.Words.data(), LegacySnapshot.data(), LegacySnapshot.size() * sizeof( uint32 ) ) == 0;
		C.Check( bLegacyBytesUnchanged, "新mesh命令を追記しても既存Quad・Mesh・Ribbon命令の全wordを変えない" );

		// 不正後半indexを既存命令付きbufferで拒否する。
		// 失敗時も保持する既存描画buffer。
		FDrawBuffer AtomicBuffer;
		// 不正meshを試す記録窓口。
		FDrawContext AtomicDraw( AtomicBuffer );
		AtomicDraw.Box( 1, 2, 3, 4, FColor32::White );
		// 不正入力の前後で比較する既存word列。
		const std::vector<uint32> BeforeInvalid = AtomicBuffer.Words;
		MakeValidMesh( Data, Vertices, Indices );
		Indices[Indices.Num() - 1] = static_cast<uint32>( Vertices.Num() );
		AtomicDraw.MaterialMesh3D( Data, Vertices.GetData(), Indices.GetData() );
		C.Check( AtomicBuffer.Words == BeforeInvalid, "最後のindexが範囲外ならbufferへ一部も追加しない" );
		MakeValidMesh( Data, Vertices, Indices );
		Vertices[Vertices.Num() - 1].U = 1.0e6f + 1.0f;
		AtomicDraw.MaterialMesh3D( Data, Vertices.GetData(), Indices.GetData() );
		C.Check( AtomicBuffer.Words == BeforeInvalid, "最後の頂点UVが上限外ならbufferへ一部も追加しない" );
		MakeValidMesh( Data, Vertices, Indices );
		Vertices[Vertices.Num() - 1].Normal[1] = std::numeric_limits<float32>::quiet_NaN();
		AtomicDraw.MaterialMesh3D( Data, Vertices.GetData(), Indices.GetData() );
		C.Check( AtomicBuffer.Words == BeforeInvalid, "最後の頂点に非有限法線があればbufferへ一部も追加しない" );

		// 最大数のmeshを作り、word上限内で全配列を受け付ける。
		// 最大payloadを検査する固定header。
		FDrawMaterialMeshData MaximumData;
		// 最大上限まで埋める頂点配列。
		TArray<FMeshVertex> MaximumVertices;
		// 最大上限まで埋める三角形index配列。
		TArray<uint32> MaximumIndices;
		MaximumData.Texture = 5;
		MaximumData.NumVertices = FDrawMaterialMeshData::MaxVertices;
		MaximumData.NumIndices = FDrawMaterialMeshData::MaxIndices;
		MaximumVertices.SetNum( MaximumData.NumVertices );
		MaximumIndices.SetNum( MaximumData.NumIndices );
		// 有限な位置を全頂点へ入れる添字。
		for ( int32 Index = 0; Index < MaximumVertices.Num(); ++Index ) MaximumVertices[Index].Position[0] = static_cast<float32>( Index );
		// 範囲内indexを最大配列へ順に入れる添字。
		for ( int32 Index = 0; Index < MaximumIndices.Num(); ++Index ) MaximumIndices[Index] = static_cast<uint32>( Index % MaximumVertices.Num() );
		C.Check( NDrawMaterialMesh::IsValid( MaximumData, MaximumVertices.GetData(), MaximumIndices.GetData() ), "1024頂点と3072 indexの最大payloadを受け付ける" );
		// 最大命令を保存するword buffer。
		FDrawBuffer MaximumBuffer;
		// 最大命令を書き込む窓口。
		FDrawContext MaximumDraw( MaximumBuffer );
		MaximumDraw.MaterialMesh3D( MaximumData, MaximumVertices.GetData(), MaximumIndices.GetData() );
		// 最大命令を数え、word長を得るview。
		FDrawCommandView MaximumCommand;
		// 記録された最大mesh命令の数。
		const int32 MaximumCount = CountCommands( MaximumBuffer, EDrawCommand::MaterialMesh3D, MaximumCommand );
		C.Check( MaximumCount == 1 && MaximumCommand.NumDataWords + 1 <= NDrawCommand::MaxWords && MaximumCommand.NumDataWords == static_cast<int32>( sizeof( MaximumData ) / 4 + MaximumVertices.Num() * sizeof( FMeshVertex ) / 4 + MaximumIndices.Num() ), "最大meshも全payloadを16bit命令長内へ一括記録する（count=%d words=%d）", MaximumCount, MaximumCommand.NumDataWords );

		// headerの代表的な破損値を一つずつ拒否する。
		Data.Texture = -1;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "textureの負handleを拒否する" );
		MakeValidMesh( Data, Vertices, Indices );
		C.Check( !NDrawMaterialMesh::IsValid( Data, nullptr, Indices.GetData() ) && !NDrawMaterialMesh::IsValid( Data, Vertices.GetData(), nullptr ), "頂点またはindex配列がnullなら拒否する" );
		Data.NumVertices = FDrawMaterialMeshData::MaxVertices + 1;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "頂点数上限を超えるheaderをpayload読取り前に拒否する" );
		MakeValidMesh( Data, Vertices, Indices );
		Data.NumIndices = FDrawMaterialMeshData::MaxIndices + 3;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "index数上限を超えるheaderをpayload読取り前に拒否する" );
		MakeValidMesh( Data, Vertices, Indices );
		Data.NumIndices = 4;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "三角形でないindex数を拒否する" );
		MakeValidMesh( Data, Vertices, Indices );
		Data.bTwoSided = 2;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "bTwoSidedの非0/1値を拒否する" );
		MakeValidMesh( Data, Vertices, Indices );
		Data.DistortionStrength = 0.25f;
		Data.DistortionTexture = -1;
		C.Check( !NDrawMaterialMesh::IsValidHeader( Data ), "正の歪み強度にnoise handleがない設定を拒否する" );
	}
}

// 材質mesh命令の検証、所有コピー、上限と旧命令のbyte保持を確認する。
void NWorldTest::RunEffectMeshCommandTests( NAssetTest::FChecker& C )
{
	TestMeshCommand( C );
}
