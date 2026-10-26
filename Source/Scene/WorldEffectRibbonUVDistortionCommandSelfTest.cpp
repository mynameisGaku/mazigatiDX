// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/DrawContext.h"
#include "WorldSelfTestSupport.h"

namespace
{
	// 有効な2f帯設定と点列を、各テストで同じ初期値から作る。
	void MakeValidRibbon( FDrawDistortedMaterialRibbonData& Data, TArray<FDrawDistortedMaterialRibbonData::FPoint>& Points, int32 NumPoints )
	{
		// 既定値から作り直した固定設定。
		Data = FDrawDistortedMaterialRibbonData();
		Data.Material.Ribbon.Texture = 17;
		Data.Material.Ribbon.NumPoints = NumPoints;
		Data.Material.Ribbon.UVRepeat = 1.0f;
		Data.DistortionTexture = 29;
		Data.UVScale[0] = 1.25f;
		Data.UVScale[1] = 0.75f;
		// 要求された数だけ確保した経路点列。
		Points.SetNum( NumPoints );
		// 全点へ有限な基本値を設定する添字。
		for ( int32 Index = 0; Index < NumPoints; ++Index )
		{
			// 経路に沿って少しずつ進む、有限な検証用の点。
			FDrawDistortedMaterialRibbonData::FPoint& Point = Points[Index];
			Point.Material.Path.Pos[0] = static_cast<float32>( Index );
			Point.Material.Path.Pos[1] = static_cast<float32>( Index % 2 );
			Point.Material.Path.Width = 0.5f + static_cast<float32>( Index ) * 0.001f;
			Point.Material.Path.U = static_cast<float32>( Index ) * 0.0005f;
			Point.Material.Path.Color = 0xA04080FFu + static_cast<uint32>( Index );
			Point.Material.Dissolve = 0.25f + 0.0001f * static_cast<float32>( Index );
			Point.Material.Emission = 1.5f + 0.001f * static_cast<float32>( Index );
			Point.Material.UVOffset[0] = -0.00025f * static_cast<float32>( Index );
			Point.Material.UVOffset[1] = 0.000125f * static_cast<float32>( Index );
			Point.Strength = 0.5f;
			Point.UVOffset[0] = 0.00025f * static_cast<float32>( Index );
			Point.UVOffset[1] = -0.000125f * static_cast<float32>( Index );
		}
	}

