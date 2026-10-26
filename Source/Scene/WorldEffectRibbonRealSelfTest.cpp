// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>

#include "Library/Core/Interfaces/Render/DrawRibbon.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 位置・全幅・累積距離・色を明示した、確認用の経路点を作る。
	FDrawRibbonData::FPoint MakePoint( float32 X, float32 Y, float32 Z, float32 Width, float32 U, uint32 Color = 0xFFFFFFFFu )
	{
		// 描画窓口へ渡す、隙間のない点データ。
		FDrawRibbonData::FPoint Point;
		Point.Pos[0] = X;
		Point.Pos[1] = Y;
		Point.Pos[2] = Z;
		Point.Width = Width;
		Point.U = U;
		Point.Color = Color;
		return Point;
	}

	// 可変長記録の内容と所有、境界値、不正な入力の拒否を画面なしで確認する。
	void TestRibbonRecording( NAssetTest::FChecker& C )
	{
		C.Group = "effect-ribbon-record";
		// 記録の前後で変えてはいけない既存命令も入れる。
		FDrawBuffer Buffer;
		// 検査対象となる、本番と同じ記録窓口。
		FDrawContext Draw( Buffer );
		Draw.Box( 1, 2, 3, 4, FColor32( 9, 8, 7, 255 ) );
		// 帯の命令が始まる位置。
		const int32 Begin = Draw.GetNumWords();
		// 後で変更し、記録が参照を保持していないことを確かめる入力。
		FDrawRibbonData::FPoint Points[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0x80112233u ), MakePoint( 2.0f, 1.0f, 0.0f, 1.0f, 4.0f, 0xFF445566u ) };
		Draw.Ribbon3D( Points, 2, 17, 0.5f );
		C.Check( Draw.GetNumWords() == Begin + 17 && ( Buffer.Words[Begin] & 0xFFu ) == static_cast<uint32>( EDrawCommand::Ribbon3D ) && ( Buffer.Words[Begin] >> 16 ) == 17u, "2点の帯は固定情報と24byteの点列を17wordの命令へ記録する" );
		// 記録を読み戻す固定情報。
		FDrawRibbonData Header;
		std::memcpy( &Header, Buffer.Words.data() + Begin + 1, sizeof( Header ) );
		// 入力の変更後も保持される、最初の点。
		FDrawRibbonData::FPoint Recorded;
		std::memcpy( &Recorded, Buffer.Words.data() + Begin + 5, sizeof( Recorded ) );
		C.Check( Header.Texture == 17 && Header.NumPoints == 2 && Header.UVRepeat == 0.5f && Header.JoinFlags == 0 && std::memcmp( &Recorded, &Points[0], sizeof( Recorded ) ) == 0, "画像・反復倍率・接続指定・点の色と座標をそのまま記録する" );
		Points[0].Pos[0] = 99.0f;
		Points[0].Color = 0;
		std::memcpy( &Recorded, Buffer.Words.data() + Begin + 5, sizeof( Recorded ) );
		C.Check( Recorded.Pos[0] == -2.0f && Recorded.Color == 0x80112233u, "呼び出し後に入力の点を書き換えても、記録済みの内容は変わらない" );
		// 不正な入力では、これ以上命令が増えない。
		const int32 BeforeInvalid = Draw.GetNumWords();
		Points[0] = MakePoint( 0.0f, 0.0f, 0.0f, 1.0f, 0.0f );
		Points[1] = MakePoint( 1.0f, 0.0f, 0.0f, 1.0f, 1.0f );
		Draw.Ribbon3D( nullptr, 2, -1 );
		Draw.Ribbon3D( Points, -1, -1 );
		Draw.Ribbon3D( Points, TNumericLimits<int32>::Min(), -1, 1.0f, FDrawRibbonData::PreviousPoint );
		Draw.Ribbon3D( Points, 0, -1 );
		Draw.Ribbon3D( Points, 1, -1 );
		Draw.Ribbon3D( Points, FDrawRibbonData::MaxPoints + 1, -1 );
		Draw.Ribbon3D( Points, 2, -2 );
		Draw.Ribbon3D( Points, 2, -1, 0.0f );
		Draw.Ribbon3D( Points, 2, -1, TNumericLimits<float32>::Infinity() );
		Draw.Ribbon3D( Points, 2, -1, TNumericLimits<float32>::QuietNaN() );
		Draw.Ribbon3D( Points, 2, -1, 1.0f, 4 );
		Draw.Ribbon3D( Points, 2, -1, 1.0f, FDrawRibbonData::PreviousPoint );
		Draw.Ribbon3D( Points, 2, -1, 1.0f, FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint );
		C.Check( Draw.GetNumWords() == BeforeInvalid, "点数・画像番号・反復倍率・接続指定が不正な場合は、命令全体を記録しない" );
		Points[1].Pos[1] = TNumericLimits<float32>::QuietNaN();
		Draw.Ribbon3D( Points, 2, -1 );
		Points[1] = MakePoint( 1.0f, 0.0f, 0.0f, -0.1f, 1.0f );
		Draw.Ribbon3D( Points, 2, -1 );
		Points[1] = MakePoint( 1.0f, 0.0f, 0.0f, 1.0f, -1.0f );
		Draw.Ribbon3D( Points, 2, -1 );
		Points[0].U = 2.0f;
		Points[1].U = 1.0f;
		Draw.Ribbon3D( Points, 2, -1 );
		C.Check( Draw.GetNumWords() == BeforeInvalid, "非有限の位置・負の幅・負または逆行する累積距離を拒否する" );
		Points[0].U = 0.0f;
		Points[1].U = static_cast<float32>( FDrawRibbonData::MaxTextureSections + 1 );
		Draw.Ribbon3D( Points, 2, 0 );
		C.Check( Draw.GetNumWords() == BeforeInvalid, "画像の反復による分割が8192区間を超える場合は、暴走せず記録を拒否する" );
		// 上限数の点でも、命令長と累積距離が正しく残る入力。
		TArray<FDrawRibbonData::FPoint> Maximum;
		Maximum.SetNum( FDrawRibbonData::MaxPoints );
		// 最大の点列に、1単位ずつ増える位置と累積距離を入れる。
		for ( int32 Index = 0; Index < Maximum.Num(); ++Index ) Maximum[Index] = MakePoint( static_cast<float32>( Index ), 0.0f, 0.0f, Index == 0 ? 0.0f : 1.0f, static_cast<float32>( Index ) );
		Draw.Ribbon3D( Maximum.GetData(), Maximum.Num(), -1 );
		C.Check( Draw.GetNumWords() == BeforeInvalid + 5 + 6 * FDrawRibbonData::MaxPoints && ( Buffer.Words[BeforeInvalid] >> 16 ) == 5u + 6u * FDrawRibbonData::MaxPoints, "1024点と幅0の先端を受け付け、16bitの命令長を超えない" );
	}

	// 実機確認で変更する、取得口のある状態を終了時に戻す。
	struct FRibbonCallerState
	{
		// 呼び出し元の描画先。
		int32 Screen = GetDrawScreen();
		// 描画先切り替え時に設定を初期化するか。
		int32 Reset = GetUseSetDrawScreenSettingReset();
		// 呼び出し元のカメラの近い限界。
		float32 Near = GetCameraNear();
		// 呼び出し元のカメラの遠い限界。
		float32 Far = GetCameraFar();
		// 呼び出し元の画素の縦横比。
		float32 DotAspect = GetCameraDotAspect();
		// 呼び出し元のカメラの表示中心。
		float32 Center[2] = {};
		// 呼び出し元のカメラの位置と向き。
		MATRIX CameraView = GetCameraViewMatrix();
		// 呼び出し元のカメラの投影。
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// 呼び出し元の物体の変換。
		MATRIX World = {};
		// 呼び出し元の直接指定された視点の変換。
		MATRIX View = {};
		// 呼び出し元の直接指定された投影。
		MATRIX Projection = {};
		// 呼び出し元の画面上の配置。
		MATRIX Viewport = {};
		// 呼び出し元の描画範囲。
		RECT Area = {};
		// 呼び出し元の背景の4成分。
		int32 Background[4] = {};
		// 呼び出し元の色の混ぜ方と強さ。
		int32 Blend[2] = {};
		// 呼び出し元の描画色の3成分。
		int32 Bright[3] = {};
		// 呼び出し元の裏面の扱い。
		int32 Culling = GetUseBackCulling();
		// 呼び出し元の既定の光の有効状態。
		int32 LightEnable = GetLightEnable();
		// 呼び出し元の既定の光の環境色。
		COLOR_F Ambient = GetLightAmbColor();
		// 呼び出し元の既定の光の拡散色。
		COLOR_F Diffuse = GetLightDifColor();

		// 描画画面を変える前に、呼び出し元の状態を退避する。
		FRibbonCallerState()
		{
			GetTransformToWorldMatrix( &World );
			GetTransformToViewMatrix( &View );
			GetTransformToProjectionMatrix( &Projection );
			GetTransformToViewportMatrix( &Viewport );
			GetDrawArea( &Area );
			GetCameraScreenCenter( &Center[0], &Center[1] );
			GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			GetDrawBlendMode( &Blend[0], &Blend[1] );
			GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// Zは、ほかのツールの実機確認と同じく2Dの重ね描き用に無効へ戻す。
		~FRibbonCallerState()
		{
			RenderVertex();
			SetDrawScreen( Screen );
			SetCameraNearFar( Near, Far );
			SetCameraDotAspect( DotAspect );
			SetCameraScreenCenter( Center[0], Center[1] );
			SetCameraViewMatrix( CameraView );
			SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World );
			SetTransformToView( &View );
			SetTransformToProjection( &Projection );
			SetTransformToViewport( &Viewport );
			SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] );
			SetDrawBlendMode( Blend[0], Blend[1] );
			SetDrawBright( Bright[0], Bright[1], Bright[2] );
			SetUseBackCulling( Culling );
			SetLightEnable( LightEnable );
			SetLightAmbColor( Ambient );
			SetLightDifColor( Diffuse );
			SetUseZBuffer3D( FALSE );
			SetWriteZBuffer3D( FALSE );
			SetUseSetDrawScreenSettingReset( Reset );
		}
	};

	// 本番の実行窓口を使い、太さ・曲がり・画像の軸と反復・色・復元をピクセルで確認する。
	void TestRibbonPixels( NAssetTest::FChecker& C, IDrawExecutor& Executor )
	{
		C.Group = "effect-ribbon-real";
		// 終了時に復帰する、この自己確認の呼び出し元の状態。
		const FRibbonCallerState Saved;
		// 正射影8単位を128ピクセルへ描く確認画面。
		const int32 Screen = MakeScreen( 128, 128, FALSE );
		// 描画結果を読み戻す画像。
		const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
		C.Check( Screen >= 0 && Soft >= 0, "帯を描画して読み戻す専用画面と画像を確保できる" );
		if ( Screen < 0 || Soft < 0 )
		{
			if ( Screen >= 0 ) DeleteGraph( Screen );
			if ( Soft >= 0 ) DeleteSoftImage( Soft );
			return;
		}
		SetDrawScreen( Screen );
		SetDrawAreaFull();
		SetCameraNearFar( 0.1f, 100.0f );
		SetCameraDotAspect( 1.0f );
		SetCameraScreenCenter( 64.0f, 64.0f );
		SetupCamera_Ortho( 8.0f );
		SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		// 元の表示領域と異なる場合も、読み戻す画面全体を使う。
		MATRIX Viewport = {};
		CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f );
		SetTransformToViewport( &Viewport );
		// ワールド座標が、そのまま描画に使われる基準の変換。
		const MATRIX Identity = MGetIdent();
		SetTransformToWorld( &Identity );
		SetBackgroundColor( 0, 0, 0, 255 );
		SetUseZBuffer3D( FALSE );
		SetWriteZBuffer3D( FALSE );
		SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
		SetDrawBright( 255, 255, 255 );
		SetLightEnable( TRUE );
		SetLightAmbColor( GetColorF( 1.0f, 1.0f, 1.0f, 1.0f ) );
		SetLightDifColor( GetColorF( 0.0f, 0.0f, 0.0f, 1.0f ) );
		// 画面から読み戻したピクセルの色。描画の座標計算には使わない。
		const auto Pixel = [&]( int32 X, int32 Y )
		{
			// DxLibが返す赤・緑・青・不透明度。
			int32 Channels[4] = {};
			GetPixelSoftImage( Soft, X, Y, &Channels[0], &Channels[1], &Channels[2], &Channels[3] );
			return FColor32( static_cast<uint8>( Channels[0] ), static_cast<uint8>( Channels[1] ), static_cast<uint8>( Channels[2] ), static_cast<uint8>( Channels[3] ) );
		};
		// 画像を消し、本番の記録と実行の窓口を通してから読み戻す。
		const auto Render = [&]( const FDrawRibbonData::FPoint* Points, int32 Count, int32 Texture = -1, float32 Repeat = 1.0f, int32 JoinFlags = 0 )
		{
			ClearDrawScreen();
			// 今回だけの帯の描画命令。
			FDrawBuffer Buffer;
			// 再生側と同じ描画の記録窓口。
			FDrawContext Draw( Buffer );
			Draw.Ribbon3D( Points, Count, Texture, Repeat, JoinFlags );
			if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			RenderVertex();
			GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		// 画像なしで、赤い全幅1単位の帯をまっすぐ描く。
		FDrawRibbonData::FPoint Straight[2] = { MakePoint( -2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0xFF0000FFu ), MakePoint( 2.0f, 0.0f, 0.0f, 1.0f, 4.0f, 0xFF0000FFu ) };
		Render( Straight, 2 );
		C.Check( Pixel( 64, 64 ).r > 200 && Pixel( 64, 64 ).g < 8 && Pixel( 64, 64 ).b < 8, "画像番号-1でも、RGBAの赤が色帯として表示される" );
		C.Check( Pixel( 64, 58 ).r > 180 && Pixel( 64, 70 ).r > 180 && Pixel( 64, 54 ).r < 8 && Pixel( 64, 74 ).r < 8 && Pixel( 30, 64 ).r < 8 && Pixel( 98, 64 ).r < 8, "幅1単位は約16ピクセルで、経路の外側や全幅の2倍には広がらない" );
		Straight[0].Color = Straight[1].Color = 0x800000FFu;
		Render( Straight, 2 );
		C.Check( Pixel( 64, 64 ).r >= 110 && Pixel( 64, 64 ).r <= 145 && Pixel( 64, 64 ).g < 8, "混合なしの呼び出し元でも、不透明度128は赤を約半分に混ぜる（%d）", Pixel( 64, 64 ).r );
		Straight[0].Color = Straight[1].Color = 0x000000FFu;
		Render( Straight, 2 );
		C.Check( Pixel( 64, 64 ).r < 8, "不透明度0の帯は、背景を変えない" );
		Straight[0].Color = 0xFF0000FFu;
		Straight[1].Color = 0xFFFF0000u;
		Render( Straight, 2 );
		C.Check( Pixel( 40, 64 ).r > Pixel( 40, 64 ).b * 2 && Pixel( 88, 64 ).b > Pixel( 88, 64 ).r * 2 && Pixel( 64, 64 ).g < 8, "両端の赤と青を帯に沿って補間し、色の成分の順番を取り違えない" );
		// 折れ曲がった経路。曲がりの両側にピクセルがあり、経路の下側は空く。
		FDrawRibbonData::FPoint Bend[3] = { MakePoint( -2.0f, -1.0f, 0.0f, 0.5f, 0.0f ), MakePoint( 0.0f, 1.0f, 0.0f, 0.5f, 2.83f ), MakePoint( 2.0f, -1.0f, 0.0f, 0.5f, 5.66f ) };
		Render( Bend, 3 );
		C.Check( Pixel( 48, 64 ).r > 180 && Pixel( 64, 48 ).r > 180 && Pixel( 80, 64 ).r > 180 && Pixel( 64, 78 ).r < 8, "曲がる帯は二つの区間と接続部に連続して描かれ、直線へ置き換わらない" );
		// 4点の曲線を1命令で描いた画像。
		FDrawRibbonData::FPoint LongBend[4] = { Bend[0], Bend[1], MakePoint( 1.0f, 1.5f, 0.0f, 0.5f, 4.25f ), MakePoint( 2.0f, -1.0f, 0.0f, 0.5f, 6.37f ) };
		for ( FDrawRibbonData::FPoint& Point : LongBend ) Point.Color = 0x80FFFFFFu;
		Render( LongBend, 4 );
		// 単一命令の結果を、全画素で比較する保存領域。
		TArray<FColor32> SingleImage;
		SingleImage.SetNum( 128 * 128 );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) SingleImage[Y * 128 + X] = Pixel( X, Y );
		// 同じ曲線を接続点の前後で二命令へ分け、ghostの区間を描かない。
		ClearDrawScreen();
		// 二命令をまとめた比較用の描画記録。
		FDrawBuffer ChunkBuffer;
		// 同じ二命令を順番どおりに詰める記録口。
		FDrawContext ChunkDraw( ChunkBuffer );
		ChunkDraw.Ribbon3D( LongBend, 4, -1, 1.0f, FDrawRibbonData::NextPoint );
		ChunkDraw.Ribbon3D( LongBend + 1, 3, -1, 1.0f, FDrawRibbonData::PreviousPoint );
		if ( !ChunkBuffer.Words.empty() ) Executor.Execute( FDrawSpan { ChunkBuffer.Words.data(), static_cast<int32>( ChunkBuffer.Words.size() ) } );
		RenderVertex();
		GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// 分割で画素が変わった時点で止める比較結果。
		bool bChunkImageMatches = true;
		for ( int32 Y = 0; Y < 128 && bChunkImageMatches; ++Y )
		{
			for ( int32 X = 0; X < 128; ++X )
			{
				const FColor32 ChunkPixel = Pixel( X, Y );
				if ( std::memcmp( &SingleImage[Y * 128 + X], &ChunkPixel, sizeof( FColor32 ) ) != 0 )
				{
					bChunkImageMatches = false;
					break;
				}
			}
		}
		C.Check( bChunkImageMatches, "接線用の隣接点を含む2命令の曲がった透明帯は、1命令の画像と一致してalpha重なりや継ぎ目を作らない" );
		// 同じ位置を二つ続けても、隣の長さのある区間を使って接続する。
		FDrawRibbonData::FPoint Duplicate[4] = { MakePoint( -2.0f, 0.0f, 0.0f, 0.5f, 0.0f ), MakePoint( 0.0f, 0.0f, 0.0f, 0.5f, 2.0f ), MakePoint( 0.0f, 0.0f, 0.0f, 0.5f, 2.0f ), MakePoint( 2.0f, 0.0f, 0.0f, 0.5f, 4.0f ) };
		Render( Duplicate, 4 );
		C.Check( Pixel( 48, 64 ).r > 180 && Pixel( 64, 64 ).r > 180 && Pixel( 80, 64 ).r > 180, "重複点があっても左右の帯が消えず、接続部に穴を開けない" );
		// 長さをまったく持たない経路では、正規化せず描画を省略する。
		const FDrawRibbonData::FPoint Same[2] = { MakePoint( 0.0f, 0.0f, 0.0f, 1.0f, 0.0f ), MakePoint( 0.0f, 0.0f, 0.0f, 1.0f, 0.0f ) };
		Render( Same, 2 );
		C.Check( Pixel( 64, 64 ).r < 8 && Pixel( 72, 64 ).r < 8, "全点が同じ位置の経路は、不正な頂点を作らず何も描かない" );
		// 急に折り返す経路。接続部の幅が無限に伸びないことを確認する。
		FDrawRibbonData::FPoint Sharp[3] = { MakePoint( -2.0f, 0.0f, 0.0f, 0.5f, 0.0f ), MakePoint( 0.0f, 0.0f, 0.0f, 0.5f, 2.0f ), MakePoint( -1.9f, 0.01f, 0.0f, 0.5f, 3.9f ) };
		Render( Sharp, 3 );
		C.Check( Pixel( 64, 32 ).r < 8 && Pixel( 64, 96 ).r < 8 && Pixel( 48, 64 ).r > 100, "折り返す接続部も幅の張り出しを制限し、画面を横切る棘を作らない" );
		// 視線と平行な区間に続いて、横へ向かう経路。
		FDrawRibbonData::FPoint Parallel[3] = { MakePoint( 0.0f, 0.0f, -1.0f, 0.5f, 0.0f ), MakePoint( 0.0f, 0.0f, 0.0f, 0.5f, 1.0f ), MakePoint( 2.0f, 0.0f, 0.0f, 0.5f, 3.0f ) };
		Render( Parallel, 3 );
		C.Check( Pixel( 80, 64 ).r > 100, "視線と平行な区間が含まれても、不正な法線で後続の帯を消さない" );
		// 画像の軸と反復を、左赤・右青の二色で確認する。
		const int32 StripeSoft = MakeARGB8ColorSoftImage( 8, 8 );
		// 画像の実機ハンドル。作成に失敗しても、ほかの確認と後始末は続ける。
		int32 Stripe = -1;
		if ( StripeSoft >= 0 )
		{
			// 長手方向の色の切り替え。幅方向の下半分には緑を足して軸も区別する。
			for ( int32 Y = 0; Y < 8; ++Y )
			{
				// 画像の長手方向に並ぶ色の列。
				for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( StripeSoft, X, Y, X < 4 ? 255 : 0, Y < 4 ? 0 : 255, X < 4 ? 0 : 255, 255 );
			}
			Stripe = CreateGraphFromSoftImage( StripeSoft );
		}
		C.Check( Stripe >= 0, "長手と幅の画像座標を区別する、実機の二色stripe画像を作成できる" );
		if ( Stripe >= 0 )
		{
			Straight[0].Color = Straight[1].Color = 0xFFFFFFFFu;
			Render( Straight, 2, Stripe, 0.25f );
			C.Check( Pixel( 48, 60 ).r > 180 && Pixel( 48, 60 ).b < 70 && Pixel( 80, 60 ).b > 180 && Pixel( 80, 60 ).r < 70 && Pixel( 48, 68 ).g > 180 && Pixel( 48, 60 ).g < 70, "累積距離Uは画像の横へ、帯の幅は画像の縦へ対応する" );
			Render( Straight, 2, Stripe, 0.5f );
			C.Check( Pixel( 40, 60 ).r > 150 && Pixel( 56, 60 ).b > 150 && Pixel( 72, 60 ).r > 150 && Pixel( 88, 60 ).b > 150, "反復倍率を2倍にすると、実ピクセルで赤青の並びも2回繰り返す" );
		}
		// 現在のカメラへ向け直すことを、横から見た同じ太さでも確かめる。
		SetCameraPositionAndTarget_UpVecY( VGet( 10.0f, 0.0f, 0.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		// 横から見える、Z方向へ伸びた帯。
		FDrawRibbonData::FPoint SideView[2] = { MakePoint( 0.0f, 0.0f, -2.0f, 1.0f, 0.0f ), MakePoint( 0.0f, 0.0f, 2.0f, 1.0f, 4.0f ) };
		Render( SideView, 2 );
		C.Check( Pixel( 64, 58 ).r > 180 && Pixel( 64, 70 ).r > 180 && Pixel( 64, 52 ).r < 8, "カメラを横へ回しても、帯はそのカメラへ面を向け同じ全幅を保つ" );
		SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		// 帯の前後で、物体の行列や混合方法を勝手に標準値へ戻してはいけない。
		const MATRIX BeforeWorld = MGetTranslate( VGet( 100.0f, 200.0f, 300.0f ) );
		SetTransformToWorld( &BeforeWorld );
		SetDrawBlendMode( DX_BLENDMODE_ADD, 137 );
		SetDrawBright( 180, 190, 200 );
		SetUseBackCulling( DX_CULLING_LEFT );
		Render( Straight, 2 );
		// 実行後に残る混合方法と、その強さ。
		int32 AfterBlend[2] = {};
		GetDrawBlendMode( &AfterBlend[0], &AfterBlend[1] );
		// 実行後に残る描画色の倍率。
		int32 AfterBright[3] = {};
		GetDrawBright( &AfterBright[0], &AfterBright[1], &AfterBright[2] );
		// 実行後に残る、呼び出し元の物体の変換。
		MATRIX AfterWorld = {};
		GetTransformToWorldMatrix( &AfterWorld );
		C.Check( AfterBlend[0] == DX_BLENDMODE_ADD && AfterBlend[1] == 137 && AfterBright[0] == 180 && AfterBright[1] == 190 && AfterBright[2] == 200 && GetUseBackCulling() == DX_CULLING_LEFT && std::memcmp( &AfterWorld, &BeforeWorld, sizeof( BeforeWorld ) ) == 0 && Pixel( 64, 64 ).r > 40, "帯はワールド位置へ描き、混合・色倍率・裏面の扱い・元の物体行列を保存する" );
		SetDrawScreen( Saved.Screen );
		if ( Stripe >= 0 ) DeleteGraph( Stripe );
		if ( StripeSoft >= 0 ) DeleteSoftImage( StripeSoft );
		DeleteGraph( Screen );
		DeleteSoftImage( Soft );
	}
}

// 帯の可変長記録と、実際の描画結果を自己確認の共通集計へ加える。
void NWorldTest::RunEffectRibbonRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor )
{
	TestRibbonRecording( C );
	TestRibbonPixels( C, Executor );
}
