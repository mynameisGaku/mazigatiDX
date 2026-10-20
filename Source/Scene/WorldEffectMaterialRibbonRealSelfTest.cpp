// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>
#include <functional>
#include <limits>

#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibRibbonMesh.h"
#include "Library/Platform/FileSystem.h"

namespace NWorldTest
{
	// 共通native sentinel資源で材質帯の描画と状態復元を試験する。
	void RunEffectMaterialRibbonNativeStateRestore( NAssetTest::FChecker& C, IDrawExecutor& Executor, const FDrawMaterialQuadData& SentinelQuad, int32 Soft, const std::function<void()>& DrawCallback );
}

namespace
{
	// 点ごとの形状・色・消失・発光・UV移動を一度に設定する試験用値。
	FDrawMaterialRibbonData::FPoint MakePoint( float32 X, float32 Y, float32 U, uint32 Color, float32 Dissolve = 0.0f, float32 Emission = 1.0f, float32 OffsetU = 0.0f, float32 OffsetV = 0.0f )
	{
		FDrawMaterialRibbonData::FPoint Point;
		Point.Path.Pos[0] = X; Point.Path.Pos[1] = Y; Point.Path.Pos[2] = 0.0f;
		Point.Path.Width = 0.5f; Point.Path.U = U; Point.Path.Color = Color;
		Point.Dissolve = Dissolve; Point.Emission = Emission;
		Point.UVOffset[0] = OffsetU; Point.UVOffset[1] = OffsetV;
		return Point;
	}