	// 記録列から対象型の個数と最後に見た命令を取得する。
	int32 CountCommands( const FDrawBuffer& Buffer, EDrawCommand Type, FDrawCommandView& OutCommand )
	{
		// 指定型の命令を数える累積値。
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

	// 記録窓口の検証・所有コピー・旧命令互換を確認する。
	void TestDistortedRibbonRecording( NAssetTest::FChecker& C )
	{
		// この検査群を識別する名前。
		C.Group = "ribbon-uv-distortion-command";
		C.Check( sizeof( FDrawDistortedMaterialRibbonData ) == 68 && sizeof( FDrawDistortedMaterialRibbonData::FPoint ) == 52, "2f固定部68byteと各点52byteの命令配置を保つ" );
		C.Check( static_cast<uint8>( EDrawCommand::Ribbon3D ) == 20 && static_cast<uint8>( EDrawCommand::MaterialRibbon3D ) == 22 && static_cast<uint8>( EDrawCommand::DistortedMaterialQuad3D ) == 23 && static_cast<uint8>( EDrawCommand::DistortedMaterialRibbon3D ) == 24, "旧Ribbon命令番号を維持し新命令だけ末尾へ加える" );
		// 4点のうち両端を接線用補助点にして、中央の経路だけを描く設定。
		FDrawDistortedMaterialRibbonData Data;
		// 固定設定と一緒に記録する入力点列。
		TArray<FDrawDistortedMaterialRibbonData::FPoint> Points;
		MakeValidRibbon( Data, Points, 4 );
		Data.Material.Ribbon.JoinFlags = FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint;
		C.Check( NDrawDistortedMaterialRibbon::IsValid( Data, Points.GetData() ), "前後の接線用補助点を含む4点から中央1区間を記録できる" );
		// 成功した1命令の記録先と窓口。
		FDrawBuffer Buffer;
		// 新しい帯命令を記録する窓口。
		FDrawContext Draw( Buffer );
		Draw.DistortedMaterialRibbon3D( Data, Points.GetData() );
		// 記録列から新命令を読むための結果。
		FDrawCommandView Recorded;
		// 新しい帯命令が記録された回数。
		const int32 RecordedCount = CountCommands( Buffer, EDrawCommand::DistortedMaterialRibbon3D, Recorded );
		// 固定部と4点分のpayloadに必要なword数。
		const int32 ExpectedDataWords = static_cast<int32>( sizeof( FDrawDistortedMaterialRibbonData ) / 4 + sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4 * 4 );
		C.Check( RecordedCount == 1 && Recorded.NumDataWords == ExpectedDataWords, "新命令は固定部と全点を正確な長さで一括記録する（count=%d words=%d expected=%d）", RecordedCount, Recorded.NumDataWords, ExpectedDataWords );
		if ( RecordedCount == 1 && Recorded.NumDataWords == ExpectedDataWords )
		{
			// 命令固定部を所有値へ読み取る。
			const FDrawDistortedMaterialRibbonData StoredHeader = Recorded.Read<FDrawDistortedMaterialRibbonData>();
			// 後続payloadが一致するかを示す結果。
			const bool bPointsMatch = std::memcmp( Recorded.Data + sizeof( StoredHeader ) / 4, Points.GetData(), static_cast<size_t>( Points.Num() ) * sizeof( Points[0] ) ) == 0;
			C.Check( std::memcmp( &StoredHeader, &Data, sizeof( Data ) ) == 0 && bPointsMatch, "新命令は固定部と全52byte点を値どおりにコピーする" );
		}

		// 型付きで所有する入力をもう一度記録し、先行命令と追加命令のbyteを比較する。
		const std::vector<uint32> FirstCommandWords = Buffer.Words;
		Draw.DistortedMaterialRibbon3D( Data, Points.GetData() );
		// 2命令の入力一致を調べる走査結果。
		int32 RepeatedCount = 0;
		// 両命令の固定設定と点列が所有入力に一致する状態。
		bool bRepeatedBytesMatch = true;
		if ( !Buffer.Words.empty() )
		{
			ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
			{
				if ( Command.Type != EDrawCommand::DistortedMaterialRibbon3D ) return;
				++RepeatedCount;
				if ( Command.NumDataWords != ExpectedDataWords ) { bRepeatedBytesMatch = false; return; }
				// 記録済みheaderと点列を型付きの独立入力と比較する。
				const FDrawDistortedMaterialRibbonData CurrentHeader = Command.Read<FDrawDistortedMaterialRibbonData>();
				bRepeatedBytesMatch = bRepeatedBytesMatch && std::memcmp( &CurrentHeader, &Data, sizeof( Data ) ) == 0 && std::memcmp( Command.Data + sizeof( CurrentHeader ) / 4, Points.GetData(), static_cast<size_t>( Points.Num() ) * sizeof( Points[0] ) ) == 0;
			} );
		}
		// 追記が既存のword列を変えず、2命令とも同じ所有入力を保存したか調べる。
		const bool bFirstCommandUnchanged = Buffer.Words.size() >= FirstCommandWords.size() && std::memcmp( Buffer.Words.data(), FirstCommandWords.data(), FirstCommandWords.size() * sizeof( uint32 ) ) == 0;
		C.Check( RepeatedCount == 2 && bRepeatedBytesMatch && bFirstCommandUnchanged, "独立した型付き入力を再記録しても両命令の全byteを保つ" );

		// 2..1024点と8192反復区間の境界を確認する。
		// 上限点数とUV反復数を検証する固定設定。
		FDrawDistortedMaterialRibbonData BoundaryData;
		// 上限検査に使う経路点列。
		TArray<FDrawDistortedMaterialRibbonData::FPoint> BoundaryPoints;
		MakeValidRibbon( BoundaryData, BoundaryPoints, FDrawRibbonData::MaxPoints );
		BoundaryData.Material.Ribbon.UVRepeat = 1.0f;
		BoundaryPoints[BoundaryPoints.Num() - 1].Material.Path.U = 1.0f;
		C.Check( NDrawDistortedMaterialRibbon::IsValid( BoundaryData, BoundaryPoints.GetData() ), "1024点上限の全payloadを受け付ける" );
		// 上限点数を記録するcommand buffer。
		FDrawBuffer MaximumBuffer;
		// 上限点数用の記録窓口。
		FDrawContext MaximumDraw( MaximumBuffer );
		MaximumDraw.DistortedMaterialRibbon3D( BoundaryData, BoundaryPoints.GetData() );
		// 上限点payloadの記録位置。
		FDrawCommandView MaximumCommand;
		// 上限点数命令の記録数。
		const int32 MaximumCount = CountCommands( MaximumBuffer, EDrawCommand::DistortedMaterialRibbon3D, MaximumCommand );
		C.Check( MaximumCount == 1 && MaximumCommand.NumDataWords == static_cast<int32>( sizeof( FDrawDistortedMaterialRibbonData ) / 4 + FDrawRibbonData::MaxPoints * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4 ) && MaximumCommand.NumDataWords + 1 <= NDrawCommand::MaxWords, "1024点payloadが16bit命令長内へ全点入りする（count=%d words=%d）", MaximumCount, MaximumCommand.NumDataWords );
		// 反復境界ぴったりの2点帯。
		MakeValidRibbon( BoundaryData, BoundaryPoints, 2 );
		BoundaryPoints[0].Material.Path.U = 0.0f;
		BoundaryPoints[1].Material.Path.U = 1.0f;
		BoundaryData.Material.Ribbon.UVRepeat = 8192.0f;
		C.Check( NDrawDistortedMaterialRibbon::IsValid( BoundaryData, BoundaryPoints.GetData() ), "8192画像区間ちょうどを受け付ける" );
		BoundaryData.Material.Ribbon.UVRepeat = 8193.0f;
		C.Check( !NDrawDistortedMaterialRibbon::IsValid( BoundaryData, BoundaryPoints.GetData() ), "8193画像区間は上限超過として拒否する" );
		// 派生UVのfloat積が有限範囲を超える設定。
		BoundaryData.Material.Ribbon.UVRepeat = 1.0f;
		BoundaryPoints[1].Material.Path.U = TNumericLimits<float32>::Max();
		BoundaryData.UVScale[0] = 128.0f;
		C.Check( !NDrawDistortedMaterialRibbon::IsValid( BoundaryData, BoundaryPoints.GetData() ), "有限入力でも派生ノイズUVがfloat上限を超える帯を拒否する" );

		// 不正な後半点でも既存bufferを途中変更しない。
		// 既存命令を保持する語列。
		FDrawBuffer AtomicBuffer;
		// 既存語列へ追加する記録窓口。
		FDrawContext AtomicDraw( AtomicBuffer );
		AtomicDraw.Box( 1, 2, 3, 4, FColor32::White );
		// 不正入力で保持される既存命令列。
		const std::vector<uint32> BeforeInvalid = AtomicBuffer.Words;
		MakeValidRibbon( Data, Points, 3 );
		Points[2].UVOffset[1] = TNumericLimits<float32>::QuietNaN();
		AtomicDraw.DistortedMaterialRibbon3D( Data, Points.GetData() );
		C.Check( AtomicBuffer.Words == BeforeInvalid, "後半点の非有限UVで命令を原子的に拒否し既存wordを変えない" );

		// 未知の補助点指定、可視点不足、点数超過、破損textureを個別に拒否する。
		MakeValidRibbon( Data, Points, 3 );
		Data.Material.Ribbon.JoinFlags = 4;
		C.Check( !NDrawDistortedMaterialRibbon::IsValidHeader( Data ), "未知の補助点指定を拒否する" );
		Data.Material.Ribbon.JoinFlags = FDrawRibbonData::PreviousPoint;
		Data.Material.Ribbon.NumPoints = 2;
		C.Check( !NDrawDistortedMaterialRibbon::IsValidHeader( Data ), "接線用補助点を除くと点が1個しか残らない指定を拒否する" );
		Data.Material.Ribbon.NumPoints = TNumericLimits<int32>::Min();
		// 壊れた点数を持つ入力先。
		FDrawBuffer MalformedBuffer;
		// 不正headerを安全に拒否する記録窓口。
		FDrawContext MalformedDraw( MalformedBuffer );
		MalformedDraw.DistortedMaterialRibbon3D( Data, Points.GetData() );
		C.Check( MalformedBuffer.Words.empty(), "INT_MIN点数を減算前に拒否する" );
		MakeValidRibbon( Data, Points, 3 );
		Data.DistortionTexture = -1;
		C.Check( !NDrawDistortedMaterialRibbon::IsValidHeader( Data ), "ノイズ画像handleの負値を拒否する" );
		Data.DistortionTexture = 29;
		Data.Material.Ribbon.NumPoints = FDrawRibbonData::MaxPoints + 1;
		// 点数超過前の既存語列。
		const std::vector<uint32> BeforeTooMany = MalformedBuffer.Words;
		MalformedDraw.DistortedMaterialRibbon3D( Data, Points.GetData() );
		C.Check( MalformedBuffer.Words == BeforeTooMany, "1025点をpayload読取り前に拒否する" );

		// 従来のRibbonと材質Ribbonの番号・固定部・非空点列を保つ。
		// 旧命令を記録する語列。
		FDrawBuffer LegacyBuffer;
		// 両方の既存Ribbon APIを呼ぶ窓口。
		FDrawContext LegacyDraw( LegacyBuffer );
		TArray<FDrawRibbonData::FPoint> LegacyPoints;
		LegacyPoints.SetNum( 3 );
		// 既知値を各点へ割り当てる添字。
		for ( int32 Index = 0; Index < LegacyPoints.Num(); ++Index )
		{
			// 旧帯命令の既知座標・色を設定する点。
			LegacyPoints[Index].Pos[0] = static_cast<float32>( Index );
			LegacyPoints[Index].Pos[1] = 0.5f * static_cast<float32>( Index );
			LegacyPoints[Index].Width = 0.25f + static_cast<float32>( Index );
			LegacyPoints[Index].U = 0.5f * static_cast<float32>( Index );
			LegacyPoints[Index].Color = 0xFF804020u + static_cast<uint32>( Index );
		}
		LegacyDraw.Ribbon3D( LegacyPoints.GetData(), LegacyPoints.Num(), 41, 1.5f, 0 );
		// 旧材質帯の既知固定値。
		FDrawMaterialRibbonData LegacyMaterial;
		LegacyMaterial.Ribbon.Texture = 41;
		LegacyMaterial.Ribbon.NumPoints = LegacyPoints.Num();
		LegacyMaterial.Ribbon.UVRepeat = 1.5f;
		TArray<FDrawMaterialRibbonData::FPoint> LegacyMaterialPoints;
		LegacyMaterialPoints.SetNum( LegacyPoints.Num() );
		// 旧経路を複写して材質値を加える添字。
		for ( int32 Index = 0; Index < LegacyMaterialPoints.Num(); ++Index )
		{
			// 旧材質帯点の既存経路と非既定材質値。
			LegacyMaterialPoints[Index].Path = LegacyPoints[Index];
			LegacyMaterialPoints[Index].Dissolve = 0.25f * static_cast<float32>( Index );
			LegacyMaterialPoints[Index].Emission = 1.5f + static_cast<float32>( Index );
			LegacyMaterialPoints[Index].UVOffset[0] = 0.125f * static_cast<float32>( Index );
		}
		LegacyMaterial.Material.MaskTexture = 53;
		LegacyMaterial.Material.EdgeColor = { 10, 20, 30, 40 };
		LegacyDraw.MaterialRibbon3D( LegacyMaterial, LegacyMaterialPoints.GetData() );
		// 旧Ribbon命令の記録数。
		int32 LegacyRibbonCount = 0;
		// 旧材質Ribbon命令の記録数。
		int32 LegacyMaterialCount = 0;
		// 両命令のpayloadが既知入力と一致するか。
		bool bLegacyPayloadMatch = true;
		// 2種類の旧命令を記録順に調べる走査。
		if ( !LegacyBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ LegacyBuffer.Words.data(), static_cast<int32>( LegacyBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Ribbon3D )
			{
				++LegacyRibbonCount;
				// 旧固定部の値と後続24byte点列を比較する。
				FDrawRibbonData Expected;
				Expected.Texture = 41;
				Expected.NumPoints = LegacyPoints.Num();
				Expected.UVRepeat = 1.5f;
				bLegacyPayloadMatch = bLegacyPayloadMatch && Command.NumDataWords == static_cast<int32>( sizeof( Expected ) / 4 + LegacyPoints.Num() * sizeof( LegacyPoints[0] ) / 4 ) && std::memcmp( Command.Data, &Expected, sizeof( Expected ) ) == 0 && std::memcmp( Command.Data + sizeof( Expected ) / 4, LegacyPoints.GetData(), static_cast<size_t>( LegacyPoints.Num() ) * sizeof( LegacyPoints[0] ) ) == 0;
			}
			if ( Command.Type == EDrawCommand::MaterialRibbon3D )
			{
				++LegacyMaterialCount;
				// 旧材質固定部と後続40byte点列を比較する。
				bLegacyPayloadMatch = bLegacyPayloadMatch && Command.NumDataWords == static_cast<int32>( sizeof( LegacyMaterial ) / 4 + LegacyMaterialPoints.Num() * sizeof( LegacyMaterialPoints[0] ) / 4 ) && std::memcmp( Command.Data, &LegacyMaterial, sizeof( LegacyMaterial ) ) == 0 && std::memcmp( Command.Data + sizeof( LegacyMaterial ) / 4, LegacyMaterialPoints.GetData(), static_cast<size_t>( LegacyMaterialPoints.Num() ) * sizeof( LegacyMaterialPoints[0] ) ) == 0;
			}
		} );
		C.Check( LegacyRibbonCount == 1 && LegacyMaterialCount == 1 && bLegacyPayloadMatch, "旧Ribbon3DとMaterialRibbon3Dの非空payloadは番号・長さ・全byteが一致する" );
	}
}

// 2fの可変長描画命令、検証境界、旧Ribbon命令のbyte互換を確認する。
void NWorldTest::RunEffectRibbonUVDistortionCommandTests( NAssetTest::FChecker& C )
{
	TestDistortedRibbonRecording( C );
}
