// SPDX-License-Identifier: NOASSERTION
#include "AssetSelfTestModelMaterial.h"

#include "AssetSelfTestSupport.h"
#include "AssetSelfTestModelSurface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Core/Interfaces/Render/MeshGeometryChunk.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/ModelMaterialEffectRenderer.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace NAssetTest
{
	namespace
	{
		// fixed ModelMaterial命令を一つ記録できる有効なテスト値を作る。
		FDrawModelMaterialData MakeValidModelMaterialCommand()
		{
			FDrawModelMaterialData Data;
			Data.Handle = 31;
			Data.Geometry = 43;
			Data.BlendMode = static_cast<int32>( EDrawBlend::Alpha );
			Data.Material.Texture = 59;
			Data.Material.Material.MaskTexture = -1;
			Data.Material.DistortionTexture = -1;
			return Data;
		}

		// Core chunk試験用にindexから復元できるx座標を持つ頂点を作る。
		FMeshVertex MakeIndexedVertex( int32 Index )
		{
			FMeshVertex Vertex;
			Vertex.Position[0] = static_cast<float32>( Index );
			Vertex.Position[1] = static_cast<float32>( Index % 7 );
			Vertex.Position[2] = 0.0f;
			Vertex.Normal[2] = 1.0f;
			Vertex.Color[0] = 255;
			Vertex.Color[1] = 255;
			Vertex.Color[2] = 255;
			Vertex.Color[3] = 255;
			return Vertex;
		}

		// 二材質へ各一三角形を割り当てたbackend geometry用fixtureを作る。
		FStaticModelSurfaceData MakeTwoMaterialSurface()
		{
			FStaticModelSurfaceData Surface;
			for ( int32 Index = 0; Index < 6; ++Index ) Surface.Vertices.Add( MakeIndexedVertex( Index ) );
			Surface.Indices = { 0, 1, 2, 3, 4, 5 };
			Surface.Materials.Add( FStaticModelSurfaceMaterial() );
			Surface.Materials.Add( FStaticModelSurfaceMaterial() );
			FStaticModelSurfaceSection First;
			First.FirstIndex = 0;
			First.NumIndices = 3;
			First.MaterialIndex = 0;
			Surface.Sections.Add( First );
			FStaticModelSurfaceSection Second;
			Second.FirstIndex = 3;
			Second.NumIndices = 3;
			Second.MaterialIndex = 1;
			Surface.Sections.Add( Second );
			return Surface;
		}

		// chunk配列の全頂点attributeとindexが同じかを比較する。
		bool SameChunks( const TArray<FMeshGeometryChunk>& A, const TArray<FMeshGeometryChunk>& B )
		{
			if ( A.Num() != B.Num() ) return false;
			for ( int32 ChunkIndex = 0; ChunkIndex < A.Num(); ++ChunkIndex )
			{
				if ( A[ChunkIndex].Vertices.Num() != B[ChunkIndex].Vertices.Num() || A[ChunkIndex].Indices.Num() != B[ChunkIndex].Indices.Num() ) return false;
				if ( !A[ChunkIndex].Vertices.IsEmpty() && std::memcmp( A[ChunkIndex].Vertices.GetData(), B[ChunkIndex].Vertices.GetData(), static_cast<size_t>( A[ChunkIndex].Vertices.Num() ) * sizeof( FMeshVertex ) ) != 0 ) return false;
				if ( !A[ChunkIndex].Indices.IsEmpty() && std::memcmp( A[ChunkIndex].Indices.GetData(), B[ChunkIndex].Indices.GetData(), static_cast<size_t>( A[ChunkIndex].Indices.Num() ) * sizeof( uint32 ) ) != 0 ) return false;
			}
			return true;
		}

		// ModelMaterial対応を個別に切り替えるrecord専用実行先。
		class FModelMaterialTestExecutor final : public IDrawExecutor
		{
		public:
			// テストでは命令実行せず必要時だけ対応能力を公開する。
			bool bSupportsEffects = false;
			void Execute( const FDrawSpan& Span ) override { (void)Span; }
			bool SupportsModelMaterialEffects() const override { return bSupportsEffects; }
		};

		// buffer内で指定されたcommand数を数える。
		int32 CountModelMaterialCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
		{
			int32 Count = 0;
			if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
			return Count;
		}

		// playback検査に使う、必須曲線なしの有効定義を作る。
		FMeshMaterialEffectDefinition MakeModelMaterialDefinition( float32 Opacity = 1.0f, EDrawBlend Blend = EDrawBlend::Alpha )
		{
			FMeshMaterialEffectDefinition Definition;
			Definition.Material.bEnabled = true;
			Definition.Duration = 1.0f;
			Definition.bLoop = true;
			Definition.Opacity = Opacity;
			Definition.Blend = Blend;
			return Definition;
		}

		// native visibilityを一時変更する実fixture値を保存し、失敗時も元へ戻す。
		struct FModelMaterialVisibilitySnapshot
		{
			int32 Handle = -1; // visibilityを切り替えるinstance handle
			int32 Mesh = -1; // 選択mesh番号
			int32 Frame = -1; // 選択frame番号
			int32 ParentFrame = -1; // 親frame番号
			int32 ModelVisible = -1; // 元model表示値
			int32 MeshVisible = -1; // 元mesh表示値
			int32 FrameVisible = -1; // 元frame表示値
			int32 ParentVisible = -1; // 元親frame表示値
			int32 CategoryVisible = -1; // 元normal category表示値
			bool bValid = false; // 全getterが有効値を返したか

			FModelMaterialVisibilitySnapshot( int32 InHandle, int32 InMesh, int32 InFrame, int32 InParentFrame )
				: Handle( InHandle ), Mesh( InMesh ), Frame( InFrame ), ParentFrame( InParentFrame )
			{
				if ( Handle < 0 || Mesh < 0 || Frame < 0 ) return;
				ModelVisible = MV1GetVisible( Handle );
				MeshVisible = MV1GetMeshVisible( Handle, Mesh );
				FrameVisible = MV1GetFrameVisible( Handle, Frame );
				ParentVisible = ParentFrame >= 0 ? MV1GetFrameVisible( Handle, ParentFrame ) : TRUE;
				CategoryVisible = MV1GetMeshCategoryVisible( Handle, DX_MV1_MESHCATEGORY_NORMAL );
				bValid = ( ModelVisible == FALSE || ModelVisible == TRUE ) && ( MeshVisible == FALSE || MeshVisible == TRUE ) && ( FrameVisible == FALSE || FrameVisible == TRUE ) && ( ParentVisible == FALSE || ParentVisible == TRUE ) && ( CategoryVisible == FALSE || CategoryVisible == TRUE );
			}

			~FModelMaterialVisibilitySnapshot() { Restore(); }

			// 全visibilityを元値へ戻し、Setterの成否を返す。
			bool Restore() const
			{
				if ( !bValid ) return false;
				const bool bModelRestored = MV1SetVisible( Handle, ModelVisible ) >= 0;
				const bool bMeshRestored = MV1SetMeshVisible( Handle, Mesh, MeshVisible ) >= 0;
				const bool bFrameRestored = MV1SetFrameVisible( Handle, Frame, FrameVisible ) >= 0;
				const bool bCategoryRestored = MV1SetMeshCategoryVisible( Handle, DX_MV1_MESHCATEGORY_NORMAL, CategoryVisible ) >= 0;
				const bool bParentRestored = ParentFrame < 0 || MV1SetFrameVisible( Handle, ParentFrame, ParentVisible ) >= 0;
				const bool bRestored = bModelRestored && bMeshRestored && bFrameRestored && bCategoryRestored && bParentRestored;
				return bRestored && MV1GetVisible( Handle ) == ModelVisible && MV1GetMeshVisible( Handle, Mesh ) == MeshVisible && MV1GetFrameVisible( Handle, Frame ) == FrameVisible && MV1GetMeshCategoryVisible( Handle, DX_MV1_MESHCATEGORY_NORMAL ) == CategoryVisible && ( ParentFrame < 0 || MV1GetFrameVisible( Handle, ParentFrame ) == ParentVisible );
			}
		};

		// 実画素試験の前後でDxLibの描画先・camera・world状態を戻す。
		struct FModelMaterialRealState
		{
			int32 Screen = GetDrawScreen(); // 呼出し前の描画先
			int32 ScreenReset = GetUseSetDrawScreenSettingReset(); // 描画先切替の自動復元設定
			int32 Culling = GetUseBackCulling(); // 元の裏面判定
			float32 Near = GetCameraNear(); // 元camera near
			float32 Far = GetCameraFar(); // 元camera far
			float32 Aspect = GetCameraDotAspect(); // 元camera aspect
			float32 Center[2] = {}; // 元camera center
			MATRIX CameraView = GetCameraViewMatrix(); // 元camera view
			MATRIX CameraProjection = GetCameraProjectionMatrix(); // 元camera projection
			MATRIX World{}; // 元world transform
			MATRIX View{}; // 元view transform
			MATRIX Projection{}; // 元projection transform
			MATRIX Viewport{}; // 元viewport transform
			RECT Area{}; // 元draw area
			int32 Blend[2] = {}; // 元blend modeとparameter
			int32 Bright[3] = {}; // 元RGB bright
			int32 Background[4] = {}; // 元背景RGBA
			bool bRestored = false; // 全状態を戻し終えたか

			FModelMaterialRealState()
			{
				GetCameraScreenCenter( &Center[0], &Center[1] );
				GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &Area );
				GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
				SetUseSetDrawScreenSettingReset( FALSE );
			}

			~FModelMaterialRealState()
			{
				Restore();
			}

			void Restore()
			{
				if ( bRestored ) return;
				RenderVertex(); SetDrawScreen( Screen );
				SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
				SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
				SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseBackCulling( Culling ); SetUseSetDrawScreenSettingReset( ScreenReset );
				bRestored = true;
			}
		};

		// hidden screenから全pixelを取得し、成功時だけtrueを返す。
		bool CaptureModelMaterialPixels( int32 Screen, int32 Soft, TArray<FColor32>& Out )
		{
			if ( GetDrawScreen() != Screen ) return false;
			if ( GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ) != 0 ) return false;
			Out.Reset();
			Out.Reserve( 128 * 128 );
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				int32 R = 0, G = 0, B = 0, A = 0; // 現在pixelのRGBA出力値
				if ( GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A ) != 0 ) return false;
				Out.Add( FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) ) );
			}
			(void)Screen;
			return Out.Num() == 128 * 128;
		}

		// 画像全体の差分数を返し背景・兄弟meshの変化も含めて比べる。
		int32 CountModelMaterialPixelDifferences( const TArray<FColor32>& A, const TArray<FColor32>& B )
		{
			if ( A.Num() != B.Num() ) return -1;
			int32 Different = 0; // 一致しないpixel数
			for ( int32 Index = 0; Index < A.Num(); ++Index ) if ( std::memcmp( &A[Index], &B[Index], sizeof( FColor32 ) ) != 0 ) ++Different;
			return Different;
		}

		// 指定pixel矩形内で背景以外の画素数を数える。
		int32 CountModelMaterialVisiblePixels( const TArray<FColor32>& Pixels, int32 Left, int32 Top, int32 Right, int32 Bottom )
		{
			if ( Pixels.Num() != 128 * 128 || Left < 0 || Top < 0 || Right > 128 || Bottom > 128 || Left >= Right || Top >= Bottom ) return -1;
			int32 Visible = 0; // 背景色以外のpixel数
			for ( int32 Y = Top; Y < Bottom; ++Y ) for ( int32 X = Left; X < Right; ++X )
			{
				const FColor32& Pixel = Pixels[Y * 128 + X];
				if ( Pixel.r != 17 || Pixel.g != 23 || Pixel.b != 31 ) ++Visible;
			}
			return Visible;
		}

		// 選択sectionの最初のtriangleを実world位置から画面へ投影する。
		struct FModelMaterialTriangleProjection
		{
			// projected triangleの画面X座標。
			float32 X[3] = {};
			// projected triangleの画面Y座標。
			float32 Y[3] = {};
		};

		// 選択材質の最初のtriangleをworld行列と現在cameraで投影し、失敗時はfalseを返す。
		bool ProjectModelMaterialTriangle( const FStaticModelSurfaceData& Surface, int32 MaterialIndex, int32 MeshIndex, const FMatrix4x4& World, FModelMaterialTriangleProjection& Out )
		{
			// 指定材質が持つ最初の三角形範囲。
			const FStaticModelSurfaceSection* SelectedSection = nullptr;
			for ( const FStaticModelSurfaceSection& Section : Surface.Sections )
			{
				if ( Section.MaterialIndex == MaterialIndex && Section.SourceMeshIndex == MeshIndex && Section.NumIndices >= 3 && Section.FirstIndex >= 0 && Section.FirstIndex <= Surface.Indices.Num() - 3 ) { SelectedSection = &Section; break; }
			}
			if ( SelectedSection == nullptr ) return false;
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				// 元surfaceの三角形cornerが参照する頂点番号。
				const uint32 VertexIndex = Surface.Indices[SelectedSection->FirstIndex + Corner];
				if ( VertexIndex >= static_cast<uint32>( Surface.Vertices.Num() ) ) return false;
				const FMeshVertex& Vertex = Surface.Vertices[static_cast<int32>( VertexIndex )];
				// model-local位置へinstance worldを掛けた座標。
				const float32 WorldX = Vertex.Position[0] * World.m[0][0] + Vertex.Position[1] * World.m[1][0] + Vertex.Position[2] * World.m[2][0] + World.m[3][0];
				const float32 WorldY = Vertex.Position[0] * World.m[0][1] + Vertex.Position[1] * World.m[1][1] + Vertex.Position[2] * World.m[2][1] + World.m[3][1];
				const float32 WorldZ = Vertex.Position[0] * World.m[0][2] + Vertex.Position[1] * World.m[1][2] + Vertex.Position[2] * World.m[2][2] + World.m[3][2];
				// native cameraで投影した画面座標。
				const VECTOR Screen = ConvWorldPosToScreenPos( VGet( WorldX, WorldY, WorldZ ) );
				if ( !std::isfinite( Screen.x ) || !std::isfinite( Screen.y ) || !std::isfinite( Screen.z ) ) return false;
				Out.X[Corner] = Screen.x;
				Out.Y[Corner] = Screen.y;
			}
			return true;
		}

		// 画面pixelが投影triangleの内側にあり、指定pixel幅だけ辺から離れているか返す。
		bool IsModelMaterialPixelInsideTriangle( float32 PixelX, float32 PixelY, const FModelMaterialTriangleProjection& Triangle, float32 MarginPixels )
		{
			// 頂点を一周したsigned triangle面積。
			float32 Area = 0.0f;
			for ( int32 Edge = 0; Edge < 3; ++Edge )
			{
				// 現在辺の次頂点。
				const int32 Next = ( Edge + 1 ) % 3;
				Area += Triangle.X[Edge] * Triangle.Y[Next] - Triangle.Y[Edge] * Triangle.X[Next];
			}
			if ( std::abs( Area ) <= 1.0e-4f ) return false;
			// 向きが時計回りかを揃える係数。
			const float32 Direction = Area > 0.0f ? 1.0f : -1.0f;
			for ( int32 Edge = 0; Edge < 3; ++Edge )
			{
				// 現在辺の次頂点。
				const int32 Next = ( Edge + 1 ) % 3;
				// 画面上での辺方向。
				const float32 EdgeX = Triangle.X[Next] - Triangle.X[Edge];
				const float32 EdgeY = Triangle.Y[Next] - Triangle.Y[Edge];
				// pixel margin判定に使う辺長。
				const float32 EdgeLength = std::sqrt( EdgeX * EdgeX + EdgeY * EdgeY );
				// 辺からpixel centerまでの向きを揃えたcross値。
				const float32 Side = Direction * ( EdgeX * ( PixelY - Triangle.Y[Edge] ) - EdgeY * ( PixelX - Triangle.X[Edge] ) );
				if ( EdgeLength <= 1.0e-4f || Side < MarginPixels * EdgeLength ) return false;
			}
			return true;
		}

		// projected triangle内部で背景以外の画素を数え、無効captureは-1を返す。
		int32 CountModelMaterialTrianglePixels( const TArray<FColor32>& Pixels, const FModelMaterialTriangleProjection& Triangle )
		{
			if ( Pixels.Num() != 128 * 128 ) return -1;
			// triangle内部の背景以外pixel数。
			int32 Count = 0;
			// 128x128 capture内のpixel位置。
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				if ( !IsModelMaterialPixelInsideTriangle( static_cast<float32>( X ) + 0.5f, static_cast<float32>( Y ) + 0.5f, Triangle, 1.5f ) ) continue;
				// 現在pixelのRGBA値。
				const FColor32& Pixel = Pixels[Y * 128 + X];
				if ( Pixel.r != 17 || Pixel.g != 23 || Pixel.b != 31 ) ++Count;
			}
			return Count;
		}

		// projected triangle内部の基準画像との画素差を数え、invalid imageなら-1を返す。
		int32 CountModelMaterialTriangleDifferences( const TArray<FColor32>& A, const TArray<FColor32>& B, const FModelMaterialTriangleProjection& Triangle )
		{
			if ( A.Num() != 128 * 128 || B.Num() != 128 * 128 ) return -1;
			// triangle内部で異なるpixel数。
			int32 Count = 0;
			// capture内のpixel位置。
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				if ( IsModelMaterialPixelInsideTriangle( static_cast<float32>( X ) + 0.5f, static_cast<float32>( Y ) + 0.5f, Triangle, 1.5f ) && std::memcmp( &A[Y * 128 + X], &B[Y * 128 + X], sizeof( FColor32 ) ) != 0 ) ++Count;
			}
			return Count;
		}

		// projected triangle内でRGB値が異なるpixel数を数え、無効画像は-1を返す。
		int32 CountModelMaterialTriangleRgbDifferences( const TArray<FColor32>& A, const TArray<FColor32>& B, const FModelMaterialTriangleProjection& Triangle )
		{
			if ( A.Num() != 128 * 128 || B.Num() != 128 * 128 ) return -1;
			int32 Count = 0; // RGBのいずれかが違うpixel数
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				if ( !IsModelMaterialPixelInsideTriangle( static_cast<float32>( X ) + 0.5f, static_cast<float32>( Y ) + 0.5f, Triangle, 1.5f ) ) continue;
				const FColor32& First = A[Y * 128 + X]; // 比較元の画素
				const FColor32& Second = B[Y * 128 + X]; // 比較先の画素
				if ( First.r != Second.r || First.g != Second.g || First.b != Second.b ) ++Count;
			}
			return Count;
		}

		// projected triangle内で背景か図形かの差を数え、無効画像は-1を返す。
		int32 CountModelMaterialTriangleSilhouetteDifferences( const TArray<FColor32>& A, const TArray<FColor32>& B, const FModelMaterialTriangleProjection& Triangle )
		{
			if ( A.Num() != 128 * 128 || B.Num() != 128 * 128 ) return -1;
			int32 Count = 0; // 背景/図形の分類が異なるpixel数
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				if ( !IsModelMaterialPixelInsideTriangle( static_cast<float32>( X ) + 0.5f, static_cast<float32>( Y ) + 0.5f, Triangle, 1.5f ) ) continue;
				const FColor32& First = A[Y * 128 + X]; // native基準の画素
				const FColor32& Second = B[Y * 128 + X]; // effect結果の画素
				const bool bFirstVisible = First.r != 17 || First.g != 23 || First.b != 31;
				const bool bSecondVisible = Second.r != 17 || Second.g != 23 || Second.b != 31;
				if ( bFirstVisible != bSecondVisible ) ++Count;
			}
			return Count;
		}

		// rootからsandboxへ捕獲画像を保存し、失敗時はlogへ記録する。
		void SaveModelMaterialDiagnosticPng( const char* Name )
		{
			// sandbox内のcapture保存先。
			const FString Directory = FString( "Saved" ) / "ModelMaterialDiagnostics";
			if ( !NFileSystem::EnsureDirectory( Directory ) ) { LOG( LogAssetSelfTest, Warning, "model-material capture directory creation failed: %s", *Directory ); return; }
			// 1 captureのsandbox内保存path。
			const FString Path = Directory / ( FString::FromUtf8( Name ) + ".png" );
			// PNG encoderの結果コード。
			const int32 Result = SaveDrawScreenToPNG( 0, 0, 128, 128, *Path, 1 );
			LOG( LogAssetSelfTest, Log, "model-material capture %s result=%d", *Path, Result );
		}
	}

	void RunStaticModelMaterialCoreChecks( FChecker& Checker )
	{
		Checker.Group = "model-material-core";
		// command family fixed payload and legacy commands remain unchanged.
		FDrawModelMaterialData Data = MakeValidModelMaterialCommand();
		Checker.Check( sizeof( FDrawModelMaterialData ) == 160 && std::is_trivially_copyable_v<FDrawModelMaterialData> && static_cast<uint8>( EDrawCommand::ModelMaterial3D ) == 26, "ModelMaterial固定payloadは160byte・末尾command番号26を保つ" );
		Checker.Check( NDrawModelMaterial::IsValid( Data ), "有効な選択材質命令値を受理する" );
		// 最後に記録したfixed payloadを読み戻すcommand view。
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		const bool bRecorded = Draw.ModelMaterial( Data );
		int32 CommandCount = 0;
		FDrawModelMaterialData Stored;
		if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::ModelMaterial3D ) return;
			++CommandCount;
			Stored = Command.Read<FDrawModelMaterialData>();
		} );
		Checker.Check( bRecorded && CommandCount == 1 && Buffer.Words.size() == 41 && std::memcmp( &Stored, &Data, sizeof( Data ) ) == 0, "固定ModelMaterial命令は41wordで値をそのまま所有記録する" );
		Data.Handle = -7;
		Data.Geometry = -9;
		Data.Opacity = 0.0f;
		Data.Material.Texture = 61;
		Checker.Check( Buffer.Words.size() == 41 && CommandCount == 1 && std::memcmp( &Stored, Buffer.Words.data() + 1, sizeof( Stored ) ) == 0, "入力headerを変更しても記録済みModelMaterial payloadを所有する" );
		// 失敗時に前のcommand buffer全体が保たれる基準値。
		const std::vector<uint32> BeforeInvalid = Buffer.Words;
		FDrawModelMaterialData Invalid = MakeValidModelMaterialCommand();
		Invalid.Opacity = std::numeric_limits<float32>::quiet_NaN();
		Checker.Check( !Draw.ModelMaterial( Invalid ) && Buffer.Words == BeforeInvalid, "非有限Opacityを拒否し記録済みcommandを変更しない" );
		Invalid = MakeValidModelMaterialCommand();
		Invalid.Material.NumVertices = 3;
		Checker.Check( !Draw.ModelMaterial( Invalid ) && Buffer.Words == BeforeInvalid, "reserved vertex countが非zeroなら拒否して記録bufferを保つ" );
		Invalid = MakeValidModelMaterialCommand();
		Invalid.BlendMode = static_cast<int32>( EDrawBlend::Multiply );
		Checker.Check( !Draw.ModelMaterial( Invalid ) && Buffer.Words == BeforeInvalid, "対応外blendを拒否して既存commandを保つ" );

		// 三角形fanが複数chunkへ分かれても元index順を復元する入力。
		TArray<FMeshVertex> Vertices;
		Vertices.Reserve( 1026 );
		for ( int32 Index = 0; Index < 1026; ++Index ) Vertices.Add( MakeIndexedVertex( Index ) );
		TArray<uint32> Indices;
		Indices.Reserve( 1024 * 3 );
		for ( uint32 Index = 1; Index <= 1024; ++Index ) { Indices.Add( 0 ); Indices.Add( Index ); Indices.Add( Index + 1 ); }
		TArray<FMeshGeometryChunk> Chunks;
		const bool bBuilt = NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( Vertices ), TArrayView<const uint32>( Indices ), Chunks );
		bool bChunkCaps = bBuilt && Chunks.Num() > 1;
		bool bTriangleOrderPreserved = bBuilt;
		int32 RecoveredIndexCount = 0;
		for ( const FMeshGeometryChunk& Chunk : Chunks )
		{
			bChunkCaps = bChunkCaps && Chunk.Vertices.Num() <= FDrawMaterialMeshData::MaxVertices && Chunk.Indices.Num() <= FDrawMaterialMeshData::MaxIndices && Chunk.Indices.Num() % 3 == 0;
			for ( const uint32 LocalIndex : Chunk.Indices )
			{
				if ( LocalIndex >= static_cast<uint32>( Chunk.Vertices.Num() ) ) { bTriangleOrderPreserved = false; continue; }
				++RecoveredIndexCount;
			}
		}
		Checker.Check( bChunkCaps && RecoveredIndexCount == 3072, "1024三角形fanを複数chunkへ分け各chunk上限を守る" );
		// 元index列の順序をchunk-local頂点位置から全量復元する配列。
		TArray<uint32> RecoveredIndices;
		RecoveredIndices.Reserve( Indices.Num() );
		for ( const FMeshGeometryChunk& Chunk : Chunks ) for ( const uint32 LocalIndex : Chunk.Indices )
		{
			if ( LocalIndex >= static_cast<uint32>( Chunk.Vertices.Num() ) ) { bTriangleOrderPreserved = false; continue; }
			RecoveredIndices.Add( static_cast<uint32>( std::lround( Chunk.Vertices[static_cast<int32>( LocalIndex )].Position[0] ) ) );
		}
		bTriangleOrderPreserved = bTriangleOrderPreserved && RecoveredIndices.Num() == Indices.Num();
		for ( int32 Index = 0; Index < RecoveredIndices.Num() && bTriangleOrderPreserved; ++Index ) bTriangleOrderPreserved = RecoveredIndices[Index] == Indices[Index];
		Checker.Check( bTriangleOrderPreserved, "chunk-local remapを戻すと元triangle順と重複indexが完全一致する" );
		// malformed input failure must not publish its partially constructed chunks.
		const TArray<FMeshGeometryChunk> ChunkSnapshot = Chunks;
		TArray<FMeshGeometryChunk> SentinelChunks = Chunks;
		TArray<uint32> InvalidIndices = { 0, 1, 2, 0 };
		const bool bMisalignedRejected = !NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( Vertices ), TArrayView<const uint32>( InvalidIndices ), SentinelChunks );
		Checker.Check( bMisalignedRejected && SameChunks( SentinelChunks, ChunkSnapshot ), "三角形境界不正時は途中chunkを公開せずOutの全値を保持する" );
		InvalidIndices = { 0, 1, 1026 };
		const bool bOutOfRangeRejected = !NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( Vertices ), TArrayView<const uint32>( InvalidIndices ), SentinelChunks );
		Checker.Check( bOutOfRangeRejected && SameChunks( SentinelChunks, ChunkSnapshot ), "範囲外index時はOutの全vertex attributeとindexを保つ" );
		const bool bNegativeCountRejected = !NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( Vertices.GetData(), -1 ), TArrayView<const uint32>( Indices ), SentinelChunks );
		Checker.Check( bNegativeCountRejected && SameChunks( SentinelChunks, ChunkSnapshot ), "負の頂点数を拒否しOutを完全に保つ" );
		const bool bNullViewRejected = !NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( nullptr, 3 ), TArrayView<const uint32>( Indices ), SentinelChunks );
		Checker.Check( bNullViewRejected && SameChunks( SentinelChunks, ChunkSnapshot ), "null頂点viewを拒否しOutを完全に保つ" );
	}

	void RunStaticModelMaterialFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory )
	{
		Checker.Group = "model-material-fake";
		// fake backend stateを他のasset testへ漏らさない退避値。
		const bool bOldSupport = Fake.bSupportModelMaterialGeometry;
		const int32 OldCreateCount = Fake.NumModelMaterialGeometryCreates;
		const int32 OldFailCall = Fake.FailModelMaterialGeometryCreateCall;
		const int32 OldNextGeometry = Fake.NextModelMaterialGeometry;
		const std::map<int32, TArray<FMeshGeometryChunk>> OldGeometries = Fake.ModelMaterialGeometries;
		const std::vector<int32> OldDeleted = Fake.DeletedModelMaterialGeometries;
		const bool bOldSurfaceSupport = Fake.bSupportStaticModelSurface;
		const FStaticModelSurfaceData OldSurface = Fake.StaticModelSurfaceData;
		const std::set<int32> OldLoading = Fake.Loading;
		Fake.ModelMaterialGeometries.clear();
		Fake.DeletedModelMaterialGeometries.clear();
		Fake.NumModelMaterialGeometryCreates = 0;
		Fake.FailModelMaterialGeometryCreateCall = 0;
		Fake.bSupportModelMaterialGeometry = false;
		FStaticModelSurfaceData Surface = MakeTwoMaterialSurface();
		Surface.Materials[0].BorrowedDiffuseTexture = 707;
		Surface.Materials[1].BorrowedDiffuseTexture = 708;
		const int32 Unsupported = Fake.CreateModelMaterialGeometry( Surface, 0 );
		Checker.Check( Unsupported < 0 && Fake.ModelMaterialGeometries.empty(), "未対応fake backendはgeometry作成を拒否し資源を残さない" );
		Fake.bSupportModelMaterialGeometry = true;
		// 異なる材質slotから独立資源を作る結果handle。
		const int32 RedGeometry = Fake.CreateModelMaterialGeometry( Surface, 0 );
		const int32 BlueGeometry = Fake.CreateModelMaterialGeometry( Surface, 1 );
		const auto Red = Fake.ModelMaterialGeometries.find( RedGeometry );
		const auto Blue = Fake.ModelMaterialGeometries.find( BlueGeometry );
		const bool bSelectedSectionsOnly = RedGeometry >= 0 && BlueGeometry >= 0 && RedGeometry != BlueGeometry && Red != Fake.ModelMaterialGeometries.end() && Blue != Fake.ModelMaterialGeometries.end() && Red->second.Num() == 1 && Blue->second.Num() == 1 && Red->second[0].Indices.Num() == 3 && Blue->second[0].Indices.Num() == 3 && Red->second[0].Vertices.Num() == 3 && Blue->second[0].Vertices.Num() == 3 && Red->second[0].Vertices[0].Position[0] == 0.0f && Blue->second[0].Vertices[0].Position[0] == 3.0f;
		Checker.Check( bSelectedSectionsOnly, "fake geometryは選択material sectionの三角形だけを別handleへcopyする" );
		// backendへ渡した後に変えても所有済みgeometryに漏れない確認値。
		if ( Surface.Vertices.IsValidIndex( 0 ) ) Surface.Vertices[0].Position[0] = 99.0f;
		const auto RedAfterSourceChange = Fake.ModelMaterialGeometries.find( RedGeometry );
		Checker.Check( RedAfterSourceChange != Fake.ModelMaterialGeometries.end() && !RedAfterSourceChange->second.IsEmpty() && RedAfterSourceChange->second[0].Vertices[0].Position[0] == 0.0f, "fake geometryはsource surface変更後も頂点copyを保持する" );
		const size_t BeforeFailure = Fake.ModelMaterialGeometries.size();
		Fake.FailModelMaterialGeometryCreateCall = Fake.NumModelMaterialGeometryCreates + 1;
		Checker.Check( Fake.CreateModelMaterialGeometry( Surface, 0 ) < 0 && Fake.ModelMaterialGeometries.size() == BeforeFailure, "注入したgeometry作成失敗は部分資源を残さない" );
		Fake.FailModelMaterialGeometryCreateCall = 0;
		Checker.Check( Fake.CreateModelMaterialGeometry( Surface, 2 ) < 0 && Fake.ModelMaterialGeometries.size() == BeforeFailure, "範囲外material indexはgeometryを作らない" );
		Fake.DeleteModelMaterialGeometry( RedGeometry );
		Fake.DeleteModelMaterialGeometry( BlueGeometry );
		Checker.Check( Fake.ModelMaterialGeometries.empty() && Fake.DeletedModelMaterialGeometries.size() == 2 && Fake.DeletedModelMaterialGeometries[0] == RedGeometry && Fake.DeletedModelMaterialGeometries[1] == BlueGeometry, "geometry解放はfake資源を消しbackend解放順を記録する" );

		// asset managerを経由したstatic snapshotとruntime rendererの個体状態。
		Fake.bSupportStaticModelSurface = true;
		Fake.bSupportModelMaterialGeometry = true;
		Fake.StaticModelSurfaceData = MakeTwoMaterialSurface();
		Fake.StaticModelSurfaceData.Materials[0].BorrowedDiffuseTexture = 707;
		Fake.StaticModelSurfaceData.Materials[1].BorrowedDiffuseTexture = 708;
		const FString ModelPath = "model-material-fake/model.mv1";
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( Directory / "model-material-fake" ) && WriteFileText( Directory / ModelPath, "fake static surface" );
		TSharedPtr<CModel> Model = bFixtureWritten ? Assets.LoadModel( ModelPath ) : TSharedPtr<CModel>();
		TSharedPtr<CModelInstance> FirstInstance = Model.IsValid() ? Model->CreateInstance() : TSharedPtr<CModelInstance>();
		TSharedPtr<CModelInstance> SiblingInstance = Model.IsValid() ? Model->CreateInstance() : TSharedPtr<CModelInstance>();
		TWeakPtr<CModel> SourceWeak = Model;
		FModelAnimator FirstAnimator( FirstInstance );
		FModelAnimator SiblingAnimator( SiblingInstance );
		FModelMaterialTestExecutor UnsupportedExecutor;
		FModelMaterialTestExecutor SupportedExecutor;
		SupportedExecutor.bSupportsEffects = true;
		FMeshMaterialEffectDefinition Definition = MakeModelMaterialDefinition( 0.5f, EDrawBlend::Add );
		const auto EmptyTextureLoader = []( const FString& ) { return TSharedPtr<CTexture>(); };
		// 未対応と未準備は従来のfull Model commandへ戻る基準。
		FDrawBuffer NativeBefore;
		FDrawContext NativeBeforeDraw( NativeBefore );
		FirstAnimator.Record( NativeBeforeDraw, FMatrix4x4::Identity );
		const int32 GeometryCallsBeforeUnsupported = Fake.NumModelMaterialGeometryCreates;
		const bool bUnsupportedRejected = !FirstAnimator.PrepareMaterialEffect( 0, Definition, EmptyTextureLoader, UnsupportedExecutor );
		FDrawBuffer UnsupportedFallback;
		FDrawContext UnsupportedDraw( UnsupportedFallback );
		FirstAnimator.Record( UnsupportedDraw, FMatrix4x4::Identity );
		Checker.Check( bFixtureWritten && Model.IsValid() && FirstInstance.IsValid() && SiblingInstance.IsValid() && bUnsupportedRejected && UnsupportedFallback.Words == NativeBefore.Words && Fake.NumModelMaterialGeometryCreates == GeometryCallsBeforeUnsupported, "未対応executorはgeometryを作らず旧Model命令byteへ戻る" );
		FModelAnimator FailedPrepareAnimator( FirstInstance );
		Fake.FailModelMaterialGeometryCreateCall = Fake.NumModelMaterialGeometryCreates + 1;
		const bool bGeometryFailureRejected = !FailedPrepareAnimator.PrepareMaterialEffect( 0, Definition, EmptyTextureLoader, SupportedExecutor );
		FDrawBuffer FailedPrepareFallback;
		FDrawContext FailedPrepareDraw( FailedPrepareFallback );
		FailedPrepareAnimator.Record( FailedPrepareDraw, FMatrix4x4::Identity );
		const bool bFailedPrepareFallback = bGeometryFailureRejected && FailedPrepareFallback.Words == NativeBefore.Words && Fake.ModelMaterialGeometries.empty();
		Fake.FailModelMaterialGeometryCreateCall = 0;
		Checker.Check( bFailedPrepareFallback, "backend geometry準備失敗は部分資源を解放して旧Model commandへ戻す" );
		const int32 QueriesBeforePrepare = Fake.NumStaticModelSurfaceQueries;
		const int32 GeometryBeforePrepare = Fake.NumModelMaterialGeometryCreates;
		const bool bPrepared = FirstAnimator.PrepareMaterialEffect( 0, Definition, EmptyTextureLoader, SupportedExecutor );
		const size_t NumGeometryAfterPrepare = Fake.ModelMaterialGeometries.size();
		const int32 QueriesAfterPrepare = Fake.NumStaticModelSurfaceQueries;
		const int32 GeometryAfterPrepare = Fake.NumModelMaterialGeometryCreates;
		// 行列を最後の12 affine要素まで与え、記録payloadで二重変換がないことを読む値。
		FMatrix4x4 World = FMatrix4x4::Identity;
		World.m[0][0] = 2.0f;
		World.m[0][1] = 0.25f;
		World.m[1][1] = 3.0f;
		World.m[2][2] = 0.5f;
		World.m[3][0] = 8.0f;
		World.m[3][1] = -2.0f;
		FDrawBuffer EffectBuffer;
		FDrawContext EffectDraw( EffectBuffer );
		FirstAnimator.Record( EffectDraw, World );
		FDrawModelMaterialData EffectCommand;
		bool bEffectPayloadFound = false;
		if ( !EffectBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ EffectBuffer.Words.data(), static_cast<int32>( EffectBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::ModelMaterial3D ) return;
			EffectCommand = Command.Read<FDrawModelMaterialData>();
			bEffectPayloadFound = true;
		} );
		const bool bMatrixStoredOnce = bEffectPayloadFound && EffectCommand.M[0] == 2.0f && EffectCommand.M[1] == 0.25f && EffectCommand.M[4] == 3.0f && EffectCommand.M[8] == 0.5f && EffectCommand.M[9] == 8.0f && EffectCommand.M[10] == -2.0f;
		// 有効instance以外や非有限変換でのRecord判断を調べるbuffer。
		FDrawBuffer InvalidWorldBuffer;
		FDrawContext InvalidWorldDraw( InvalidWorldBuffer );
		FMatrix4x4 InvalidWorld = FMatrix4x4::Identity;
		InvalidWorld.m[0][0] = std::numeric_limits<float32>::quiet_NaN();
		const bool bInvalidWorldHandled = FirstAnimator.GetMaterialEffect().Record( InvalidWorldDraw, FirstInstance, InvalidWorld );
		// 別sourceとanimation attached状態を確認するnative fallback。
		WriteFileText( Directory / "model-material-fake/other.mv1", "other fake static surface" );
		TSharedPtr<CModel> OtherModel = Assets.LoadModel( "model-material-fake/other.mv1" );
		TSharedPtr<CModelInstance> OtherInstance = OtherModel.IsValid() ? OtherModel->CreateInstance() : TSharedPtr<CModelInstance>();
		FDrawBuffer MismatchBuffer;
		FDrawContext MismatchDraw( MismatchBuffer );
		const bool bSourceMismatchHandled = OtherInstance.IsValid() && !FirstAnimator.GetMaterialEffect().Record( MismatchDraw, OtherInstance, World );
		const int32 AttachIndex = FirstInstance.IsValid() ? FirstInstance->AttachAnim( Model, 0 ) : INDEX_NONE;
		FDrawBuffer AttachedBuffer;
		FDrawContext AttachedDraw( AttachedBuffer );
		const bool bAttachedFallback = AttachIndex >= 0 && !FirstAnimator.GetMaterialEffect().Record( AttachedDraw, FirstInstance, World );
		if ( AttachIndex >= 0 ) FirstInstance->DetachAnim( AttachIndex );
		FDrawBuffer SiblingBefore;
		FDrawContext SiblingBeforeDraw( SiblingBefore );
		SiblingAnimator.Record( SiblingBeforeDraw, FMatrix4x4::Identity );
		FirstAnimator.Update( 0.2f );
		FModelMaterialEffectRenderer IndependentClock = FirstAnimator.GetMaterialEffect();
		IndependentClock.Update( 0.4f );
		const bool bIndependentClock = std::abs( FirstAnimator.GetMaterialEffect().GetEffectTime() - 0.2 ) < 1.0e-6 && std::abs( IndependentClock.GetEffectTime() - 0.6 ) < 1.0e-6;
		IndependentClock.Pause();
		IndependentClock.Update( 0.2f );
		const bool bPauseRetainsClock = std::abs( IndependentClock.GetEffectTime() - 0.6 ) < 1.0e-6;
		IndependentClock.Play();
		IndependentClock.Update( 0.1f );
		const bool bPlayResumesClock = std::abs( IndependentClock.GetEffectTime() - 0.7 ) < 1.0e-6;
		IndependentClock.Restart();
		const bool bRestartStartsAtZero = IndependentClock.GetEffectTime() == 0.0;
		IndependentClock.Update( 0.15f );
		IndependentClock.Stop();
		const bool bStopResetsClock = IndependentClock.GetEffectTime() == 0.0;
		FDrawBuffer StoppedBuffer;
		FDrawContext StoppedDraw( StoppedBuffer );
		FirstAnimator.GetMaterialEffect().Stop();
		FirstAnimator.Record( StoppedDraw, FMatrix4x4::Identity );
		FDrawBuffer SiblingAfter;
		FDrawContext SiblingAfterDraw( SiblingAfter );
		SiblingAnimator.Record( SiblingAfterDraw, FMatrix4x4::Identity );
		const int32 QueriesBeforeRecordLoop = Fake.NumStaticModelSurfaceQueries;
		const int32 GeometryBeforeRecordLoop = Fake.NumModelMaterialGeometryCreates;
		const int32 LoadsBeforeRecordLoop = Fake.NumTextureLoads;
		IndependentClock.Restart();
		for ( int32 Iteration = 0; Iteration < 8; ++Iteration ) { FDrawBuffer Repeated; FDrawContext RepeatedDraw( Repeated ); IndependentClock.Record( RepeatedDraw, FirstInstance, World ); }
		const bool bRecordIsResourceFree = Fake.NumStaticModelSurfaceQueries == QueriesBeforeRecordLoop && Fake.NumModelMaterialGeometryCreates == GeometryBeforeRecordLoop && Fake.NumTextureLoads == LoadsBeforeRecordLoop;
		Checker.Check( bPrepared && FirstAnimator.GetMaterialEffect().IsPrepared() && bEffectPayloadFound && CountModelMaterialCommands( EffectBuffer, EDrawCommand::ModelMaterial3D ) == 1 && CountModelMaterialCommands( EffectBuffer, EDrawCommand::Model3D ) == 0 && EffectCommand.Handle == FirstInstance->GetHandle() && EffectCommand.Geometry >= 0 && EffectCommand.BlendMode == static_cast<int32>( EDrawBlend::Add ) && EffectCommand.Opacity == 0.5f && bMatrixStoredOnce, "準備済み個体はselected material命令一件だけを出しOpacity・Add・world行列を固定payloadへ一度記録する" );
		Checker.Check( bInvalidWorldHandled && InvalidWorldBuffer.Words.empty() && bSourceMismatchHandled && MismatchBuffer.Words.empty() && bAttachedFallback && AttachedBuffer.Words.empty(), "非有限worldはhandled無記録としsource不一致・animation attachedはeffect描画を引き受けない" );
		Checker.Check( CountModelMaterialCommands( SiblingBefore, EDrawCommand::Model3D ) == 1 && SiblingBefore.Words == SiblingAfter.Words && StoppedBuffer.Words == NativeBefore.Words, "Sibling個体のnative命令はeffect再生中も不変でStop後は旧Model command bytesへ戻る" );
		Checker.Check( bIndependentClock && bPauseRetainsClock && bPlayResumesClock && bRestartStartsAtZero && bStopResetsClock, "再生時計copyは独立しPlay/Pause/Stop/Restartの個別状態を保つ" );
		Checker.Check( bRecordIsResourceFree && QueriesAfterPrepare == QueriesBeforePrepare + 1 && GeometryAfterPrepare == GeometryBeforePrepare + 1 && NumGeometryAfterPrepare == 1, "準備時だけsnapshotとgeometryを取得しRecord反復ではresource query・loadをしない" );
		// source・borrowed imageは最後のrenderer copyが残る間だけ生存する。
		FirstAnimator.ClearMaterialEffect();
		OtherInstance.Reset(); OtherModel.Reset();
		FailedPrepareAnimator.Instance.Reset();
		FirstAnimator.Instance.Reset();
		SiblingAnimator.Instance.Reset();
		FirstInstance.Reset();
		SiblingInstance.Reset();
		Model.Reset();
		Assets.ReleaseUnused();
		TSharedPtr<CModel> PinnedWhileRendererLives = SourceWeak.Pin();
		Checker.Check( PinnedWhileRendererLives.IsValid() && Fake.ModelMaterialGeometries.size() == 1, "renderer copyはsourceと借用画像ownerとgeometryを保持する" );
		PinnedWhileRendererLives.Reset();
		IndependentClock.Clear();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		Checker.Check( Fake.ModelMaterialGeometries.empty() && !SourceWeak.IsValid(), "最後のeffect資源解放後にgeometryとsource modelを片付ける" );
		Fake.Loading = OldLoading;
		Fake.bSupportModelMaterialGeometry = bOldSupport;
		Fake.NumModelMaterialGeometryCreates = OldCreateCount;
		Fake.FailModelMaterialGeometryCreateCall = OldFailCall;
		Fake.NextModelMaterialGeometry = OldNextGeometry;
		Fake.ModelMaterialGeometries = OldGeometries;
		Fake.DeletedModelMaterialGeometries = OldDeleted;
		Fake.bSupportStaticModelSurface = bOldSurfaceSupport;
		Fake.StaticModelSurfaceData = OldSurface;
	}

	void RunStaticModelMaterialRealChecks( FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory )
	{
		Checker.Group = "model-material-real";
		// 全体状態を退避し、hidden capture resourceを最後に削除するguard。
		FModelMaterialRealState SavedState; // caller画面状態を戻すguard
		const int32 Screen = MakeScreen( 128, 128, TRUE ); // hidden native draw target
		const int32 Soft = MakeSoftImage( 128, 128 ); // capture画像を読むCPU image
		const bool bPackOffFixtureWritten = NFileSystem::EnsureDirectory( Directory / "model_material_pack_off" ) && WriteStaticModelSurfaceFixture( Directory / "model_material_pack_off", "model.x" ); // pack OFF source fixture
		const bool bPackOffSettingAccepted = MV1SetLoadModelUsePackDraw( FALSE ) >= 0; // load時のpack OFF設定結果
		const FString ModelPath = "model_material_pack_off/model.x"; // manager root相対model path
		TSharedPtr<CModel> Model = bPackOffFixtureWritten ? Assets.LoadModel( ModelPath ) : TSharedPtr<CModel>(); // pack OFF元model
		TSharedPtr<CModelInstance> First = Model.IsValid() ? Model->CreateInstance() : TSharedPtr<CModelInstance>(); // 効果を付ける個体
		TSharedPtr<CModelInstance> Sibling = Model.IsValid() ? Model->CreateInstance() : TSharedPtr<CModelInstance>(); // nativeのまま残す兄弟個体
		TWeakPtr<CModel> ModelWeak = Model; // 最後の所有参照解放を確かめるweak参照
		const bool bPackOnFixtureWritten = NFileSystem::EnsureDirectory( Directory / "model_material_pack_on" ) && WriteStaticModelSurfaceFixture( Directory / "model_material_pack_on", "model.x" ); // pack ON source fixture
		const bool bPackOnSettingAccepted = MV1SetLoadModelUsePackDraw( TRUE ) >= 0; // load時のpack ON設定結果
		TSharedPtr<CModel> PackOnModel = bPackOnFixtureWritten ? Assets.LoadModel( "model_material_pack_on/model.x" ) : TSharedPtr<CModel>(); // pack ON元model
		MV1SetLoadModelUsePackDraw( FALSE );
		TSharedPtr<CModelInstance> PackOnInstance = PackOnModel.IsValid() ? PackOnModel->CreateInstance() : TSharedPtr<CModelInstance>(); // pack ON描画用個体
		TWeakPtr<CModel> PackOnWeak = PackOnModel; // pack ON source解放確認用
		const bool bExecutorSupported = Executor.SupportsModelMaterialEffects(); // 実executorの能力
		Checker.Check( bPackOffFixtureWritten && bPackOffSettingAccepted && bPackOnFixtureWritten && bPackOnSettingAccepted && Screen >= 0 && Soft >= 0 && Model.IsValid() && First.IsValid() && Sibling.IsValid() && PackOnModel.IsValid() && PackOnInstance.IsValid(), "pack OFF・ONを指定して2triangle・4material model fixtureを読み込む" );
		Checker.Check( bExecutorSupported, "DxLib executorはstatic model material描画を実行できる" );
		TSharedPtr<const FStaticModelSurface> Surface = Model.IsValid() ? Model->CreateStaticSurfaceSnapshot() : TSharedPtr<const FStaticModelSurface>(); // sectionの実materialを選ぶsnapshot
		int32 MaterialIndex = INDEX_NONE; // 埋込画像を持つ材質index
		int32 SelectedMeshIndex = INDEX_NONE; // 対象sectionのnative mesh index
		int32 SelectedFrameIndex = INDEX_NONE; // 対象sectionを所有するnative frame index
		int32 OtherMaterialIndex = INDEX_NONE; // 非対象材質pixelを比べるsection
		int32 OtherMeshIndex = INDEX_NONE; // 非対象材質sectionのnative mesh
		if ( Surface.IsValid() ) for ( const FStaticModelSurfaceSection& Section : Surface->GetData().Sections )
		{
			if ( Section.MaterialIndex < 0 || Section.MaterialIndex >= Surface->GetData().Materials.Num() || Surface->GetData().Materials[Section.MaterialIndex].BorrowedDiffuseTexture < 0 ) continue;
			MaterialIndex = Section.MaterialIndex;
			SelectedMeshIndex = Section.SourceMeshIndex;
			SelectedFrameIndex = Section.SourceFrameIndex;
			break;
		}
		if ( Surface.IsValid() && MaterialIndex >= 0 ) for ( const FStaticModelSurfaceSection& Section : Surface->GetData().Sections )
		{
			if ( Section.MaterialIndex == MaterialIndex || Section.MaterialIndex < 0 || Section.MaterialIndex >= Surface->GetData().Materials.Num() || Section.SourceMeshIndex < 0 || Section.NumIndices < 3 ) continue;
			OtherMaterialIndex = Section.MaterialIndex;
			OtherMeshIndex = Section.SourceMeshIndex;
			break;
		}
		const bool bHasSelectedSection = MaterialIndex >= 0 && SelectedMeshIndex >= 0; // pixel試験へ進めるmesh selection
		Checker.Check( bHasSelectedSection, "fixtureの埋込textureを持つ三角形から実在material・mesh indexを選ぶ" );
		if ( Screen >= 0 && Soft >= 0 && Model.IsValid() && First.IsValid() && Sibling.IsValid() && bExecutorSupported && bHasSelectedSection && MaterialIndex >= 0 )
		{
			// Captureを同一native camera・背景・draw stateで繰り返す。
			SetDrawScreen( Screen ); SetDrawAreaFull(); SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 17, 23, 31, 255 ); SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 ); SetUseBackCulling( DX_CULLING_NONE );
			SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 2.0f, 1.0f, 10.0f ), VGet( 2.0f, 1.0f, 0.0f ) );
			FMatrix4x4 CenteredWorld = FMatrix4x4::Identity; // fixtureを画面中央へ寄せるmodel world
			CenteredWorld.m[3][0] = -2.0f; CenteredWorld.m[3][1] = -1.0f;
			FModelMaterialTriangleProjection CenteredTriangle; // native triangleの実screen位置から作るpixel ROI
			const bool bCenteredTriangleProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, CenteredWorld, CenteredTriangle );
			FModelMaterialTriangleProjection OtherTriangle; // 非対象材質triangleの独立ROI
			const bool bOtherTriangleProjected = Surface.IsValid() && OtherMaterialIndex >= 0 && ProjectModelMaterialTriangle( Surface->GetData(), OtherMaterialIndex, OtherMeshIndex, CenteredWorld, OtherTriangle ); // 非対象材質triangleの投影結果
			const int32 NativeMeshCount = MV1GetMeshNum( First->GetHandle() ); // native modelが所有するmesh数
			const int32 NativeFrameCount = MV1GetFrameNum( First->GetHandle() ); // native modelが所有するframe数
			const int32 SelectedNativeMaterial = SelectedMeshIndex >= 0 && SelectedMeshIndex < NativeMeshCount ? MV1GetMeshMaterial( First->GetHandle(), SelectedMeshIndex ) : -1; // selected meshのnative material mapping
			const int32 SelectedNativeVisible = SelectedMeshIndex >= 0 && SelectedMeshIndex < NativeMeshCount ? MV1GetMeshVisible( First->GetHandle(), SelectedMeshIndex ) : -1; // selected meshのnative visibility
			const int32 SelectedNativeCulling = SelectedMeshIndex >= 0 && SelectedMeshIndex < NativeMeshCount ? MV1GetMeshBackCulling( First->GetHandle(), SelectedMeshIndex ) : -1; // selected mesh固有の裏面除去設定
			int32 OtherNativeMeshIndex = INDEX_NONE; // selected以外の比較用mesh番号
			for ( int32 MeshIndex = 0; MeshIndex < NativeMeshCount; ++MeshIndex ) if ( MeshIndex != SelectedMeshIndex ) { OtherNativeMeshIndex = MeshIndex; break; }
			const int32 OtherNativeMaterial = OtherNativeMeshIndex >= 0 ? MV1GetMeshMaterial( First->GetHandle(), OtherNativeMeshIndex ) : -1; // 比較meshのnative material mapping
			const int32 OtherNativeCulling = OtherNativeMeshIndex >= 0 ? MV1GetMeshBackCulling( First->GetHandle(), OtherNativeMeshIndex ) : -1; // 比較mesh固有の裏面除去設定
			const int32 SelectedFrameVisible = SelectedFrameIndex >= 0 && SelectedFrameIndex < NativeFrameCount ? MV1GetFrameVisible( First->GetHandle(), SelectedFrameIndex ) : -1; // selected frameのnative visibility
			const int32 SelectedFrameParent = SelectedFrameIndex >= 0 && SelectedFrameIndex < NativeFrameCount ? MV1GetFrameParent( First->GetHandle(), SelectedFrameIndex ) : -1; // selected frameの親番号
			LOG( LogAssetSelfTest, Log, "材質試験mapping: 対象材質=%d mesh=%d native材質=%d mesh表示=%d frame表示=%d frame=%d 親=%d 投影=%d p0=(%.2f,%.2f) p1=(%.2f,%.2f) p2=(%.2f,%.2f) 対照mesh=%d native材質=%d 裏面除去=%d/%d", MaterialIndex, SelectedMeshIndex, SelectedNativeMaterial, SelectedNativeVisible, SelectedFrameVisible, SelectedFrameIndex, SelectedFrameParent, bCenteredTriangleProjected ? 1 : 0, CenteredTriangle.X[0], CenteredTriangle.Y[0], CenteredTriangle.X[1], CenteredTriangle.Y[1], CenteredTriangle.X[2], CenteredTriangle.Y[2], OtherNativeMeshIndex, OtherNativeMaterial, SelectedNativeCulling, OtherNativeCulling );
			FModelAnimator FirstAnimator( First ); // selected effectを持つ個体
			FModelAnimator SiblingAnimator( Sibling ); // native-only兄弟個体
			FModelAnimator PlainAnimator( First ); // effect未準備のnative positive control
			const auto DrawPixels = [&]( const FModelAnimator& Animator, const FMatrix4x4& World, TArray<FColor32>& Pixels, const char* CaptureName )
			{
				SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
				FDrawBuffer Buffer;
				FDrawContext Draw( Buffer );
				Animator.Record( Draw, World );
				if ( Buffer.Words.empty() ) return false;
				Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
				RenderVertex();
				const bool bCaptured = CaptureModelMaterialPixels( Screen, Soft, Pixels );
				if ( bCaptured ) SaveModelMaterialDiagnosticPng( CaptureName );
				return bCaptured;
			};
			// model本体handleを直接描き、source資源のnative画素を採る。
			const auto DrawSourcePixels = [&]( const FMatrix4x4& World, TArray<FColor32>& Pixels, const char* CaptureName )
			{
				const MATRIX OldMatrix = MV1GetMatrix( Model->GetHandle() ); // source handleの元行列
				const MATRIX NewMatrix = static_cast<MATRIX>( World ); // source captureへ使うinstance行列
				RenderVertex(); SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
				const bool bMatrixSet = MV1SetMatrix( Model->GetHandle(), NewMatrix ) >= 0;
				if ( bMatrixSet ) MV1DrawModel( Model->GetHandle() );
				RenderVertex();
				const bool bCaptured = bMatrixSet && CaptureModelMaterialPixels( Screen, Soft, Pixels );
				const bool bMatrixRestored = MV1SetMatrix( Model->GetHandle(), OldMatrix ) >= 0;
				RenderVertex();
				const MATRIX RestoredMatrix = MV1GetMatrix( Model->GetHandle() );
				if ( bCaptured ) SaveModelMaterialDiagnosticPng( CaptureName );
				return bCaptured && bMatrixRestored && std::memcmp( &RestoredMatrix, &OldMatrix, sizeof( MATRIX ) ) == 0;
			};
			// effect未準備のsourceと兄弟個体それぞれのnative positive baseline。
			TArray<FColor32> NativePixels; // effect前の対象native image
			TArray<FColor32> SiblingPixelsBefore; // effect前の兄弟native image
			TArray<FColor32> SourcePixelsBefore; // effect前のsource model native image
			const bool bNativeCaptured = DrawPixels( FirstAnimator, CenteredWorld, NativePixels, "native" );
			const bool bSiblingCaptured = DrawPixels( SiblingAnimator, CenteredWorld, SiblingPixelsBefore, "sibling_before" );
			const bool bSourceCaptured = DrawSourcePixels( CenteredWorld, SourcePixelsBefore, "source_before" );
			const int32 NativeBaselineTrianglePixels = bCenteredTriangleProjected && bNativeCaptured ? CountModelMaterialTrianglePixels( NativePixels, CenteredTriangle ) : -1; // 効果準備前のnative三角形の可視画素数
			const int32 NativeOtherTrianglePixels = bOtherTriangleProjected && bNativeCaptured ? CountModelMaterialTrianglePixels( NativePixels, OtherTriangle ) : -1; // 非対象native材質の基準表示数
			LOG( LogAssetSelfTest, Log, "材質試験native基準: 撮影=%d 投影=%d 対象三角形の可視画素=%d 裏面除去=%d 対照mesh裏面除去=%d", bNativeCaptured ? 1 : 0, bCenteredTriangleProjected ? 1 : 0, NativeBaselineTrianglePixels, SelectedNativeCulling, OtherNativeCulling );
			Checker.Check( bCenteredTriangleProjected && bNativeCaptured && NativeBaselineTrianglePixels > 0, "効果準備前のnative三角形が投影領域内に表示される" );
			Checker.Check( bOtherTriangleProjected && bNativeCaptured && NativeOtherTrianglePixels > 0, "非対象材質のnative三角形も投影領域内に表示される" );
			Checker.Check( bSourceCaptured && bCenteredTriangleProjected && CountModelMaterialTrianglePixels( SourcePixelsBefore, CenteredTriangle ) > 0, "source model handleからnative三角形を直接描ける" );
			const FBackendMemoryInfo BeforePrepare = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo(); // prepare直前native handle数
			FMeshMaterialEffectDefinition Definition = MakeModelMaterialDefinition( 0.0f, EDrawBlend::Alpha ); // 完全透明Alpha効果
			Definition.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
			Definition.Material.Emission.Keys.Add( { 1.0f, 1.0f } );
			const bool bPrepared = FirstAnimator.PrepareMaterialEffect( MaterialIndex, Definition, [&]( const FString& Path ) { return Assets.LoadTexture( Path ); }, Executor, true ); // selected material effect準備結果
			const FBackendMemoryInfo AfterPrepare = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo(); // prepare直後native handle数
			TArray<FColor32> TransparentPixels; // Opacity 0で消した対象画像
			const bool bTransparentCaptured = DrawPixels( FirstAnimator, CenteredWorld, TransparentPixels, "opacity_zero" );
			// 合成方式とOpacityだけを替え、同じselected triangleの実画素を採取する。
			const auto CaptureEffectVariant = [&]( float32 Opacity, EDrawBlend Blend, bool bTwoSided, const char* CaptureName, TArray<FColor32>& Pixels )
			{
				FirstAnimator.ClearMaterialEffect();
				FMeshMaterialEffectDefinition Variant = MakeModelMaterialDefinition( Opacity, Blend );
				Variant.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
				Variant.Material.Emission.Keys.Add( { 1.0f, 1.0f } );
				const bool bVariantPrepared = FirstAnimator.PrepareMaterialEffect( MaterialIndex, Variant, [&]( const FString& Path ) { return Assets.LoadTexture( Path ); }, Executor, bTwoSided );
				return bVariantPrepared && DrawPixels( FirstAnimator, CenteredWorld, Pixels, CaptureName );
			};
			TArray<FColor32> OpacityOnePixels; // Opacity 1 Alpha結果
			TArray<FColor32> OpacityHalfAlphaPixels; // Opacity 0.5 Alpha結果
			TArray<FColor32> OpacityHalfAddPixels; // Opacity 0.5 Add結果
			const bool bOpacityOneCaptured = CaptureEffectVariant( 1.0f, EDrawBlend::Alpha, true, "opacity_one", OpacityOnePixels );
			const bool bOpacityHalfAlphaCaptured = CaptureEffectVariant( 0.5f, EDrawBlend::Alpha, true, "opacity_half_alpha", OpacityHalfAlphaPixels );
			const bool bOpacityHalfAddCaptured = CaptureEffectVariant( 0.5f, EDrawBlend::Add, true, "opacity_half_add", OpacityHalfAddPixels );
			const TArray<FColor32> SiblingPixelsAfterCapture = [&]() { TArray<FColor32> Pixels; DrawPixels( SiblingAnimator, CenteredWorld, Pixels, "sibling_after" ); return Pixels; }(); // 効果描画後もnative兄弟を再取得
			int32 SelectedDifferences = bCenteredTriangleProjected ? CountModelMaterialTriangleDifferences( NativePixels, TransparentPixels, CenteredTriangle ) : -1; // 投影triangle内で変わったpixel数
			int32 SiblingDifferences = 0; // 兄弟画像で変わったpixel数
			SiblingDifferences = CountModelMaterialPixelDifferences( SiblingPixelsBefore, SiblingPixelsAfterCapture );
			const int32 NativeSelectedPixels = NativeBaselineTrianglePixels;
			const int32 NativeOtherPixels = NativeOtherTrianglePixels;
			const int32 ZeroOpacityPixels = bCenteredTriangleProjected ? CountModelMaterialTrianglePixels( TransparentPixels, CenteredTriangle ) : -1;
			const int32 FullOpacityPixels = bCenteredTriangleProjected ? CountModelMaterialTrianglePixels( OpacityOnePixels, CenteredTriangle ) : -1;
			const int32 HalfOpacityPixels = bCenteredTriangleProjected ? CountModelMaterialTrianglePixels( OpacityHalfAlphaPixels, CenteredTriangle ) : -1;
			const int32 OtherOpacityZeroRgbDifferences = bOtherTriangleProjected ? CountModelMaterialTriangleRgbDifferences( NativePixels, TransparentPixels, OtherTriangle ) : -1;
			const int32 OtherOpacityOneRgbDifferences = bOtherTriangleProjected ? CountModelMaterialTriangleRgbDifferences( NativePixels, OpacityOnePixels, OtherTriangle ) : -1;
			const int32 OtherOpacityHalfAddRgbDifferences = bOtherTriangleProjected ? CountModelMaterialTriangleRgbDifferences( NativePixels, OpacityHalfAddPixels, OtherTriangle ) : -1;
			int32 BlendDifferences = 0;
			if ( OpacityHalfAlphaPixels.Num() == 128 * 128 && OpacityHalfAddPixels.Num() == 128 * 128 ) for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				const int32 Index = Y * 128 + X;
				if ( bCenteredTriangleProjected && IsModelMaterialPixelInsideTriangle( static_cast<float32>( X ) + 0.5f, static_cast<float32>( Y ) + 0.5f, CenteredTriangle, 1.5f ) && std::memcmp( &OpacityHalfAlphaPixels[Index], &OpacityHalfAddPixels[Index], sizeof( FColor32 ) ) != 0 ) ++BlendDifferences;
			}
			LOG( LogAssetSelfTest, Log, "model-material captures native=%d opacity0=%d op1=%d op0.5alpha=%d op0.5add=%d sibling=%d/%d prepare=%d selectedROI diff=%d native=%d opacity0=%d opacity1=%d opacity0.5=%d blendDiff=%d otherMaterial=%d otherNative=%d RGB差分=%d/%d/%d", bNativeCaptured ? 1 : 0, bTransparentCaptured ? 1 : 0, bOpacityOneCaptured ? 1 : 0, bOpacityHalfAlphaCaptured ? 1 : 0, bOpacityHalfAddCaptured ? 1 : 0, bSiblingCaptured ? 1 : 0, SiblingDifferences, bPrepared ? 1 : 0, SelectedDifferences, NativeSelectedPixels, ZeroOpacityPixels, FullOpacityPixels, HalfOpacityPixels, BlendDifferences, OtherMaterialIndex, NativeOtherPixels, OtherOpacityZeroRgbDifferences, OtherOpacityOneRgbDifferences, OtherOpacityHalfAddRgbDifferences );
			Checker.Check( bCenteredTriangleProjected && bNativeCaptured && !NativePixels.IsEmpty() && SelectedDifferences > 0 && NativeSelectedPixels > 0 && ZeroOpacityPixels == 0, "native fixtureに像がありOpacity 0の選択材質効果で対象triangleが透明になる" );
			Checker.Check( bOtherTriangleProjected && NativeOtherPixels > 0 && bTransparentCaptured && bOpacityOneCaptured && bOpacityHalfAddCaptured && OtherOpacityZeroRgbDifferences == 0 && OtherOpacityOneRgbDifferences == 0 && OtherOpacityHalfAddRgbDifferences == 0, "同一effect個体の非対象材質はOpacity 0・1・half Addでもnative RGBを保つ" );
			Checker.Check( bOpacityOneCaptured && bOpacityHalfAlphaCaptured && bOpacityHalfAddCaptured && FullOpacityPixels > 0 && HalfOpacityPixels > 0 && BlendDifferences > 0, "Opacity 1・0.5とAlpha・Addでselected triangleのpixel出力が変わる" );
			TArray<FColor32> VisibilityBaselinePixels; // opacity 1で全材質が表示されたinstance基準
			const bool bVisibilityBaselineCaptured = CaptureEffectVariant( 1.0f, EDrawBlend::Alpha, true, "visibility_baseline", VisibilityBaselinePixels );
			const FModelMaterialVisibilitySnapshot VisibilityState( First->GetHandle(), SelectedMeshIndex, SelectedFrameIndex, SelectedFrameParent ); // visibilityを戻すinstance状態
			const int32 SourceModelVisibleBeforeVisibility = MV1GetVisible( Model->GetHandle() ); // 元source model表示値
			const int32 SourceMeshVisibleBeforeVisibility = MV1GetMeshVisible( Model->GetHandle(), SelectedMeshIndex ); // 元source mesh表示値
			const int32 SourceCategoryVisibleBeforeVisibility = MV1GetMeshCategoryVisible( Model->GetHandle(), DX_MV1_MESHCATEGORY_NORMAL ); // 元source category表示値
			// 個別visibilityを切って実画像と復元画像を比較する。
			const auto CheckVisibilitySuppression = [&]( const char* Name, const char* HiddenName, const char* RestoredName, bool bApplicable, auto SetHidden )
			{
				TArray<FColor32> HiddenPixels; // visibilityを切ったeffect画素
				TArray<FColor32> SiblingDuringPixels; // visibility切替中の兄弟native画素
				TArray<FColor32> SourceDuringPixels; // visibility切替中のsource native画素
				TArray<FColor32> RestoredPixels; // 元状態へ戻したeffect画素
				const int32 SetResult = bApplicable ? SetHidden() : -1; // visibility setterの結果コード
				const bool bHiddenCaptured = SetResult >= 0 && DrawPixels( FirstAnimator, CenteredWorld, HiddenPixels, HiddenName ); // hidden状態のeffect capture成否
				const int32 HiddenTrianglePixels = bCenteredTriangleProjected && bHiddenCaptured ? CountModelMaterialTrianglePixels( HiddenPixels, CenteredTriangle ) : -1; // hidden状態で残った対象pixel数
				const bool bSiblingDuringCaptured = SetResult >= 0 && DrawPixels( SiblingAnimator, CenteredWorld, SiblingDuringPixels, "sibling_visibility_during" ); // siblingの同時capture成否
				const bool bSourceDuringCaptured = SetResult >= 0 && DrawSourcePixels( CenteredWorld, SourceDuringPixels, "source_visibility_during" ); // sourceの同時capture成否
				const int32 SiblingDuringDifferences = bSiblingDuringCaptured ? CountModelMaterialPixelDifferences( SiblingPixelsBefore, SiblingDuringPixels ) : -1; // sibling baselineとの差分数
				const int32 SourceDuringDifferences = bSourceDuringCaptured ? CountModelMaterialPixelDifferences( SourcePixelsBefore, SourceDuringPixels ) : -1; // source baselineとの差分数
				const bool bRestored = VisibilityState.Restore(); // 全visibilityを保存値へ戻した結果
				const bool bRestoredCaptured = bVisibilityBaselineCaptured && DrawPixels( FirstAnimator, CenteredWorld, RestoredPixels, RestoredName ); // 復元状態のcapture成否
				const int32 RestoredDifferences = bRestoredCaptured ? CountModelMaterialPixelDifferences( VisibilityBaselinePixels, RestoredPixels ) : -1; // 復元画像との全差分数
				LOG( LogAssetSelfTest, Log, "材質visibility試験: %s setter=%d hiddenCapture=%d hiddenPixels=%d sourceDiff=%d siblingDiff=%d restore=%d restoredCapture=%d restoredDiff=%d", Name, SetResult, bHiddenCaptured ? 1 : 0, HiddenTrianglePixels, SourceDuringDifferences, SiblingDuringDifferences, bRestored ? 1 : 0, bRestoredCaptured ? 1 : 0, RestoredDifferences );
				Checker.Check( VisibilityState.bValid && bVisibilityBaselineCaptured && SetResult >= 0 && bHiddenCaptured && HiddenTrianglePixels == 0 && bSiblingDuringCaptured && SiblingDuringDifferences == 0 && bSourceDuringCaptured && SourceDuringDifferences == 0 && bRestored && bRestoredCaptured && RestoredDifferences == 0, "native visibility切替は対象材質だけを隠しsource・Siblingを保って復元する (%s)", Name );
			};
			Checker.Check( VisibilityState.bValid && SelectedFrameParent >= 0, "visibility試験fixtureは有効なmodel・mesh・frame・parentを持つ" );
			CheckVisibilitySuppression( "model", "model_hidden", "model_restored", VisibilityState.bValid, [&]() { return MV1SetVisible( First->GetHandle(), FALSE ); } );
			CheckVisibilitySuppression( "mesh", "mesh_hidden", "mesh_restored", VisibilityState.bValid, [&]() { return MV1SetMeshVisible( First->GetHandle(), SelectedMeshIndex, FALSE ); } );
			CheckVisibilitySuppression( "frame", "frame_hidden", "frame_restored", VisibilityState.bValid, [&]() { return MV1SetFrameVisible( First->GetHandle(), SelectedFrameIndex, FALSE ); } );
			CheckVisibilitySuppression( "parent frame", "parent_hidden", "parent_restored", VisibilityState.bValid && SelectedFrameParent >= 0, [&]() { return MV1SetFrameVisible( First->GetHandle(), SelectedFrameParent, FALSE ); } );
			CheckVisibilitySuppression( "normal category", "category_hidden", "category_restored", VisibilityState.bValid, [&]() { return MV1SetMeshCategoryVisible( First->GetHandle(), DX_MV1_MESHCATEGORY_NORMAL, FALSE ); } );
			TArray<FColor32> SiblingPixelsAfterVisibility; // visibility切替後の別個体画像
			const bool bSiblingAfterVisibilityCaptured = DrawPixels( SiblingAnimator, CenteredWorld, SiblingPixelsAfterVisibility, "sibling_after_visibility" ); // 全切替後のsibling capture成否
			const bool bSourceVisibilityUnchanged = MV1GetVisible( Model->GetHandle() ) == SourceModelVisibleBeforeVisibility && MV1GetMeshVisible( Model->GetHandle(), SelectedMeshIndex ) == SourceMeshVisibleBeforeVisibility && MV1GetMeshCategoryVisible( Model->GetHandle(), DX_MV1_MESHCATEGORY_NORMAL ) == SourceCategoryVisibleBeforeVisibility; // sourceの元表示値維持
			Checker.Check( bSiblingCaptured && bSiblingAfterVisibilityCaptured && CountModelMaterialPixelDifferences( SiblingPixelsBefore, SiblingPixelsAfterVisibility ) == 0 && bSourceVisibilityUnchanged, "visibility切替はsourceと別instanceへ漏れずSibling画素も維持する" );
			const int32 FirstNativeCullBefore = MV1GetMeshBackCulling( First->GetHandle(), SelectedMeshIndex ); // effect個体の元native culling
			const int32 SourceNativeCullBefore = MV1GetMeshBackCulling( Model->GetHandle(), SelectedMeshIndex ); // sourceの元native culling
			const int32 SiblingNativeCullBefore = MV1GetMeshBackCulling( Sibling->GetHandle(), SelectedMeshIndex ); // siblingの元native culling
			const int32 GlobalNativeCullBefore = GetUseBackCulling(); // 描画全体へ設定された裏面判定
			const int32 CullModes[3] = { DX_CULLING_LEFT, DX_CULLING_RIGHT, DX_CULLING_NONE }; // 比較するnative mesh culling値
			int32 CullSilhouetteMismatches = 0; // nativeと片面effectで違ったsilhouette数
			int32 TwoSidedMissingPixels = 0; // 両面effectで消えたpositive pixel数
			int32 NativeFrontPixels = 0; // 正面cameraでnative表示された最大pixel数
			int32 NativeBackPixels = 0; // 背面cameraでnative表示された最大pixel数
			bool bCullCapturesValid = VisibilityState.bValid && FirstNativeCullBefore >= 0 && SourceNativeCullBefore >= 0 && SiblingNativeCullBefore >= 0; // culling試験の事前状態が有効か
			// three handleに設定したnative cullingを各々の元値へ戻す。
			const auto RestoreNativeCull = [&]()
			{
				const bool bFirstSet = MV1SetMeshBackCulling( First->GetHandle(), SelectedMeshIndex, FirstNativeCullBefore ) >= 0;
				const bool bSourceSet = MV1SetMeshBackCulling( Model->GetHandle(), SelectedMeshIndex, SourceNativeCullBefore ) >= 0;
				const bool bSiblingSet = MV1SetMeshBackCulling( Sibling->GetHandle(), SelectedMeshIndex, SiblingNativeCullBefore ) >= 0;
				return bFirstSet && bSourceSet && bSiblingSet && MV1GetMeshBackCulling( First->GetHandle(), SelectedMeshIndex ) == FirstNativeCullBefore && MV1GetMeshBackCulling( Model->GetHandle(), SelectedMeshIndex ) == SourceNativeCullBefore && MV1GetMeshBackCulling( Sibling->GetHandle(), SelectedMeshIndex ) == SiblingNativeCullBefore;
			};
			for ( int32 CullIndex = 0; CullIndex < 3; ++CullIndex )
			{
				const int32 SetCullResult = MV1SetMeshBackCulling( First->GetHandle(), SelectedMeshIndex, CullModes[CullIndex] ); // 今回試すnative mesh cullingの設定結果
				bCullCapturesValid = bCullCapturesValid && SetCullResult >= 0;
				for ( int32 SideIndex = 0; SideIndex < 2; ++SideIndex )
				{
					SetCameraPositionAndTarget_UpVecY( VGet( 2.0f, 1.0f, SideIndex == 0 ? 10.0f : -10.0f ), VGet( 2.0f, 1.0f, 0.0f ) );
					FModelMaterialTriangleProjection CullTriangle; // 現cameraでnativeとeffectを比べる投影範囲
					const bool bCullProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, CenteredWorld, CullTriangle ); // 現cameraのtriangle投影結果
					TArray<FColor32> CullNativePixels; // 対応cullingのnative pixel基準
					TArray<FColor32> CullOneSidedPixels; // 対応cullingの片面効果画素
					TArray<FColor32> CullTwoSidedPixels; // 対応cullingの両面効果画素
					const bool bCullNativeCaptured = DrawPixels( PlainAnimator, CenteredWorld, CullNativePixels, "native_culling" ); // native meshの画素取得結果
					const bool bCullOneSidedCaptured = CaptureEffectVariant( 1.0f, EDrawBlend::Alpha, false, "effect_one_sided_culling", CullOneSidedPixels ); // 片面effectの画素取得結果
					const bool bCullTwoSidedCaptured = CaptureEffectVariant( 1.0f, EDrawBlend::Alpha, true, "effect_two_sided_culling", CullTwoSidedPixels ); // 両面effectの画素取得結果
					const int32 CullNativeVisible = bCullProjected && bCullNativeCaptured ? CountModelMaterialTrianglePixels( CullNativePixels, CullTriangle ) : -1; // native triangleの表示数
					const int32 CullOneSidedVisible = bCullProjected && bCullOneSidedCaptured ? CountModelMaterialTrianglePixels( CullOneSidedPixels, CullTriangle ) : -1; // 片面effectの表示数
					const int32 CullTwoSidedVisible = bCullProjected && bCullTwoSidedCaptured ? CountModelMaterialTrianglePixels( CullTwoSidedPixels, CullTriangle ) : -1; // 両面effectの表示数
					const int32 CullSilhouetteDifference = bCullProjected && bCullNativeCaptured && bCullOneSidedCaptured ? CountModelMaterialTriangleSilhouetteDifferences( CullNativePixels, CullOneSidedPixels, CullTriangle ) : -1; // nativeと片面effectのsilhouette差
					LOG( LogAssetSelfTest, Log, "材質culling試験: meshCull=%d cameraSide=%s 投影=%d nativeCapture=%d nativePixels=%d oneSidedCapture=%d oneSidedPixels=%d silhouetteDiff=%d twoSidedCapture=%d twoSidedPixels=%d globalCull=%d", CullModes[CullIndex], SideIndex == 0 ? "正面" : "背面", bCullProjected ? 1 : 0, bCullNativeCaptured ? 1 : 0, CullNativeVisible, bCullOneSidedCaptured ? 1 : 0, CullOneSidedVisible, CullSilhouetteDifference, bCullTwoSidedCaptured ? 1 : 0, CullTwoSidedVisible, GetUseBackCulling() );
				bCullCapturesValid = bCullCapturesValid && bCullProjected && bCullNativeCaptured && bCullOneSidedCaptured && bCullTwoSidedCaptured && CullNativeVisible >= 0 && CullOneSidedVisible >= 0 && CullTwoSidedVisible > 0 && CullSilhouetteDifference == 0 && GetUseBackCulling() == GlobalNativeCullBefore;
				CullSilhouetteMismatches += CullSilhouetteDifference < 0 ? 1 : CullSilhouetteDifference;
				if ( CullTwoSidedVisible <= 0 ) ++TwoSidedMissingPixels;
				if ( SideIndex == 0 ) NativeFrontPixels += std::max( CullNativeVisible, 0 ); else NativeBackPixels += std::max( CullNativeVisible, 0 );
			}
			if ( !RestoreNativeCull() ) bCullCapturesValid = false;
			SetCameraPositionAndTarget_UpVecY( VGet( 2.0f, 1.0f, 10.0f ), VGet( 2.0f, 1.0f, 0.0f ) );
			const bool bCullStateRestored = RestoreNativeCull() && GetUseBackCulling() == GlobalNativeCullBefore; // source・instance・global設定の復元結果
			TArray<FColor32> NativePixelsAfterCull; // culling設定復元後のnative positive image
			TArray<FColor32> SiblingPixelsAfterCull; // culling設定復元後の兄弟native image
			TArray<FColor32> SourcePixelsAfterCull; // culling設定復元後のsource native image
			const bool bNativeAfterCullCaptured = DrawPixels( PlainAnimator, CenteredWorld, NativePixelsAfterCull, "native_culling_restored" ); // 元instanceのnative再取得結果
			const bool bSiblingAfterCullCaptured = DrawPixels( SiblingAnimator, CenteredWorld, SiblingPixelsAfterCull, "sibling_culling_restored" ); // sibling再取得結果
			const bool bSourceAfterCullCaptured = DrawSourcePixels( CenteredWorld, SourcePixelsAfterCull, "source_culling_restored" ); // source再取得結果
			const bool bCullPixelsRestored = bNativeCaptured && bNativeAfterCullCaptured && CountModelMaterialPixelDifferences( NativePixels, NativePixelsAfterCull ) == 0 && bSiblingCaptured && bSiblingAfterCullCaptured && CountModelMaterialPixelDifferences( SiblingPixelsBefore, SiblingPixelsAfterCull ) == 0 && bSourceCaptured && bSourceAfterCullCaptured && CountModelMaterialPixelDifferences( SourcePixelsBefore, SourcePixelsAfterCull ) == 0; // three native imageの復元一致
			Checker.Check( bCullCapturesValid && CullSilhouetteMismatches == 0 && TwoSidedMissingPixels == 0 && NativeFrontPixels > 0 && NativeBackPixels > 0 && bCullStateRestored && bCullPixelsRestored, "LEFT・RIGHT・NONEでnativeと片面effectのculling領域が一致し、両面effectは前後cameraへ表示して設定とnative画像を戻す" );
			FDrawBuffer ValidEffectBuffer; // executorへ渡す既知の有効fixed command
			FDrawContext ValidEffectDraw( ValidEffectBuffer ); // 有効なeffect命令を記録するcontext
			FirstAnimator.Record( ValidEffectDraw, CenteredWorld );
			const bool bValidEffectCommand = CountModelMaterialCommands( ValidEffectBuffer, EDrawCommand::ModelMaterial3D ) == 1 && ValidEffectBuffer.Words.size() == 41; // 既知有効命令の個数と長さ
			const int32 MatrixVisibleBeforeMalformed = MV1GetVisible( First->GetHandle() ); // malformed試験前model表示
			const int32 MeshVisibleBeforeMalformed = MV1GetMeshVisible( First->GetHandle(), SelectedMeshIndex ); // malformed試験前mesh表示
			const int32 CullBeforeMalformed = MV1GetMeshBackCulling( First->GetHandle(), SelectedMeshIndex ); // malformed試験前mesh裏面設定
			const MATRIX ModelMatrixBeforeMalformed = MV1GetMatrix( First->GetHandle() ); // malformed試験前instance行列
			MATRIX WorldMatrixBeforeMalformed{}; // malformed試験前描画world行列
			GetTransformToWorldMatrix( &WorldMatrixBeforeMalformed );
			int32 BlendBeforeMalformed = DX_BLENDMODE_NOBLEND; // malformed試験前blend方式
			int32 BlendParamBeforeMalformed = 0; // malformed試験前blend強度
			GetDrawBlendMode( &BlendBeforeMalformed, &BlendParamBeforeMalformed );
			// 直接executorへ渡した不正commandの後にscreenを読む。
			const auto CaptureMalformedCommand = [&]( const FDrawBuffer& Buffer, TArray<FColor32>& Pixels )
			{
				if ( Buffer.Words.empty() ) return false;
				SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
				Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
				RenderVertex();
				return CaptureModelMaterialPixels( Screen, Soft, Pixels );
			};
			FDrawBuffer TrailingWordBuffer = ValidEffectBuffer; // 不正な追加word付きcommand
			bool bTrailingHeaderChanged = false; // 余分なwordをheader長へ含めたか
			if ( !TrailingWordBuffer.Words.empty() )
			{
				const uint32 OriginalHeader = TrailingWordBuffer.Words[0]; // 元command flagsを残すheader
				TrailingWordBuffer.Words[0] = NDrawCommand::MakeHeader( EDrawCommand::ModelMaterial3D, static_cast<uint8>( ( OriginalHeader >> 8 ) & 0xFFu ), 42 );
				TrailingWordBuffer.Words.push_back( 0xDEADBEEFu );
				bTrailingHeaderChanged = true;
			}
			TArray<FColor32> TrailingWordPixels; // 余分なwordを拒否した後の画面
			const bool bTrailingWordCaptured = CaptureMalformedCommand( TrailingWordBuffer, TrailingWordPixels ); // 余分なword commandの描画後capture成否
			FDrawBuffer NonFinitePayloadBuffer = ValidEffectBuffer; // matrixへNaNを含むcommand
			bool bNonFinitePayloadChanged = false; // 非有限値をpayloadへ格納したか
			if ( NonFinitePayloadBuffer.Words.size() == 41 )
			{
				FDrawModelMaterialData InvalidData{}; // 直接executorへ渡す不正payload
				std::memcpy( &InvalidData, NonFinitePayloadBuffer.Words.data() + 1, sizeof( InvalidData ) );
				InvalidData.M[0] = std::numeric_limits<float32>::quiet_NaN();
				std::memcpy( NonFinitePayloadBuffer.Words.data() + 1, &InvalidData, sizeof( InvalidData ) );
				bNonFinitePayloadChanged = true;
			}
			TArray<FColor32> NonFinitePixels; // NaN payloadを拒否した後の画面
			const bool bNonFiniteCaptured = CaptureMalformedCommand( NonFinitePayloadBuffer, NonFinitePixels ); // 非有限payloadの描画後capture成否
			MATRIX WorldMatrixAfterMalformed{}; // malformed実行後の描画world行列
			GetTransformToWorldMatrix( &WorldMatrixAfterMalformed );
			const MATRIX ModelMatrixAfterMalformed = MV1GetMatrix( First->GetHandle() ); // malformed実行後のinstance行列
			int32 BlendAfterMalformed = DX_BLENDMODE_NOBLEND; // malformed実行後blend方式
			int32 BlendParamAfterMalformed = 0; // malformed実行後blend強度
			GetDrawBlendMode( &BlendAfterMalformed, &BlendParamAfterMalformed );
			const bool bMalformedStateUnchanged = MV1GetVisible( First->GetHandle() ) == MatrixVisibleBeforeMalformed && MV1GetMeshVisible( First->GetHandle(), SelectedMeshIndex ) == MeshVisibleBeforeMalformed && MV1GetMeshBackCulling( First->GetHandle(), SelectedMeshIndex ) == CullBeforeMalformed && std::memcmp( &ModelMatrixAfterMalformed, &ModelMatrixBeforeMalformed, sizeof( MATRIX ) ) == 0 && std::memcmp( &WorldMatrixAfterMalformed, &WorldMatrixBeforeMalformed, sizeof( MATRIX ) ) == 0 && BlendAfterMalformed == BlendBeforeMalformed && BlendParamAfterMalformed == BlendParamBeforeMalformed && GetUseBackCulling() == GlobalNativeCullBefore;
			const int32 TrailingWordVisiblePixels = bTrailingWordCaptured ? CountModelMaterialVisiblePixels( TrailingWordPixels, 0, 0, 128, 128 ) : -1;
			const int32 NonFiniteVisiblePixels = bNonFiniteCaptured ? CountModelMaterialVisiblePixels( NonFinitePixels, 0, 0, 128, 128 ) : -1;
			LOG( LogAssetSelfTest, Log, "材質不正command試験: validControl=%d trailing=%d header=%d trailingVisible=%d nonfinite=%d payload=%d nonfiniteVisible=%d stateRestored=%d", bValidEffectCommand ? 1 : 0, bTrailingWordCaptured ? 1 : 0, bTrailingHeaderChanged ? 1 : 0, TrailingWordVisiblePixels, bNonFiniteCaptured ? 1 : 0, bNonFinitePayloadChanged ? 1 : 0, NonFiniteVisiblePixels, bMalformedStateUnchanged ? 1 : 0 );
			Checker.Check( bValidEffectCommand && bOpacityOneCaptured && FullOpacityPixels > 0, "不正command試験の前に有効なModelMaterial命令が実triangleを描く" );
			Checker.Check( bTrailingHeaderChanged && bTrailingWordCaptured && TrailingWordVisiblePixels == 0 && bMalformedStateUnchanged, "fixed payload末尾にwordを足したcommandは描画せずnative状態を保つ" );
			Checker.Check( bNonFinitePayloadChanged && bNonFiniteCaptured && NonFiniteVisiblePixels == 0 && bMalformedStateUnchanged, "NaN行列payloadを直接executorへ渡しても描画せずnative状態を保つ" );
			Checker.Check( bPrepared && FirstAnimator.GetMaterialEffect().IsPrepared() && AfterPrepare.NumModelHandles == BeforePrepare.NumModelHandles && AfterPrepare.NumTextureHandles >= BeforePrepare.NumTextureHandles, "実Model material準備はnative modelを複製せず効果資源を保持する" );
			Checker.Check( bSiblingCaptured && SiblingDifferences == 0, "別個体はeffect準備・描画後もnative画素を保つ" );
				FirstAnimator.ClearMaterialEffect();
			FirstAnimator.PrepareMaterialEffect( MaterialIndex, Definition, [&]( const FString& Path ) { return Assets.LoadTexture( Path ); }, Executor, true );
			FirstAnimator.GetMaterialEffect().Stop();
			TArray<FColor32> StoppedPixels;
			const bool bStoppedCaptured = DrawPixels( FirstAnimator, CenteredWorld, StoppedPixels, "stopped" );
			Checker.Check( bStoppedCaptured && CountModelMaterialPixelDifferences( NativePixels, StoppedPixels ) == 0, "Stop後の全native画素はeffect前と一致する" );
			if ( bPrepared )
			{
				// 同一instanceへの先行native命令を別位置へ残し、その後の透明effectがflushを保つか確認する。
				FMatrix4x4 EarlierWorld = CenteredWorld;
				EarlierWorld.m[3][0] = -4.0f;
				FMatrix4x4 LaterWorld = CenteredWorld;
				LaterWorld.m[3][0] = 2.0f;
				FModelMaterialTriangleProjection EarlierTriangle; // 先行native drawの実投影範囲
				FModelMaterialTriangleProjection LaterTriangle; // 後続effect drawの実投影範囲
				const bool bEarlierProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, EarlierWorld, EarlierTriangle );
				const bool bLaterProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, LaterWorld, LaterTriangle );
				TArray<FColor32> EarlierNativePixels; // 先行native位置だけのpositive image
				TArray<FColor32> LaterNativePixels; // 後続effect位置のnative positive image
				const bool bEarlierNativeCaptured = DrawPixels( PlainAnimator, EarlierWorld, EarlierNativePixels, "same_instance_earlier_native" );
				const bool bLaterNativeCaptured = DrawPixels( PlainAnimator, LaterWorld, LaterNativePixels, "same_instance_later_native" );
				FirstAnimator.GetMaterialEffect().Restart();
				FDrawBuffer Sequence;
				FDrawContext SequenceDraw( Sequence );
				PlainAnimator.Record( SequenceDraw, EarlierWorld );
				FirstAnimator.Record( SequenceDraw, LaterWorld );
				SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
				const bool bSequenceHasBothCommands = CountModelMaterialCommands( Sequence, EDrawCommand::Model3D ) == 1 && CountModelMaterialCommands( Sequence, EDrawCommand::ModelMaterial3D ) == 1;
				Executor.Execute( FDrawSpan{ Sequence.Words.data(), static_cast<int32>( Sequence.Words.size() ) } ); RenderVertex();
				TArray<FColor32> SequencePixels;
				const bool bSequenceCaptured = CaptureModelMaterialPixels( Screen, Soft, SequencePixels );
				if ( bSequenceCaptured ) SaveModelMaterialDiagnosticPng( "same_instance_sequence" );
				const int32 EarlierExpectedPixels = bEarlierProjected ? CountModelMaterialTrianglePixels( EarlierNativePixels, EarlierTriangle ) : -1;
				const int32 EarlierSequencePixels = bEarlierProjected ? CountModelMaterialTrianglePixels( SequencePixels, EarlierTriangle ) : -1;
				const int32 LaterExpectedPixels = bLaterProjected ? CountModelMaterialTrianglePixels( LaterNativePixels, LaterTriangle ) : -1;
				const int32 LaterTransparentPixels = bLaterProjected ? CountModelMaterialTrianglePixels( SequencePixels, LaterTriangle ) : -1;
				LOG( LogAssetSelfTest, Log, "model-material sequence commands=%d captured=%d earlierBaseline=%d earlierProjected=%d earlierROI=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f) expected=%d sequence=%d laterBaseline=%d laterProjected=%d laterROI=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f) expected=%d afterEffect=%d selectedMeshVisibleAfter=%d", bSequenceHasBothCommands ? 1 : 0, bSequenceCaptured ? 1 : 0, bEarlierNativeCaptured ? 1 : 0, bEarlierProjected ? 1 : 0, EarlierTriangle.X[0], EarlierTriangle.Y[0], EarlierTriangle.X[1], EarlierTriangle.Y[1], EarlierTriangle.X[2], EarlierTriangle.Y[2], EarlierExpectedPixels, EarlierSequencePixels, bLaterNativeCaptured ? 1 : 0, bLaterProjected ? 1 : 0, LaterTriangle.X[0], LaterTriangle.Y[0], LaterTriangle.X[1], LaterTriangle.Y[1], LaterTriangle.X[2], LaterTriangle.Y[2], LaterExpectedPixels, LaterTransparentPixels, MV1GetMeshVisible( First->GetHandle(), SelectedMeshIndex ) );
				Checker.Check( bEarlierNativeCaptured && bLaterNativeCaptured && bEarlierProjected && bLaterProjected && bSequenceHasBothCommands && bSequenceCaptured && EarlierExpectedPixels > 0 && EarlierSequencePixels == EarlierExpectedPixels && LaterExpectedPixels > 0 && LaterTransparentPixels == 0, "native同instance先行drawはflush後も残り、後続Opacity 0の選択材質は実native領域から消える" );
			}
			// model packed drawを有効にした別sourceでも同じnative・effect・stop経路を確認する。
			FModelAnimator PackOnAnimator( PackOnInstance ); // pack ON instanceのeffect playback
			FModelAnimator PackOnPlainAnimator( PackOnInstance ); // pack ON instanceのnative positive control
			TArray<FColor32> PackOnNativePixels; // pack ON effect前のnative image
			TArray<FColor32> PackOnEffectPixels; // pack ON effect中のimage
			const bool bPackOnNativeCaptured = DrawPixels( PackOnPlainAnimator, CenteredWorld, PackOnNativePixels, "pack_on_native" ); // pack ON native capture成否
			const bool bPackOnPrepared = PackOnAnimator.PrepareMaterialEffect( MaterialIndex, Definition, [&]( const FString& Path ) { return Assets.LoadTexture( Path ); }, Executor, true ); // pack ON effect準備成否
			const bool bPackOnEffectCaptured = DrawPixels( PackOnAnimator, CenteredWorld, PackOnEffectPixels, "pack_on_effect" ); // pack ON effect capture成否
			if ( bPackOnPrepared )
			{
				FMatrix4x4 PackEarlierWorld = CenteredWorld; // 先行native描画を左へ置く行列
				PackEarlierWorld.m[3][0] = -4.0f;
				FMatrix4x4 PackLaterWorld = CenteredWorld; // 後続effect描画を右へ置く行列
				PackLaterWorld.m[3][0] = 2.0f;
				FModelMaterialTriangleProjection PackEarlierTriangle; // 先行native三角形の実ROI
				FModelMaterialTriangleProjection PackLaterTriangle; // 後続effect三角形の実ROI
				const bool bPackEarlierProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, PackEarlierWorld, PackEarlierTriangle ); // 先行位置のROI投影結果
				const bool bPackLaterProjected = Surface.IsValid() && ProjectModelMaterialTriangle( Surface->GetData(), MaterialIndex, SelectedMeshIndex, PackLaterWorld, PackLaterTriangle ); // 後続位置のROI投影結果
				TArray<FColor32> PackEarlierNativePixels; // 先行位置単独のnative positive control
				TArray<FColor32> PackLaterNativePixels; // 後続位置単独のnative positive control
				const bool bPackEarlierNativeCaptured = DrawPixels( PackOnPlainAnimator, PackEarlierWorld, PackEarlierNativePixels, "pack_on_earlier_native" ); // 先行native画素のcapture成否
				const bool bPackLaterNativeCaptured = DrawPixels( PackOnPlainAnimator, PackLaterWorld, PackLaterNativePixels, "pack_on_later_native" ); // 後続native画素のcapture成否
				PackOnAnimator.GetMaterialEffect().Restart();
				FDrawBuffer PackSequence; // pack ON instanceへの連続native/effect command
				FDrawContext PackSequenceDraw( PackSequence );
				PackOnPlainAnimator.Record( PackSequenceDraw, PackEarlierWorld );
				PackOnAnimator.Record( PackSequenceDraw, PackLaterWorld );
				SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
				const bool bPackSequenceHasBothCommands = CountModelMaterialCommands( PackSequence, EDrawCommand::Model3D ) == 1 && CountModelMaterialCommands( PackSequence, EDrawCommand::ModelMaterial3D ) == 1; // 両方のcommandが一件ずつあるか
				if ( !PackSequence.Words.empty() ) Executor.Execute( FDrawSpan{ PackSequence.Words.data(), static_cast<int32>( PackSequence.Words.size() ) } );
				RenderVertex();
				TArray<FColor32> PackSequencePixels; // 連続描画後の合成画素
				const bool bPackSequenceCaptured = CaptureModelMaterialPixels( Screen, Soft, PackSequencePixels ); // 連続描画後のcapture成否
				if ( bPackSequenceCaptured ) SaveModelMaterialDiagnosticPng( "pack_on_same_instance_sequence" );
				const int32 PackEarlierExpectedPixels = bPackEarlierProjected && bPackEarlierNativeCaptured ? CountModelMaterialTrianglePixels( PackEarlierNativePixels, PackEarlierTriangle ) : -1; // 先行native領域のpositive画素数
				const int32 PackEarlierSequencePixels = bPackEarlierProjected && bPackSequenceCaptured ? CountModelMaterialTrianglePixels( PackSequencePixels, PackEarlierTriangle ) : -1; // 連続描画後に先行位置へ残った画素数
				const int32 PackLaterExpectedPixels = bPackLaterProjected && bPackLaterNativeCaptured ? CountModelMaterialTrianglePixels( PackLaterNativePixels, PackLaterTriangle ) : -1; // 後続native領域のpositive画素数
				const int32 PackLaterSequencePixels = bPackLaterProjected && bPackSequenceCaptured ? CountModelMaterialTrianglePixels( PackSequencePixels, PackLaterTriangle ) : -1; // 連続描画後に後続位置へ残った画素数
				LOG( LogAssetSelfTest, Log, "材質pack連続試験: nativeCommand=%d effectCommand=%d capture=%d 先行ROI=%d/%d 後続ROI=%d/%d", CountModelMaterialCommands( PackSequence, EDrawCommand::Model3D ), CountModelMaterialCommands( PackSequence, EDrawCommand::ModelMaterial3D ), bPackSequenceCaptured ? 1 : 0, PackEarlierExpectedPixels, PackEarlierSequencePixels, PackLaterExpectedPixels, PackLaterSequencePixels );
				Checker.Check( bPackEarlierProjected && bPackLaterProjected && bPackEarlierNativeCaptured && bPackLaterNativeCaptured && bPackSequenceHasBothCommands && bPackSequenceCaptured && PackEarlierExpectedPixels > 0 && PackEarlierSequencePixels == PackEarlierExpectedPixels && PackLaterExpectedPixels > 0 && PackLaterSequencePixels == 0, "pack ON同一instanceの先行native像を保ち、後続Opacity 0像だけを消す" );
			}
			PackOnAnimator.GetMaterialEffect().Stop();
			TArray<FColor32> PackOnStoppedPixels;
			const bool bPackOnStoppedCaptured = DrawPixels( PackOnAnimator, CenteredWorld, PackOnStoppedPixels, "pack_on_stopped" );
			LOG( LogAssetSelfTest, Log, "model-material pack captures fixture=%d setting=%d native=%d prepare=%d effect=%d diff=%d stop=%d stopDiff=%d selectedMesh=%d selectedVisibleAfter=%d", bPackOnFixtureWritten ? 1 : 0, bPackOnSettingAccepted ? 1 : 0, bPackOnNativeCaptured ? 1 : 0, bPackOnPrepared ? 1 : 0, bPackOnEffectCaptured ? 1 : 0, CountModelMaterialPixelDifferences( PackOnNativePixels, PackOnEffectPixels ), bPackOnStoppedCaptured ? 1 : 0, CountModelMaterialPixelDifferences( PackOnNativePixels, PackOnStoppedPixels ), SelectedMeshIndex, PackOnInstance.IsValid() ? MV1GetMeshVisible( PackOnInstance->GetHandle(), SelectedMeshIndex ) : -1 );
			Checker.Check( bPackOnFixtureWritten && bPackOnSettingAccepted && bPackOnNativeCaptured && bPackOnPrepared && bPackOnEffectCaptured && CountModelMaterialPixelDifferences( PackOnNativePixels, PackOnEffectPixels ) > 0 && bPackOnStoppedCaptured && CountModelMaterialPixelDifferences( PackOnNativePixels, PackOnStoppedPixels ) == 0, "pack ON modelもselected effectで画素が変わりStop後native画素へ復帰する" );
			PackOnAnimator.ClearMaterialEffect();
			FirstAnimator.ClearMaterialEffect();
		}
		MV1SetLoadModelUsePackDraw( FALSE );
		Surface.Reset();
		const FBackendMemoryInfo BeforeCleanup = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
		PackOnInstance.Reset(); PackOnModel.Reset();
		First.Reset(); Sibling.Reset(); Model.Reset(); Assets.ReleaseUnused(); Assets.Tick( 0.0f );
		SavedState.Restore();
		const int32 ScreenDeleteResult = Screen >= 0 ? DeleteGraph( Screen ) : 0;
		const int32 SoftDeleteResult = Soft >= 0 ? DeleteSoftImage( Soft ) : 0;
		Checker.Check( !ModelWeak.IsValid() && !PackOnWeak.IsValid() && ScreenDeleteResult == 0 && SoftDeleteResult == 0, "real model instanceとcapture handleを後片付けする" );
		const FBackendMemoryInfo AfterCleanup = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
		Checker.Check( BeforeCleanup.NumModelHandles >= AfterCleanup.NumModelHandles && BeforeCleanup.NumTextureHandles >= AfterCleanup.NumTextureHandles, "effect資源解放後にnative handle数が増え続けない" );
	}
}