	// 指定した一色を埋めた8x8のDxLib graphを作る。
	int32 MakeSolidGraph( uint8 R, uint8 G, uint8 B, uint8 A )
	{
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, R, G, B, A );
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// U方向で赤青を分けた8x8画像を作る。
	int32 MakeSplitGraph()
	{
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 255 : 0, 0, X < 4 ? 0 : 255, 255 );
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// U方向の左半分を消し、右半分を残す8x8 mask画像を作る。
	int32 MakeHalfMask()
	{
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 0 : 255, 0, 0, 255 );
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// 描画画面を戻し、色を読み取るまでの実機状態を所有する。
	struct FRibbonMaterialCallerState
	{
		// 実機試験を始める前の画面と設定リセット状態。
		int32 Screen = GetDrawScreen();
		int32 Reset = GetUseSetDrawScreenSettingReset();
		// 実機試験を始める前のカメラと変換行列。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		RECT Area{};
		// 実機試験を始める前のブレンド・色・背景。
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// 画面を変える前に、終了時に戻す設定を保存する。
		FRibbonMaterialCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] );
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &Area );
			GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 実機試験で変えた画面・変換・色・ブレンドを呼び出し元へ戻す。
		~FRibbonMaterialCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen );
			SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] );
			SetUseZBuffer3D( FALSE ); SetWriteZBuffer3D( FALSE ); SetUseSetDrawScreenSettingReset( Reset );
		}
	};

	// 記録サイズ・点の所有・不正値拒否を本番DrawContextで確かめる。
	void TestMaterialRibbonRecording( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-ribbon-record";
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		FDrawMaterialRibbonData Data;
		Data.Ribbon.Texture = 7; Data.Ribbon.NumPoints = 2; Data.Ribbon.UVRepeat = 0.5f;
		FDrawMaterialRibbonData::FPoint Points[2] = { MakePoint( -1.0f, 0.0f, 0.0f, 0x80112233u ), MakePoint( 1.0f, 0.0f, 2.0f, 0xFF445566u, 0.75f, 2.0f, 0.9f, -0.1f ) };
		const int32 Begin = Draw.GetNumWords();
		Draw.MaterialRibbon3D( Data, Points );
		C.Check( Draw.GetNumWords() == Begin + 35 && ( Buffer.Words[Begin] & 0xFFu ) == static_cast<uint32>( EDrawCommand::MaterialRibbon3D ) && ( Buffer.Words[Begin] >> 16 ) == 35u, "2点の材質帯は固定14wordと点10wordずつを記録する" );
		// 入力変更後にbufferから読み直す点所有確認の出力。
		FDrawMaterialRibbonData::FPoint Recorded{};
		Points[0].Path.Pos[0] = 99.0f;
		Data.Ribbon.Texture = 99;
		// 入力header変更後にbufferから読み直す所有値。
		FDrawMaterialRibbonData RecordedHeader{};
		std::memcpy( &RecordedHeader, Buffer.Words.data() + Begin + 1, sizeof( RecordedHeader ) );
		std::memcpy( &Recorded, Buffer.Words.data() + Begin + 1 + sizeof( FDrawMaterialRibbonData ) / 4, sizeof( Recorded ) );
		C.Check( RecordedHeader.Ribbon.Texture == 7 && Recorded.Path.Pos[0] == -1.0f && Recorded.Path.Color == 0x80112233u, "headerと点を変更してもbuffer内の記録値は所有コピーを保つ" );
		Points[0] = MakePoint( -1.0f, 0.0f, 0.0f, 0x80112233u );
		FDrawBuffer AliasBuffer;
		AliasBuffer.Words.reserve( 128 ); AliasBuffer.Words.resize( AliasBuffer.Words.capacity() );
		const size_t AliasBegin = AliasBuffer.Words.size();
		const size_t AliasHeader = AliasBegin - sizeof( FDrawMaterialRibbonData ) / 4 - 2 * sizeof( FDrawMaterialRibbonData::FPoint ) / 4;
		std::memcpy( AliasBuffer.Words.data() + AliasHeader, &Data, sizeof( Data ) );
		std::memcpy( AliasBuffer.Words.data() + AliasHeader + sizeof( Data ) / 4, Points, sizeof( Points ) );
		FDrawContext AliasDraw( AliasBuffer );
		const FDrawMaterialRibbonData* AliasedData = reinterpret_cast<const FDrawMaterialRibbonData*>( AliasBuffer.Words.data() + AliasHeader );
		const FDrawMaterialRibbonData::FPoint* AliasedPoints = reinterpret_cast<const FDrawMaterialRibbonData::FPoint*>( AliasBuffer.Words.data() + AliasHeader + sizeof( Data ) / 4 );
		AliasDraw.MaterialRibbon3D( *AliasedData, AliasedPoints );
		FDrawMaterialRibbonData::FPoint AliasRecorded{};
		std::memcpy( &AliasRecorded, AliasBuffer.Words.data() + AliasBegin + 1 + sizeof( Data ) / 4, sizeof( AliasRecorded ) );
		C.Check( AliasBuffer.Words.size() == AliasBegin + 35 && AliasRecorded.Path.Pos[0] == -1.0f, "headerと点が同じ記録bufferを指し、追記で再確保しても先に値を所有する" );
		const int32 BeforeInvalid = Draw.GetNumWords();
		Points[0] = MakePoint( 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu ); Points[1] = MakePoint( 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu );
		Data.Ribbon.NumPoints = 1; Draw.MaterialRibbon3D( Data, Points );
		Data.Ribbon.NumPoints = 2; Data.Material.DepthWrite = 2; Draw.MaterialRibbon3D( Data, Points );
		Data.Material.DepthWrite = 0; Points[1].Dissolve = ( std::numeric_limits<float32>::quiet_NaN )(); Draw.MaterialRibbon3D( Data, Points );
		Points[1].Dissolve = 0.0f; Points[1].Path.U = -1.0f; Draw.MaterialRibbon3D( Data, Points );
		Points[0].Path.U = Points[1].Path.U = 1.0e38f; Data.Material.UVScale[0] = 128.0f; Draw.MaterialRibbon3D( Data, Points );
		C.Check( Draw.GetNumWords() == BeforeInvalid, "点数・深度flag・非有限消失値・負の累積距離を記録前に拒否する" );
	}

	// native D3D11で材質帯を描き、画素とghost分割を検査する。
	void TestMaterialRibbonPixels( NAssetTest::FChecker& C, IDrawExecutor& Executor )
	{
		C.Group = "effect-material-ribbon-real";
		C.Check( Executor.SupportsEffectMaterialRibbons(), "通常のDxLib D3D11実行先は材質付き帯を扱える" );
		if ( !Executor.SupportsEffectMaterialRibbons() ) return;
		const FRibbonMaterialCallerState Saved;
		const int32 Screen = MakeScreen( 128, 128, FALSE );
		const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
		int32 White = MakeSolidGraph( 255, 255, 255, 255 );
		int32 Dim = MakeSolidGraph( 80, 80, 80, 255 );
		int32 HalfAlpha = MakeSolidGraph( 80, 40, 20, 128 );
		int32 Split = MakeSplitGraph();
		int32 Mask = MakeHalfMask();
		int32 Red = MakeSolidGraph( 255, 0, 0, 255 );
		int32 Blue = MakeSolidGraph( 0, 0, 255, 255 );
		int32 Green = MakeSolidGraph( 0, 255, 0, 255 );
		int32 DerivedRight = -1;
		int32 DerivedMaskRight = -1;
		C.Check( Screen >= 0 && Soft >= 0 && White >= 0 && Dim >= 0 && HalfAlpha >= 0 && Split >= 0 && Mask >= 0 && Red >= 0 && Blue >= 0 && Green >= 0, "材質帯用の描画先・readback画像・表示画像とmaskを作れる" );
		if ( Screen < 0 || Soft < 0 || White < 0 || Dim < 0 || HalfAlpha < 0 || Split < 0 || Mask < 0 || Red < 0 || Blue < 0 || Green < 0 )
		{
			if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); if ( White >= 0 ) DeleteGraph( White ); if ( Dim >= 0 ) DeleteGraph( Dim ); if ( HalfAlpha >= 0 ) DeleteGraph( HalfAlpha ); if ( Split >= 0 ) DeleteGraph( Split ); if ( Mask >= 0 ) DeleteGraph( Mask ); if ( Red >= 0 ) DeleteGraph( Red ); if ( Blue >= 0 ) DeleteGraph( Blue ); if ( Green >= 0 ) DeleteGraph( Green );
			return;
		}
		DerivedRight = DerivationGraph( 4, 0, 4, 8, Split );
		DerivedMaskRight = DerivationGraph( 4, 0, 4, 8, Mask );
		C.Check( SetUseGraphZBuffer( Screen, TRUE ) == 0, "深度4組合を試すscreenへZ bufferを割り当てる" );
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport );
		const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity );
		SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE ); SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 0, 0, 0, 255 );
		// helperへ渡す、二点からなる連続Uの基本帯。
		FDrawRibbonData ReuseHeader;
		ReuseHeader.Texture = White; ReuseHeader.NumPoints = 2; ReuseHeader.UVRepeat = 1.0f;
		// helperへ型付きで渡すため所有領域へ複写した経路点。
		FDrawRibbonData::FPoint ReusePoints[2] = {};
		ReusePoints[0].Pos[0] = -2.0f; ReusePoints[0].Width = 0.5f; ReusePoints[0].U = 0.0f; ReusePoints[0].Color = 0xFFFFFFFFu;
		ReusePoints[1].Pos[0] = 2.0f; ReusePoints[1].Width = 0.5f; ReusePoints[1].U = 1.0f; ReusePoints[1].Color = 0xFFFFFFFFu;
		// 二回目のBuildで同じ出力先が初期化され、同じ頂点と番号を作るか確認する。
		FDxLibRibbonMesh ReusedMesh;
		const bool bFirstBuild = NDxLibRibbonMesh::Build( ReuseHeader, ReusePoints, false, ReusedMesh );
		const TArray<VERTEX3D> FirstVertices = ReusedMesh.Vertices;
		const TArray<unsigned short> FirstIndices = ReusedMesh.Indices;
		const bool bSecondBuild = NDxLibRibbonMesh::Build( ReuseHeader, ReusePoints, false, ReusedMesh );
		bool bSameMesh = bFirstBuild && bSecondBuild && FirstVertices.Num() == ReusedMesh.Vertices.Num() && FirstIndices.Num() == ReusedMesh.Indices.Num();
		for ( int32 Index = 0; Index < FirstIndices.Num() && bSameMesh; ++Index ) bSameMesh = FirstIndices[Index] == ReusedMesh.Indices[Index];
		for ( int32 Index = 0; Index < FirstVertices.Num() && bSameMesh; ++Index )
		{
			const VERTEX3D& First = FirstVertices[Index];
			const VERTEX3D& Again = ReusedMesh.Vertices[Index];
			bSameMesh = First.pos.x == Again.pos.x && First.pos.y == Again.pos.y && First.pos.z == Again.pos.z && First.norm.x == Again.norm.x && First.norm.y == Again.norm.y && First.norm.z == Again.norm.z && First.dif.r == Again.dif.r && First.dif.g == Again.dif.g && First.dif.b == Again.dif.b && First.dif.a == Again.dif.a && First.u == Again.u && First.v == Again.v;
		}
		C.Check( bSameMesh && ReusedMesh.Vertices.Num() == 4 && ReusedMesh.Indices.Num() == 6, "同じmesh出力先を再利用しても頂点・三角形番号が累積せず同じ結果になる" );
		const auto Pixel = [&]( int32 X, int32 Y )
		{
			int32 Channels[4] = {};
			GetPixelSoftImage( Soft, X, Y, &Channels[0], &Channels[1], &Channels[2], &Channels[3] );
			return FColor32( static_cast<uint8>( Channels[0] ), static_cast<uint8>( Channels[1] ), static_cast<uint8>( Channels[2] ), static_cast<uint8>( Channels[3] ) );
		};
		const auto SaveProof = [&]( const char* Name )
		{
			const FString Directory = FString( "Saved" ) / "EffectMaterialRibbonProof";
			if ( NFileSystem::EnsureDirectory( Directory ) ) SaveDrawScreenToPNG( 0, 0, 128, 128, *( Directory / FString::FromUtf8( Name ) ) );
		};
		const auto Render = [&]( const FDrawMaterialRibbonData& Material, const FDrawMaterialRibbonData::FPoint* Points, EDrawBlend BlendMode = EDrawBlend::Alpha )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( BlendMode, 255 ); Draw.MaterialRibbon3D( Material, Points );
			if ( !Commands.Words.empty() ) Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		const auto MakeData = [&]( int32 Texture, int32 MaskHandle, int32 Count )
		{
			FDrawMaterialRibbonData Data;
			Data.Ribbon.Texture = Texture; Data.Ribbon.NumPoints = Count; Data.Ribbon.UVRepeat = 1.0f;
			Data.Material.MaskTexture = MaskHandle; Data.Material.DepthTest = 1; Data.Material.DepthWrite = 0;
			return Data;
		};
		FDrawMaterialRibbonData::FPoint Straight[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 0xFF0000FFu ), MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFF0000u ) };
		FDrawMaterialRibbonData Data = MakeData( White, -1, 2);
		Render( Data, Straight );
		C.Check( Pixel( 48, 64 ).r > 100 && Pixel( 80, 64 ).b > 100, "材質帯は生の点RGBAを長手方向へ補間する" );
		// DrawAreaだけを狭めても位置と大きさを保ち、外側だけを切り取る描画関数。
		const auto RenderRibbonArea = [&]( const RECT& Area, const MATRIX& LogicalViewport )
		{
			SetDrawScreen( Screen ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom ); SetTransformToViewport( &LogicalViewport ); ClearDrawScreen();
			FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialRibbon3D( Data, Straight );
			if ( !Commands.Words.empty() ) Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		// native full viewportと同じ論理位置を使うDrawArea比較用行列。
		MATRIX FullViewport{}; CreateViewportMatrix( &FullViewport, 64.0f, 64.0f, 128.0f, 128.0f );
		const RECT TightArea{ 48, 48, 80, 80 };
		RenderRibbonArea( TightArea, FullViewport );
		C.Check( Pixel( 64, 64 ).r > 100 && Pixel( 40, 64 ).r < 8 && Pixel( 88, 64 ).r < 8, "材質帯の狭いDrawAreaは位置と大きさを保って外側だけを切り取る" );
		// 64x64の論理viewportへ切り替えた場合の位置・大きさ。
		MATRIX SmallViewport{}; CreateViewportMatrix( &SmallViewport, 32.0f, 32.0f, 64.0f, 64.0f );
		const RECT FullArea{ 0, 0, 128, 128 };
		RenderRibbonArea( FullArea, SmallViewport );
		C.Check( Pixel( 32, 32 ).r > 100 && Pixel( 32, 32 ).b > 100 && Pixel( 64, 64 ).r < 8, "材質帯は非既定logical viewportの中心へ移り、旧中心は空になる" );
		SetDrawAreaFull(); SetTransformToViewport( &FullViewport );
		Data = MakeData( Dim, -1, 2 ); Straight[0].Emission = Straight[1].Emission = 1.0f; Straight[0].Path.Color = Straight[1].Path.Color = 0xFFFFFFFFu;
		Render( Data, Straight ); const uint8 EmissionOne = Pixel( 80, 64 ).r;
		Straight[0].Emission = Straight[1].Emission = 2.0f; Render( Data, Straight ); const uint8 EmissionTwo = Pixel( 80, 64 ).r;
		C.Check( EmissionTwo > EmissionOne, "点ごとの発光倍率は画像色を変え、LDR上限を越えない" );
		Data = MakeData( Dim, Mask, 2 ); Data.Material.bDissolve = 1; Data.Material.EdgeWidth = 0.2f; Data.Material.EdgeEmission = 4.0f; Data.Material.EdgeColor = { 0, 255, 0, 255 }; Straight[0].Emission = Straight[1].Emission = 1.0f;
		Straight[0].Dissolve = Straight[1].Dissolve = 0.5f;
		Render( Data, Straight );
		C.Check( Pixel( 48, 64 ).r < 8 && Pixel( 80, 64 ).r > 50 && Pixel( 64, 64 ).g > Pixel( 80, 64 ).g, "中間しきい値で実際の穴を開け、生存側だけに境界色を加える" );
		SaveProof( "effect-material-ribbon-hole-edge.png" );
		Straight[0].Dissolve = Straight[1].Dissolve = 0.0f; Render( Data, Straight );
		C.Check( Pixel( 48, 64 ).r >= 75 && Pixel( 48, 64 ).r <= 85 && Pixel( 80, 64 ).r >= 75 && Pixel( 80, 64 ).r <= 85, "しきい値0では境界発光なしで既知source色を全区間に表示する" );
		Straight[0].Dissolve = Straight[1].Dissolve = 1.0f; Render( Data, Straight );
		C.Check( Pixel( 48, 64 ).r < 8 && Pixel( 80, 64 ).r < 8, "しきい値1では全区間を消す" );
		SaveProof( "effect-material-ribbon-full-dissolve.png" );
		Data = MakeData( Split, -1, 2 ); Straight[0].Path.Color = Straight[1].Path.Color = 0xFFFFFFFFu; Straight[0].Dissolve = Straight[1].Dissolve = 0.0f;
		Render( Data, Straight );
		C.Check( Pixel( 48, 64 ).r > 180 && Pixel( 48, 64 ).b < 60 && Pixel( 80, 64 ).b > 180 && Pixel( 80, 64 ).r < 60, "連続Uへsource画像を対応付けて既知の赤青位置を読む" );
		Data.Ribbon.Texture = DerivedRight; Render( Data, Straight );
		C.Check( DerivedRight >= 0 && Pixel( 48, 64 ).b > 180 && Pixel( 80, 64 ).b > 180, "派生source画像はbase画像内の右cropだけを読む" );
		Data.Ribbon.Texture = White; Data.Material.MaskTexture = DerivedMaskRight; Data.Material.bDissolve = 1; Straight[0].Dissolve = Straight[1].Dissolve = 0.5f; Render( Data, Straight );
		C.Check( DerivedMaskRight >= 0 && Pixel( 48, 64 ).r > 180 && Pixel( 80, 64 ).r > 180, "派生mask画像はbase画像内の右cropだけを読む" );
		if ( DerivedRight >= 0 ) { DeleteGraph( DerivedRight ); DerivedRight = -1; }
		if ( DerivedMaskRight >= 0 ) { DeleteGraph( DerivedMaskRight ); DerivedMaskRight = -1; }
		Data = MakeData( Split, -1, 2 ); Data.Material.UVScale[0] = 1.5f; Data.Material.MaskScale = 1.5f; Straight[0].Path.Color = Straight[1].Path.Color = 0xFFFFFFFFu; Straight[0].Emission = Straight[1].Emission = 1.0f; Straight[0].Dissolve = Straight[1].Dissolve = 0.0f; Straight[0].UVOffset[0] = 0.9f; Straight[1].UVOffset[0] = 1.1f;
		Render( Data, Straight );
		// .9から1.1へ連続scrollした既知位置の色。
		const FColor32 PositiveScrollLeft = Pixel( 56, 64 ); const FColor32 PositiveScrollRight = Pixel( 80, 64 );
		C.Check( PositiveScrollLeft.b > PositiveScrollLeft.r && PositiveScrollRight.r > PositiveScrollRight.b, "正の点別scrollと非整数scaleが整数U境界をまたいでsource画像を移動する" );
		// 正のwrap結果を負側の同値offsetと比べるための全画面pixel。
		TArray<FColor32> PositiveScrollPixels; PositiveScrollPixels.SetNum( 128 * 128 );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) PositiveScrollPixels[Y * 128 + X] = Pixel( X, Y );
		Straight[0].UVOffset[0] = Straight[1].UVOffset[0] = 0.5f; Render( Data, Straight );
		C.Check( Pixel( 56, 64 ).r > Pixel( 56, 64 ).b && Pixel( 80, 64 ).b > Pixel( 80, 64 ).r, "非整数source scaleと半周scrollで既知画像色が移る" );
		Straight[0].UVOffset[0] = -0.1f; Straight[1].UVOffset[0] = 0.1f; Render( Data, Straight );
		// 1周期だけ異なるoffsetが同じsource pixelを作る比較結果。
		bool bNegativeWrapMatches = true;
		for ( int32 Y = 0; Y < 128 && bNegativeWrapMatches; ++Y ) for ( int32 X = 0; X < 128; ++X ) { const FColor32 Actual = Pixel( X, Y ); const FColor32 Expected = PositiveScrollPixels[Y * 128 + X]; if ( ( Actual.r > Expected.r ? Actual.r - Expected.r : Expected.r - Actual.r ) > 1 || ( Actual.g > Expected.g ? Actual.g - Expected.g : Expected.g - Actual.g ) > 1 || ( Actual.b > Expected.b ? Actual.b - Expected.b : Expected.b - Actual.b ) > 1 ) { bNegativeWrapMatches = false; break; } }
		C.Check( bNegativeWrapMatches && Pixel( 56, 64 ).b > Pixel( 56, 64 ).r && Pixel( 80, 64 ).r > Pixel( 80, 64 ).b, "負から正へ連続するscrollは整数U境界をwrapして正側offsetと同じ既知色を作る" );
		Data.Material.MaskScale = 1.0f; Data.Material.MaskTexture = Mask; Data.Material.bDissolve = 1; Straight[0].Dissolve = Straight[1].Dissolve = 0.5f; Straight[0].UVOffset[0] = Straight[1].UVOffset[0] = 0.0f; Render( Data, Straight );
		// source scroll前に固定maskが作る穴と表示画素。
		const FColor32 FixedMaskHole = Pixel( 48, 64 ); const FColor32 FixedMaskAlive = Pixel( 80, 64 );
		Straight[0].UVOffset[0] = Straight[1].UVOffset[0] = 0.5f; Render( Data, Straight );
		C.Check( FixedMaskHole.r + FixedMaskHole.g + FixedMaskHole.b < 16 && Pixel( 48, 64 ).r + Pixel( 48, 64 ).g + Pixel( 48, 64 ).b < 16 && FixedMaskAlive.r > 30 && Pixel( 80, 64 ).b > 30 && ( FixedMaskAlive.r != Pixel( 80, 64 ).r || FixedMaskAlive.b != Pixel( 80, 64 ).b ), "sourceのscrollで色を動かしてもmask側の穴位置は固定する" );
		SaveProof( "effect-material-ribbon-uv-scroll.png" );
		SetBackgroundColor( 30, 20, 10, 255 ); Data = MakeData( HalfAlpha, -1, 2 ); Straight[0].Path.Color = Straight[1].Path.Color = 0xFFFFFFFFu; Straight[0].Emission = Straight[1].Emission = 1.0f;
		Render( Data, Straight, EDrawBlend::Alpha );
		// Alpha合成後に中央で読む赤成分。
		const uint8 AlphaPixel = Pixel( 64, 64 ).r;
		Render( Data, Straight, EDrawBlend::Add );
		// Add合成後に中央で読む赤成分。
		const uint8 AddPixel = Pixel( 64, 64 ).r;
		C.Check( AlphaPixel >= 50 && AlphaPixel <= 60 && AddPixel >= 65 && AddPixel <= 75, "半透明帯はAlphaとAddで呼び出し側の異なる合成を使う" );
		SetBackgroundColor( 0, 0, 0, 255 );
		Data = MakeData( White, -1, 2 ); Straight[0].Path.Color = Straight[1].Path.Color = 0x800040FFu; Straight[0].Emission = Straight[1].Emission = 1.0f;
		Render( Data, Straight, EDrawBlend::Alpha );
		// 点の生RGBAを半透明blendした中央pixel。
		const FColor32 RawAlphaColor = Pixel( 64, 64 );
		C.Check( RawAlphaColor.r >= 120 && RawAlphaColor.r <= 135 && RawAlphaColor.g >= 25 && RawAlphaColor.g <= 39 && RawAlphaColor.b < 8, "点RGBAはRGBを先に前掛けせず、alpha合成時に既知の半透明色になる" );
		// 単独半透明Ribbonの全画素をghost分割結果と比較する画像。
		TArray<FColor32> Single;
		Single.SetNum( 128 * 128 );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) Single[Y * 128 + X] = Pixel( X, Y );
		// RGBA勾配と材質値を持つ曲線状の比較経路。
		FDrawMaterialRibbonData::FPoint Bend[4] = { MakePoint( -2.0f, -1.0f, 0.0f, 0x600000FFu, 0.0f, 1.0f, 0.9f ), MakePoint( -0.5f, 1.0f, 0.33f, 0x8000FF00u, 0.25f, 1.5f, 1.0f ), MakePoint( 1.0f, -0.5f, 0.66f, 0xA0FF0000u, 0.5f, 2.0f, 1.1f ), MakePoint( 2.0f, 1.0f, 1.0f, 0xBFFFFFFFu, 0.75f, 1.0f, 1.2f ) };
		Data = MakeData( Split, Mask, 4 ); Data.Material.bDissolve = 1; Data.Material.EdgeWidth = 0.15f; Data.Material.UVScale[0] = 1.5f;
		Render( Data, Bend );
		SaveProof( "effect-material-ribbon-curved.png" );
		// 単独Ribbonが透明な黒だけでなく実際に描いた画素数。
		int32 SingleLitPixels = 0;
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) { Single[Y * 128 + X] = Pixel( X, Y ); if ( Single[Y * 128 + X].r > 8 || Single[Y * 128 + X].g > 8 || Single[Y * 128 + X].b > 8 ) ++SingleLitPixels; }
		C.Check( SingleLitPixels > 100, "半透明ghost帯の単独描画に十分な可視画素がある" );
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		// ghost点で二つに分けた二命令を所有するbufferとwriter。
		FDrawBuffer Chunks; FDrawContext ChunkDraw( Chunks );
		Data.Ribbon.JoinFlags = FDrawRibbonData::NextPoint; ChunkDraw.Blend( EDrawBlend::Alpha, 255 ); ChunkDraw.MaterialRibbon3D( Data, Bend );
		Data.Ribbon.NumPoints = 3; Data.Ribbon.JoinFlags = FDrawRibbonData::PreviousPoint; ChunkDraw.MaterialRibbon3D( Data, Bend + 1 );
		if ( !Chunks.Words.empty() ) Executor.Execute( FDrawSpan { Chunks.Words.data(), static_cast<int32>( Chunks.Words.size() ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// 分割描画と単独画像の全RGBが一致するか。
		bool bSplitMatches = true;
		for ( int32 Y = 0; Y < 128 && bSplitMatches; ++Y ) for ( int32 X = 0; X < 128; ++X ) { const FColor32 Actual = Pixel( X, Y ); if ( std::memcmp( &Single[Y * 128 + X], &Actual, sizeof( FColor32 ) ) != 0 ) { bSplitMatches = false; break; } }
		C.Check( SingleLitPixels > 100 && bSplitMatches, "曲がった半透明材質帯はghost点で2命令へ分けてもRGBA・消失・発光・UV移動の全画素が一致する" );
		Data = MakeData( White, -1, 2 ); Straight[0] = MakePoint( -2.0f, 0.0f, 0.0f, 0xFFFFFFFFu ); Straight[1] = MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFFFFFFu );
		// 意図的に帯から離した呼び出し元World行列。
		const MATRIX CallerWorld = MGetTranslate( VGet( 100.0f, 200.0f, 300.0f ) ); SetTransformToWorld( &CallerWorld ); Render( Data, Straight );
		C.Check( Pixel( 64, 64 ).r > 200, "既にworld座標の帯へ呼び出し元WorldMatrixを重ねない" );
		// 4通りの深度test/write指定を別々の遮蔽順で確認する。
		C.Group = "effect-material-ribbon-depth";
		const auto AppendDepthBand = [&]( FDrawContext& Draw, int32 Texture, float32 Z, int32 DepthTest, int32 DepthWrite )
		{
			// 描画したい深度比較と書込を持つ一つの帯設定。
			FDrawMaterialRibbonData DepthData = MakeData( Texture, -1, 2 ); DepthData.Material.DepthTest = DepthTest; DepthData.Material.DepthWrite = DepthWrite;
			// 指定Z位置に置く左右二点。
			FDrawMaterialRibbonData::FPoint DepthPoints[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 0xFFFFFFFFu ), MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFFFFFFu ) };
			DepthPoints[0].Path.Pos[2] = DepthPoints[1].Path.Pos[2] = Z; DepthPoints[0].Path.Width = DepthPoints[1].Path.Width = 1.5f;
			Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialRibbon3D( DepthData, DepthPoints );
		};
		SetTransformToWorld( &Identity ); SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( TRUE );
		for ( int32 DepthTest = 0; DepthTest <= 1; ++DepthTest ) for ( int32 DepthWrite = 0; DepthWrite <= 1; ++DepthWrite )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			// 手前青と試験対象を順に描くcommand buffer。
			FDrawBuffer Occlusion; FDrawContext OcclusionDraw( Occlusion );
			AppendDepthBand( OcclusionDraw, Blue, -1.0f, 1, 1 ); AppendDepthBand( OcclusionDraw, Red, 0.0f, DepthTest, DepthWrite );
			Executor.Execute( FDrawSpan { Occlusion.Words.data(), static_cast<int32>( Occlusion.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			// 手前の青帯と試験対象のどちらが残ったか。
			const FColor32 OcclusionPixel = Pixel( 64, 64 );
			C.Check( DepthTest == 0 ? OcclusionPixel.r > OcclusionPixel.b : OcclusionPixel.b > OcclusionPixel.r, "材質帯の深度比較flagが手前の遮蔽物を通すか拒否する" );
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			// 対象帯の深度書込後に奥側緑を描くcommand buffer。
			FDrawBuffer WriteCheck; FDrawContext WriteDraw( WriteCheck );
			AppendDepthBand( WriteDraw, Red, 0.0f, DepthTest, DepthWrite ); AppendDepthBand( WriteDraw, Green, 1.0f, 1, 0 );
			Executor.Execute( FDrawSpan { WriteCheck.Words.data(), static_cast<int32>( WriteCheck.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			// 深度書込により赤が残ったか緑が重なったか。
			const FColor32 WritePixel = Pixel( 64, 64 );
			C.Check( DepthWrite == 1 ? WritePixel.r > WritePixel.g : WritePixel.g > WritePixel.r, "材質帯の深度書込flagが後続の奥側を遮蔽する" );
		}
		SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE );
		// 通常quadと旧Ribbonが材質Ribbon描画後も単独時の全画素を保つか比較する。
		C.Group = "effect-material-ribbon-legacy-regression";
		FDrawQuad3DData PlainQuad; PlainQuad.Handle = White; PlainQuad.Pos[1] = -1.5f; PlainQuad.Width = 1.5f; PlainQuad.Height = 1.5f; PlainQuad.Color = { 255, 255, 255, 255 };
		// 旧Ribbon単独描画と材質描画後の比較に使う二点。
		FDrawRibbonData::FPoint LegacyPoints[2] = {};
		LegacyPoints[0].Pos[0] = -2.0f; LegacyPoints[0].Width = 0.5f; LegacyPoints[0].U = 0.0f; LegacyPoints[0].Color = 0xFFFFFFFFu;
		LegacyPoints[1].Pos[0] = 2.0f; LegacyPoints[1].Width = 0.5f; LegacyPoints[1].U = 1.0f; LegacyPoints[1].Color = 0xFFFFFFFFu;
		// 単独表示時に保存する全screen pixel。
		TArray<FColor32> PlainAndOld;
		PlainAndOld.SetNum( 128 * 128 );
		// 通常quadと旧Ribbonを必要なら材質Ribbonの後へ並べて描く処理。
		const auto RenderPlainAndOld = [&]( bool bAfterMaterial )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen(); FDrawBuffer Commands; FDrawContext Draw( Commands );
			if ( bAfterMaterial )
			{
				FDrawMaterialRibbonData Above = MakeData( White, -1, 2 ); FDrawMaterialRibbonData::FPoint AbovePoints[2] = { MakePoint( -2.0f, 1.5f, 0.0f, 0xFFFFFFFFu ), MakePoint( 2.0f, 1.5f, 1.0f, 0xFFFFFFFFu ) }; AbovePoints[0].Path.Width = AbovePoints[1].Path.Width = 0.5f;
				Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialRibbon3D( Above, AbovePoints );
			}
			Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.Quad3D( PlainQuad ); Draw.Ribbon3D( LegacyPoints, 2, White, 1.0f );
			Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) PlainAndOld[Y * 128 + X] = Pixel( X, Y );
		};
		RenderPlainAndOld( false );
		// 材質描画前に撮った通常描画の比較基準。
		TArray<FColor32> AfterMaterialPlainAndOld; AfterMaterialPlainAndOld.SetNum( 128 * 128 );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) AfterMaterialPlainAndOld[Y * 128 + X] = PlainAndOld[Y * 128 + X];
		RenderPlainAndOld( true );
		bool bLegacyPixelsMatch = true;
		for ( int32 Y = 40; Y < 104 && bLegacyPixelsMatch; ++Y ) for ( int32 X = 28; X < 100; ++X ) if ( ( Y >= 56 && Y < 72 ) || ( Y >= 76 && Y < 104 ) ) { const FColor32 A = AfterMaterialPlainAndOld[Y * 128 + X]; const FColor32 B = Pixel( X, Y ); if ( A.r != B.r || A.g != B.g || A.b != B.b ) { bLegacyPixelsMatch = false; break; } }
		C.Check( bLegacyPixelsMatch && AfterMaterialPlainAndOld[88 * 128 + 64].r > 50 && AfterMaterialPlainAndOld[64 * 128 + 64].r > 50 && Pixel( 64, 40 ).r > 100, "材質帯を先に描いても既知色の通常quadと旧Ribbonは単独時の全RGB画素に一致する" );
		// 不正な長さ・点数・flag・非有限値・UV積はnative描画入口で拒否する。
		C.Group = "effect-material-ribbon-malformed-command";
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		// 直前の不正commandで中心画素が変化していないか調べる関数。
		const auto PixelIsBlack = [&]() { RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); const FColor32 Value = Pixel( 64, 64 ); return Value.r < 8 && Value.g < 8 && Value.b < 8; };
		// 長さ不足payloadを単独で実行し、拒否結果を返す関数。
		const auto RejectShortCommand = [&]()
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			const uint32 ShortCommand[2] = { static_cast<uint32>( EDrawCommand::MaterialRibbon3D ) | ( 2u << 16 ), 0u };
			Executor.Execute( FDrawSpan { ShortCommand, 2 } );
			return PixelIsBlack();
		};
		C.Check( RejectShortCommand(), "短い材質帯payloadを描かずに拒否する" );
		Render( MakeData( White, -1, 2 ), Straight );
		C.Check( Pixel( 64, 64 ).r > 200, "偽造command拒否試験の正常材質帯controlが描画される" );
		// 指定された偽造headerとpayloadを実行し、描画されないことを返す関数。
		const auto SendForged = [&]( const FDrawMaterialRibbonData& BadData, const FDrawMaterialRibbonData::FPoint* BadPoints, int32 ReportedTotalWords, int32 ActualPayloadWords )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			// 指定長で組み立てた偽造command全体。
			TArray<uint32> Words; Words.SetNum( 1 + ActualPayloadWords ); Words[0] = static_cast<uint32>( EDrawCommand::MaterialRibbon3D ) | ( static_cast<uint32>( ReportedTotalWords ) << 16 );
			if ( ActualPayloadWords >= static_cast<int32>( sizeof( FDrawMaterialRibbonData ) / 4 ) ) std::memcpy( Words.GetData() + 1, &BadData, sizeof( BadData ) );
			if ( BadPoints != nullptr && ActualPayloadWords >= static_cast<int32>( sizeof( FDrawMaterialRibbonData ) / 4 + 2 * sizeof( FDrawMaterialRibbonData::FPoint ) / 4 ) ) std::memcpy( Words.GetData() + 1 + sizeof( FDrawMaterialRibbonData ) / 4, BadPoints, 2 * sizeof( FDrawMaterialRibbonData::FPoint ) );
			Executor.Execute( FDrawSpan { Words.GetData(), static_cast<int32>( Words.Num() ) } );
			return PixelIsBlack();
		};
		// 不正値だけを変えた偽造headerと点列。
		FDrawMaterialRibbonData BadData = MakeData( White, -1, 2 );
		// fixed payload長を満たす元の二点。
		FDrawMaterialRibbonData::FPoint BadPoints[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 0xFFFFFFFFu ), MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFFFFFFu ) };
		BadData.Ribbon.NumPoints = FDrawRibbonData::MaxPoints + 1; C.Check( SendForged( BadData, nullptr, 15, 14 ), "偽造した最大点数超過を点配列確保前に拒否する" );
		BadData = MakeData( White, -1, 2 ); BadData.Ribbon.JoinFlags = 4; C.Check( SendForged( BadData, BadPoints, 35, 34 ), "偽造JoinFlagsを描画前に拒否する" );
		BadData.Ribbon.JoinFlags = 0; BadData.Material.DepthWrite = 3; C.Check( SendForged( BadData, BadPoints, 35, 34 ), "偽造DepthWriteを描画前に拒否する" );
		BadData.Material.DepthWrite = 0; BadPoints[1].Emission = ( std::numeric_limits<float32>::quiet_NaN )(); C.Check( SendForged( BadData, BadPoints, 35, 34 ), "点の非有限Emissionを描画前に拒否する" );
		BadPoints[1] = MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFFFFFFu ); BadPoints[0].Path.U = BadPoints[1].Path.U = 1.0e38f; BadData.Material.UVScale[0] = 128.0f; C.Check( SendForged( BadData, BadPoints, 35, 34 ), "有限入力のscale積がoverflowするpayloadを描画前に拒否する" );
		// 専用sentinel native状態の中で材質Ribbon自身が描画され、全状態を戻すcallback。
		FDrawMaterialQuadData SentinelQuad;
		SentinelQuad.Quad.Handle = White; SentinelQuad.MaskTexture = Mask; SentinelQuad.Quad.Width = 4.0f; SentinelQuad.Quad.Height = 4.0f; SentinelQuad.Quad.Color = { 255, 255, 255, 255 }; SentinelQuad.Dissolve = 0.0f; SentinelQuad.EdgeWidth = 0.0f;
		// sentinel shader中で描き、native stateの復元を検査する帯設定。
		FDrawMaterialRibbonData NativeData = MakeData( White, -1, 2 );
		// native fixtureの中心pixelへ届く帯の点。
		FDrawMaterialRibbonData::FPoint NativePoints[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 0xFFFFFFFFu ), MakePoint( 2.0f, 0.0f, 1.0f, 0xFFFFFFFFu ) };
		NativePoints[0].Path.Width = NativePoints[1].Path.Width = 1.5f;
		// fixtureがsentinelを置いた直後に呼ぶ実際の材質Ribbon描画。
		const std::function<void()> DrawNativeRibbon = [&]()
		{
			FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialRibbon3D( NativeData, NativePoints );
			if ( !Commands.Words.empty() ) Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } );
		};
		NWorldTest::RunEffectMaterialRibbonNativeStateRestore( C, Executor, SentinelQuad, Soft, DrawNativeRibbon );
		if ( DerivedRight >= 0 ) DeleteGraph( DerivedRight );
		SetDrawScreen( Saved.Screen );
		DeleteGraph( Screen ); DeleteSoftImage( Soft ); DeleteGraph( White ); DeleteGraph( Dim ); DeleteGraph( HalfAlpha ); DeleteGraph( Split ); DeleteGraph( Mask ); DeleteGraph( Red ); DeleteGraph( Blue ); DeleteGraph( Green );
	}
}

// 材質帯の記録・画像・ghost分割結果をworld selftestへ加える。
void NWorldTest::RunEffectMaterialRibbonRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor )
{
	TestMaterialRibbonRecording( C );
	TestMaterialRibbonPixels( C, Executor );
}
