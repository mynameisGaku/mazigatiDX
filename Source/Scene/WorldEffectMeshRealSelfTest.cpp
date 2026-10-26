// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectMeshRealSelfTest.h"

#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <d3d11.h>
#include <limits>
#include <utility>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/MeshBuffer.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 実機試験中に置き換えるDxLib画面と変換状態を保存する。
	struct FMeshTestCallerState
	{
		// 呼び出し前の画面と自動設定切替。
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 呼び出し前のcamera範囲と中心。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		// cameraとDxLib変換行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		// 描画範囲と色・合成状態。
		RECT Area{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// 変更前の状態を取得し、screen切替による暗黙変更を止める。
		FMeshTestCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] );
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &Area );
			GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 作った画面を解放する前に呼び出し元画面と状態を戻す。
		~FMeshTestCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen );
			SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// UVの左右半分を異なる色にし、source座標を画素で判定できる画像を作る。
	int32 MakeQuadrantTexture()
	{
		// 既知色を塗る8x8一時画像。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 240 : 20, 20, X < 4 ? 20 : 240, 255 );
		// 塗り終えた画像から作るDxLib graph。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// 左側を消し、右側を残すmask画像を作る。
	int32 MakeHalfMask()
	{
		// mask値を塗る8x8一時画像。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 0 : 255, 0, 0, 255 );
		// 塗り終えたmaskから作るDxLib graph。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// 左右で符号の異なるnoiseを作り、派生画像のcropを確認する。
	int32 MakeSplitNoise()
	{
		// 横半分ごとに逆向きの変位を持つ一時画像。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 255 : 0, 128, 0, 255 );
		// crop元となるDxLib graph。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// maskを0から255へ連続変化させ、境界発光の画素を選べる画像を作る。
	int32 MakeRampMask()
	{
		// 左右に単調増加するmask値を塗る一時画像。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, X * 36, 0, 0, 255 );
		// ramp maskから作るDxLib graph。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// 深度比較で前後が分かる不透明一色の小画像を作る。
	int32 MakeSolidTexture( uint8 R, uint8 G, uint8 B )
	{
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, R, G, B, 255 );
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// plane四隅と2三角形を持つworld座標メッシュを作る。
	void MakePlane( FMeshVertex* Vertices, uint32* Indices )
	{
		const float32 Positions[4][3] = { { -2.0f, 2.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, { 2.0f, -2.0f, 0.0f }, { -2.0f, -2.0f, 0.0f } };
		const float32 UVs[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			// 出力頂点へworld位置と画像座標を設定する。
			std::memcpy( Vertices[Index].Position, Positions[Index], sizeof( Positions[Index] ) );
			Vertices[Index].U = UVs[Index][0]; Vertices[Index].V = UVs[Index][1];
			Vertices[Index].Color[0] = 255; Vertices[Index].Color[1] = 255; Vertices[Index].Color[2] = 255; Vertices[Index].Color[3] = 255;
		}
		Indices[0] = 0; Indices[1] = 1; Indices[2] = 2; Indices[3] = 0; Indices[4] = 2; Indices[5] = 3;
	}

	// 内側を空けた四角いringを4つの帯三角形で作る。
	void MakeRing( FMeshVertex* Vertices, uint32* Indices )
	{
		const float32 Positions[8][3] = { { -2.0f, 2.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, { 2.0f, -2.0f, 0.0f }, { -2.0f, -2.0f, 0.0f }, { -0.75f, 0.75f, 0.0f }, { 0.75f, 0.75f, 0.0f }, { 0.75f, -0.75f, 0.0f }, { -0.75f, -0.75f, 0.0f } };
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			// ring頂点へworld位置・白色・正規化UVを設定する。
			std::memcpy( Vertices[Index].Position, Positions[Index], sizeof( Positions[Index] ) );
			Vertices[Index].U = ( Positions[Index][0] + 2.0f ) * 0.25f; Vertices[Index].V = ( 2.0f - Positions[Index][1] ) * 0.25f;
			Vertices[Index].Color[0] = 255; Vertices[Index].Color[1] = 255; Vertices[Index].Color[2] = 255; Vertices[Index].Color[3] = 255;
		}
		for ( uint32 Side = 0; Side < 4; ++Side )
		{
			// 外側と内側の対応辺を結ぶ三角形index。
			const uint32 Next = ( Side + 1 ) % 4;
			const uint32 Base = Side * 6;
			Indices[Base] = Side; Indices[Base + 1] = Next; Indices[Base + 2] = 4 + Next; Indices[Base + 3] = Side; Indices[Base + 4] = 4 + Next; Indices[Base + 5] = 4 + Side;
		}
	}

	// 材質描画が変更するnative D3D11 state全体を保存し、照合と復元に使う。
	struct FMeshNativeSnapshot
	{
		// 入力形式、vertex/index buffer、stride、offset、topology。
		ID3D11InputLayout* Layout = nullptr;
		ID3D11Buffer* VertexBuffer = nullptr;
		ID3D11Buffer* IndexBuffer = nullptr;
		UINT VertexStride = 0;
		UINT VertexOffset = 0;
		DXGI_FORMAT IndexFormat = DXGI_FORMAT_UNKNOWN;
		UINT IndexOffset = 0;
		D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		// VS/PSの定数buffer 0、PSのsource/mask/noiseとsampler。
		ID3D11Buffer* VertexConstants = nullptr;
		ID3D11Buffer* PixelConstants = nullptr;
		ID3D11ShaderResourceView* Resources[3] = {};
		ID3D11SamplerState* Sampler = nullptr;
		// blend state、4係数、sample mask。
		ID3D11BlendState* Blend = nullptr;
		FLOAT BlendFactor[4] = {};
		UINT SampleMask = 0;
		// depth/stencil stateと参照値、raster state。
		ID3D11DepthStencilState* Depth = nullptr;
		UINT StencilRef = 0;
		ID3D11RasterizerState* Raster = nullptr;
		// 各shader段階の本体とclass instance配列。
		ID3D11VertexShader* VertexShader = nullptr;
		ID3D11PixelShader* PixelShader = nullptr;
		ID3D11GeometryShader* GeometryShader = nullptr;
		ID3D11HullShader* HullShader = nullptr;
		ID3D11DomainShader* DomainShader = nullptr;
		ID3D11ClassInstance* VertexClasses[256] = {};
		ID3D11ClassInstance* PixelClasses[256] = {};
		ID3D11ClassInstance* GeometryClasses[256] = {};
		ID3D11ClassInstance* HullClasses[256] = {};
		ID3D11ClassInstance* DomainClasses[256] = {};
		UINT VertexClassCount = 256;
		UINT PixelClassCount = 256;
		UINT GeometryClassCount = 256;
		UINT HullClassCount = 256;
		UINT DomainClassCount = 256;

		// native contextの状態をgetterのCOM参照付きで取得する。
		void Save( ID3D11DeviceContext* Context )
		{
			Context->IAGetInputLayout( &Layout ); Context->IAGetVertexBuffers( 0, 1, &VertexBuffer, &VertexStride, &VertexOffset ); Context->IAGetIndexBuffer( &IndexBuffer, &IndexFormat, &IndexOffset ); Context->IAGetPrimitiveTopology( &Topology );
			Context->VSGetConstantBuffers( 0, 1, &VertexConstants ); Context->PSGetConstantBuffers( 0, 1, &PixelConstants ); Context->PSGetShaderResources( 0, 3, Resources ); Context->PSGetSamplers( 0, 1, &Sampler );
			Context->OMGetBlendState( &Blend, BlendFactor, &SampleMask ); Context->OMGetDepthStencilState( &Depth, &StencilRef ); Context->RSGetState( &Raster );
			Context->VSGetShader( &VertexShader, VertexClasses, &VertexClassCount ); Context->PSGetShader( &PixelShader, PixelClasses, &PixelClassCount ); Context->GSGetShader( &GeometryShader, GeometryClasses, &GeometryClassCount ); Context->HSGetShader( &HullShader, HullClasses, &HullClassCount ); Context->DSGetShader( &DomainShader, DomainClasses, &DomainClassCount );
		}

		// 退避した各stateをcontextへ戻す。
		void Restore( ID3D11DeviceContext* Context ) const
		{
			Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, const_cast<ID3D11Buffer**>( &VertexBuffer ), &VertexStride, &VertexOffset ); Context->IASetIndexBuffer( IndexBuffer, IndexFormat, IndexOffset ); Context->IASetPrimitiveTopology( Topology );
			Context->VSSetConstantBuffers( 0, 1, const_cast<ID3D11Buffer**>( &VertexConstants ) ); Context->PSSetConstantBuffers( 0, 1, const_cast<ID3D11Buffer**>( &PixelConstants ) ); Context->PSSetShaderResources( 0, 3, const_cast<ID3D11ShaderResourceView**>( Resources ) ); Context->PSSetSamplers( 0, 1, const_cast<ID3D11SamplerState**>( &Sampler ) );
			Context->OMSetBlendState( Blend, BlendFactor, SampleMask ); Context->OMSetDepthStencilState( Depth, StencilRef ); Context->RSSetState( Raster );
			Context->VSSetShader( VertexShader, VertexClasses, VertexClassCount ); Context->PSSetShader( PixelShader, PixelClasses, PixelClassCount ); Context->GSSetShader( GeometryShader, GeometryClasses, GeometryClassCount ); Context->HSSetShader( HullShader, HullClasses, HullClassCount ); Context->DSSetShader( DomainShader, DomainClasses, DomainClassCount );
		}

		// 現在のcontextが保存値と参照・数値の両方で一致するか確認する。
		bool Matches( ID3D11DeviceContext* Context ) const
		{
			FMeshNativeSnapshot Current;
			Current.Save( Context );
			bool bMatches = Layout == Current.Layout && VertexBuffer == Current.VertexBuffer && IndexBuffer == Current.IndexBuffer && VertexStride == Current.VertexStride && VertexOffset == Current.VertexOffset && IndexFormat == Current.IndexFormat && IndexOffset == Current.IndexOffset && Topology == Current.Topology && VertexConstants == Current.VertexConstants && PixelConstants == Current.PixelConstants && Sampler == Current.Sampler && Blend == Current.Blend && SampleMask == Current.SampleMask && Depth == Current.Depth && StencilRef == Current.StencilRef && Raster == Current.Raster && VertexShader == Current.VertexShader && PixelShader == Current.PixelShader && GeometryShader == Current.GeometryShader && HullShader == Current.HullShader && DomainShader == Current.DomainShader && VertexClassCount == Current.VertexClassCount && PixelClassCount == Current.PixelClassCount && GeometryClassCount == Current.GeometryClassCount && HullClassCount == Current.HullClassCount && DomainClassCount == Current.DomainClassCount;
			for ( int32 Index = 0; Index < 3; ++Index ) bMatches = bMatches && Resources[Index] == Current.Resources[Index];
			for ( int32 Index = 0; Index < 4; ++Index ) bMatches = bMatches && BlendFactor[Index] == Current.BlendFactor[Index];
			for ( UINT Index = 0; Index < VertexClassCount; ++Index ) bMatches = bMatches && VertexClasses[Index] == Current.VertexClasses[Index];
			for ( UINT Index = 0; Index < PixelClassCount; ++Index ) bMatches = bMatches && PixelClasses[Index] == Current.PixelClasses[Index];
			for ( UINT Index = 0; Index < GeometryClassCount; ++Index ) bMatches = bMatches && GeometryClasses[Index] == Current.GeometryClasses[Index];
			for ( UINT Index = 0; Index < HullClassCount; ++Index ) bMatches = bMatches && HullClasses[Index] == Current.HullClasses[Index];
			for ( UINT Index = 0; Index < DomainClassCount; ++Index ) bMatches = bMatches && DomainClasses[Index] == Current.DomainClasses[Index];
			return bMatches;
		}

		// getterが増やしたCOM参照を解放する。
		~FMeshNativeSnapshot()
		{
			if ( Layout != nullptr ) Layout->Release(); if ( VertexBuffer != nullptr ) VertexBuffer->Release(); if ( IndexBuffer != nullptr ) IndexBuffer->Release(); if ( VertexConstants != nullptr ) VertexConstants->Release(); if ( PixelConstants != nullptr ) PixelConstants->Release();
			for ( ID3D11ShaderResourceView* Resource : Resources ) if ( Resource != nullptr ) Resource->Release();
			if ( Sampler != nullptr ) Sampler->Release(); if ( Blend != nullptr ) Blend->Release(); if ( Depth != nullptr ) Depth->Release(); if ( Raster != nullptr ) Raster->Release();
			if ( VertexShader != nullptr ) VertexShader->Release(); if ( PixelShader != nullptr ) PixelShader->Release(); if ( GeometryShader != nullptr ) GeometryShader->Release(); if ( HullShader != nullptr ) HullShader->Release(); if ( DomainShader != nullptr ) DomainShader->Release();
			for ( UINT Index = 0; Index < VertexClassCount; ++Index ) if ( VertexClasses[Index] != nullptr ) VertexClasses[Index]->Release();
			for ( UINT Index = 0; Index < PixelClassCount; ++Index ) if ( PixelClasses[Index] != nullptr ) PixelClasses[Index]->Release();
			for ( UINT Index = 0; Index < GeometryClassCount; ++Index ) if ( GeometryClasses[Index] != nullptr ) GeometryClasses[Index]->Release();
			for ( UINT Index = 0; Index < HullClassCount; ++Index ) if ( HullClasses[Index] != nullptr ) HullClasses[Index]->Release();
			for ( UINT Index = 0; Index < DomainClassCount; ++Index ) if ( DomainClasses[Index] != nullptr ) DomainClasses[Index]->Release();
		}
	};
}

// 実際のDxLib shader経由でplaneの色、mask、発光、状態復元を読む。
void NWorldTest::RunEffectMaterialMeshRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor )
{
	C.Group = "effect-material-mesh-d3d11";
	C.Check( Executor.SupportsEffectMaterialMesh(), "通常のDxLib Direct3D 11環境で材質メッシュ能力を公開する" );
	if ( !Executor.SupportsEffectMaterialMesh() ) return;
	// 呼び出し元の描画状態を復元する所有snapshot。
	FMeshTestCallerState Saved;
	// mesh実画素を描くscreenとreadback用画像。
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	// source左右の既知色とmaskのgraph handle。
	int32 Source = MakeQuadrantTexture();
	int32 Mask = MakeHalfMask();
	// threshold境界の発光量を読み分けるgradient mask。
	int32 RampMask = MakeRampMask();
	// 左右で向きが異なるnoiseと、右半分だけの派生graph。
	int32 SplitNoise = MakeSplitNoise();
	int32 DerivedNoise = SplitNoise >= 0 ? DerivationGraph( 4, 0, 4, 8, SplitNoise ) : -1;
	// 発光倍率をLDR上限で比較できる中間輝度画像。
	const int32 DimSource = MakeARGB8ColorSoftImage( 8, 8 );
	int32 DimGraph = -1;
	if ( DimSource >= 0 )
	{
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( DimSource, X, Y, 64, 64, 64, 255 );
		DimGraph = CreateGraphFromSoftImage( DimSource );
		DeleteSoftImage( DimSource );
	}
	// +X方向へ一定変位させるnoise画像。
	const int32 NoiseSoft = MakeARGB8ColorSoftImage( 8, 8 );
	int32 NoiseGraph = -1;
	if ( NoiseSoft >= 0 )
	{
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( NoiseSoft, X, Y, 255, 128, 0, 255 );
		NoiseGraph = CreateGraphFromSoftImage( NoiseSoft );
		DeleteSoftImage( NoiseSoft );
	}
	// 深度順序を独立した色で判定するnear/far画像。
	const int32 RedGraph = MakeSolidTexture( 240, 20, 20 );
	const int32 BlueGraph = MakeSolidTexture( 20, 20, 240 );
	// 頂点rgba単独の影響を調べる白source。
	const int32 WhiteGraph = MakeSolidTexture( 255, 255, 255 );
	// 既知領域へ切り出したsourceとmaskの派生handle。
	int32 DerivedSource = Source >= 0 ? DerivationGraph( 4, 0, 4, 8, Source ) : -1;
	int32 DerivedMask = Mask >= 0 ? DerivationGraph( 4, 0, 4, 8, Mask ) : -1;
	C.Check( Screen >= 0 && Soft >= 0 && Source >= 0 && Mask >= 0 && RampMask >= 0 && SplitNoise >= 0 && DerivedNoise >= 0 && DimGraph >= 0 && NoiseGraph >= 0 && RedGraph >= 0 && BlueGraph >= 0 && WhiteGraph >= 0 && DerivedSource >= 0 && DerivedMask >= 0, "材質メッシュ用screen・readback・source・mask・noiseを用意する" );
	if ( Screen < 0 || Soft < 0 || Source < 0 || Mask < 0 || RampMask < 0 || SplitNoise < 0 || DerivedNoise < 0 || DimGraph < 0 || NoiseGraph < 0 || RedGraph < 0 || BlueGraph < 0 || WhiteGraph < 0 || DerivedSource < 0 || DerivedMask < 0 )
	{
		if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); if ( Source >= 0 ) DeleteGraph( Source ); if ( Mask >= 0 ) DeleteGraph( Mask ); if ( RampMask >= 0 ) DeleteGraph( RampMask ); if ( SplitNoise >= 0 ) DeleteGraph( SplitNoise ); if ( DerivedNoise >= 0 ) DeleteGraph( DerivedNoise ); if ( DimGraph >= 0 ) DeleteGraph( DimGraph ); if ( NoiseGraph >= 0 ) DeleteGraph( NoiseGraph ); if ( RedGraph >= 0 ) DeleteGraph( RedGraph ); if ( BlueGraph >= 0 ) DeleteGraph( BlueGraph ); if ( WhiteGraph >= 0 ) DeleteGraph( WhiteGraph ); if ( DerivedSource >= 0 ) DeleteGraph( DerivedSource ); if ( DerivedMask >= 0 ) DeleteGraph( DerivedMask );
		return;
	}
	SetUseGraphZBuffer( Screen, TRUE ); SetDrawScreen( Screen ); SetDrawAreaFull(); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
	MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport );
	const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity ); SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 12, 12, 12, 255 );
	// 描画する二三角形planeと順序付きindex。
	FMeshVertex Vertices[4]{};
	uint32 Indices[6]{};
	MakePlane( Vertices, Indices );
	// source色を比較する基本材質mesh。
	FDrawMaterialMeshData Data;
	Data.Texture = Source; Data.NumVertices = 4; Data.NumIndices = 6; Data.Material.DepthTest = 1; Data.Material.DepthWrite = 0; Data.bTwoSided = 1;
	// 描画直前のrasterを確認するDirect3D context。
	ID3D11DeviceContext* Context = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
	// 二面試験でflush後に設定するcaller raster。
	ID3D11RasterizerState* RasterAfterFlush = nullptr;
	// screenをclearし、一命令を実行して全画面をreadbackする。
	// 今回実行したspanに含まれる材質メッシュ命令の個数。
	int32 LastMaterialMeshCommands = 0;
	const auto Render = [&]( const FDrawMaterialMeshData& Input, const FMeshVertex* InputVertices, const uint32* InputIndices, ID3D11RasterizerState* RequestedRaster = nullptr, const RECT* RequestedArea = nullptr, EDrawBlend BlendMode = EDrawBlend::Alpha )
	{
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		if ( RequestedArea != nullptr ) SetDrawArea( RequestedArea->left, RequestedArea->top, RequestedArea->right, RequestedArea->bottom );
		FDrawBuffer Buffer; FDrawContext Draw( Buffer );
		if ( RequestedRaster != nullptr ) SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 ); else Draw.Blend( BlendMode, 255 );
		Draw.MaterialMesh3D( Input, InputVertices, InputIndices );
		LastMaterialMeshCommands = 0;
		if ( !Buffer.Words.empty() )
		{
			// Blend命令ではなくmesh命令だけを数えて記録成功を判定する。
			ForEachDrawCommand( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) ++LastMaterialMeshCommands; } );
			if ( RequestedRaster != nullptr && Context != nullptr )
			{
				// DxLibのclearと保留頂点をflushした後でcallerの裏面cullingを設定する。
				RenderVertex(); Context->RSSetState( RequestedRaster );
				ID3D11RasterizerState* BoundRaster = nullptr;
				Context->RSGetState( &BoundRaster );
				D3D11_RASTERIZER_DESC BoundDesc{};
				if ( BoundRaster != nullptr ) BoundRaster->GetDesc( &BoundDesc );
				C.Check( BoundRaster != nullptr && BoundDesc.CullMode == D3D11_CULL_BACK, "材質mesh直前にcallerの裏面cullingがbindされる" );
				if ( BoundRaster != nullptr ) BoundRaster->Release();
			}
			Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			if ( RequestedRaster != nullptr && Context != nullptr )
			{
				// 材質shader終了後もnative rasterがcaller値へ戻ったことを確かめる。
				ID3D11RasterizerState* RestoredRaster = nullptr;
				Context->RSGetState( &RestoredRaster );
				C.Check( RestoredRaster == RequestedRaster, "材質mesh後にcaller raster stateを復元する" );
				if ( RestoredRaster != nullptr ) RestoredRaster->Release();
			}
		}
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
	};
	// 一画素のRGBAをreadback画像から取り出す。
	const auto Pixel = [&]( int32 X, int32 Y )
	{
		int32 Channels[4] = {};
		GetPixelSoftImage( Soft, X, Y, &Channels[0], &Channels[1], &Channels[2], &Channels[3] );
		return FColor32( static_cast<uint8>( Channels[0] ), static_cast<uint8>( Channels[1] ), static_cast<uint8>( Channels[2] ), static_cast<uint8>( Channels[3] ) );
	};
	Render( Data, Vertices, Indices );
	C.Check( LastMaterialMeshCommands == 1, "正常な固定planeが材質メッシュ命令として一件記録される" );
	const FColor32 Left = Pixel( 48, 64 );
	const FColor32 Right = Pixel( 80, 64 );
	C.Check( Left.r > 150 && Left.b < 60 && Right.b > 150 && Right.r < 60, "plane頂点UVが左右source色へ対応し、index三角形が非空描画される" );
	// 通常材質planeの全RGBAを後続条件と比較する基準配列。
	TArray<FColor32> BaselinePixels;
	BaselinePixels.SetNum( 128 * 128 );
	for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) BaselinePixels[Y * 128 + X] = Pixel( X, Y );
	// caller world変換を移しても既にworld座標のmesh画素は同じ位置に残る。
	MATRIX ShiftedWorld = Identity;
	ShiftedWorld.m[3][0] = 2.0f;
	SetTransformToWorld( &ShiftedWorld );
	Render( Data, Vertices, Indices );
	bool bWorldPixelsMatch = true;
	for ( int32 Y = 0; Y < 128 && bWorldPixelsMatch; ++Y ) for ( int32 X = 0; X < 128; ++X )
	{
		// caller worldを変えた後に同位置から読む画素。
		const FColor32 Actual = Pixel( X, Y );
		const FColor32& Expected = BaselinePixels[Y * 128 + X];
		if ( Actual.r != Expected.r || Actual.g != Expected.g || Actual.b != Expected.b || Actual.a != Expected.a ) { bWorldPixelsMatch = false; break; }
	}
	C.Check( bWorldPixelsMatch, "world頂点へcaller world matrixを二重適用せず全pixel位置を保つ" );
	SetTransformToWorld( &Identity );
	// native表示範囲を狭めてもDrawAreaは既知world画素をclipする。
	const RECT NarrowDrawArea = { 32, 32, 96, 96 };
	Render( Data, Vertices, Indices, nullptr, &NarrowDrawArea );
	const FColor32 ClippedLeft = Pixel( 48, 64 );
	const FColor32 ClippedRight = Pixel( 80, 64 );
	const FColor32 ClippedOutside = Pixel( 24, 64 );
	C.Check( ClippedLeft.r > 150 && ClippedRight.b > 150 && ClippedOutside.r < 20 && ClippedOutside.g < 20 && ClippedOutside.b < 20, "狭いDrawAreaはmesh位置を保って外側だけをclipする" );
	// 論理viewportを64x64へ縮め、中心32の既知色と範囲外pixelを読む。
	MATRIX SmallLogicalViewport{};
	CreateViewportMatrix( &SmallLogicalViewport, 32.0f, 32.0f, 64.0f, 64.0f );
	SetTransformToViewport( &SmallLogicalViewport );
	Render( Data, Vertices, Indices );
	// 縮小viewport内の左側source色を読むpixel。
	const FColor32 ViewportLeft = Pixel( 24, 32 );
	// 縮小viewport内の右側source色を読むpixel。
	const FColor32 ViewportRight = Pixel( 40, 32 );
	// 縮小viewportの外にある背景pixel。
	const FColor32 ViewportOutside = Pixel( 80, 32 );
	C.Check( ViewportLeft.r > 150 && ViewportRight.b > 150 && ViewportOutside.r < 20 && ViewportOutside.g < 20 && ViewportOutside.b < 20, "論理viewport変換後もworld位置と既知source色を保つ (left=%d,%d,%d right=%d,%d,%d outside=%d,%d,%d)", ViewportLeft.r, ViewportLeft.g, ViewportLeft.b, ViewportRight.r, ViewportRight.g, ViewportRight.b, ViewportOutside.r, ViewportOutside.g, ViewportOutside.b );
	SetTransformToViewport( &Viewport );
	Data.UVOffset[0] = 0.5f;
	Render( Data, Vertices, Indices );
	const FColor32 ScrolledLeft = Pixel( 48, 64 );
	const FColor32 ScrolledRight = Pixel( 80, 64 );
	C.Check( ScrolledLeft.b > 150 && ScrolledLeft.r < 60 && ScrolledRight.r > 150 && ScrolledRight.b < 60, "負でないsource UV移動が三角形全体の色位置を一周期ずらす" );
	Data.UVOffset[0] = -0.5f;
	Render( Data, Vertices, Indices );
	const FColor32 NegativeScrollLeft = Pixel( 48, 64 );
	const FColor32 NegativeScrollRight = Pixel( 80, 64 );
	C.Check( NegativeScrollLeft.b > 150 && NegativeScrollLeft.r < 60 && NegativeScrollRight.r > 150 && NegativeScrollRight.b < 60, "負のsource UV移動はfraction wrap後に正の半周期と同じ既知色を返す" );
	Data.UVOffset[0] = 0.0f; Data.DistortionTexture = NoiseGraph; Data.DistortionStrength = 0.35f;
	Render( Data, Vertices, Indices );
	const FColor32 DistortedLeft = Pixel( 48, 64 );
	C.Check( DistortedLeft.b > 150 && DistortedLeft.r < 60 && LastMaterialMeshCommands == 1, "正のnoise変位がsourceの既知赤画素を右側の青へ移す" );
	Data.DistortionStrength = 0.0f; Data.DistortionTexture = -1;
	Data.DistortionStrength = 0.0f; Data.DistortionTexture = -1;
	Render( Data, Vertices, Indices );
	bool bZeroDistortionMatches = true;
	for ( int32 Y = 0; Y < 128 && bZeroDistortionMatches; ++Y ) for ( int32 X = 0; X < 128; ++X )
	{
		// 基準と無効なゆがみ経路の現在pixel。
		const FColor32 Current = Pixel( X, Y );
		const FColor32& Expected = BaselinePixels[Y * 128 + X];
		if ( Current.r != Expected.r || Current.g != Expected.g || Current.b != Expected.b || Current.a != Expected.a ) { bZeroDistortionMatches = false; break; }
	}
	C.Check( bZeroDistortionMatches, "ゆがみ強度0はnoise画像なしで通常材質planeの全RGBAを保つ" );
	// source U≈0.445では+0.35が青領域、-0.35が赤領域になるのでnoise crop符号差を読む。
	Data.Texture = Source; Data.Material.UVScale[0] = 1.0f; Data.Material.UVScale[1] = 1.0f; Data.Material.MaskTexture = -1; Data.Material.bDissolve = 0; Data.Material.EdgeWidth = 0.0f; Data.Dissolve = 0.0f; Data.Emission = 1.0f; Data.UVOffset[0] = 0.0f; Data.UVOffset[1] = 0.0f; Data.DistortionTexture = DerivedNoise; Data.DistortionStrength = 0.35f; Data.DistortionUVScale[0] = 1.0f; Data.DistortionUVScale[1] = 1.0f; Data.DistortionUVOffset[0] = 0.0f; Data.DistortionUVOffset[1] = 0.0f;
	Render( Data, Vertices, Indices );
	// UV≈0.445の点で派生noiseの-X結果を読むpixel。
	const FColor32 CroppedNoisePixel = Pixel( 60, 64 );
	Data.DistortionTexture = SplitNoise;
	Render( Data, Vertices, Indices );
	// 同じUVで元noise左側の+X結果を読むpixel。
	const FColor32 FullNoisePixel = Pixel( 60, 64 );
	C.Check( CroppedNoisePixel.r > 150 && CroppedNoisePixel.b < 60 && FullNoisePixel.b > 150 && FullNoisePixel.r < 60, "noise派生cropは右半分の-Xを読み、元画像左半分+Xと異なるsource画素を選ぶ (crop=%d,%d,%d full=%d,%d,%d)", CroppedNoisePixel.r, CroppedNoisePixel.g, CroppedNoisePixel.b, FullNoisePixel.r, FullNoisePixel.g, FullNoisePixel.b );
	Data.DistortionStrength = 0.0f; Data.DistortionTexture = -1;
	Data.Material.MaskTexture = Mask; Data.Material.bDissolve = 1; Data.Dissolve = 0.5f; Data.Material.EdgeWidth = 0.2f; Data.Material.EdgeEmission = 2.0f;
	Render( Data, Vertices, Indices );
	const FColor32 Hole = Pixel( 48, 64 );
	const FColor32 Alive = Pixel( 80, 64 );
	C.Check( Hole.r < 20 && Hole.g < 20 && Hole.b < 20 && Alive.b > 150, "mask消失はplane内部へ実際の穴を開け、残存側は元画像色を保つ" );
	Data.Dissolve = 1.0f;
	Render( Data, Vertices, Indices );
	const FColor32 FullyDissolved = Pixel( 64, 64 );
	C.Check( FullyDissolved.r < 20 && FullyDissolved.g < 20 && FullyDissolved.b < 20, "消失値1では材質mesh全体を描かない" );
	Data.Texture = DerivedSource; Data.Material.MaskTexture = -1; Data.Material.bDissolve = 0; Data.Dissolve = 0.0f;
	Render( Data, Vertices, Indices );
	const FColor32 CroppedSourceLeft = Pixel( 48, 64 );
	const FColor32 CroppedSourceRight = Pixel( 80, 64 );
	C.Check( CroppedSourceLeft.b > 150 && CroppedSourceRight.b > 150 && CroppedSourceLeft.r < 60 && CroppedSourceRight.r < 60, "source派生画像はbase textureの右領域だけをmeshへ写す" );
	Data.Texture = Source; Data.Material.MaskTexture = DerivedMask; Data.Material.bDissolve = 1; Data.Dissolve = 0.5f;
	Render( Data, Vertices, Indices );
	const FColor32 CroppedMaskLeft = Pixel( 48, 64 );
	C.Check( CroppedMaskLeft.r > 150 && CroppedMaskLeft.b < 60, "mask派生画像はbase mask右領域を読み、mesh左側の穴を消す" );
	Data.Material.MaskTexture = -1; Data.Material.bDissolve = 0; Data.Dissolve = 0.0f; Data.Emission = 2.0f;
	Data.Texture = DimGraph;
	Render( Data, Vertices, Indices );
	const FColor32 EmissionTwo = Pixel( 48, 64 );
	Data.Emission = 1.0f; Render( Data, Vertices, Indices );
	const FColor32 EmissionOne = Pixel( 48, 64 );
	C.Check( EmissionOne.r >= 58 && EmissionOne.r <= 70 && EmissionTwo.r >= 120 && EmissionTwo.r <= 136 && EmissionTwo.r > EmissionOne.r + 45, "発光1と2はLDR clamp前の既知64画素を64と128付近へ分ける" );
	// ramp maskのthreshold境界で赤edgeを加算する材質値。
	Data.Texture = DimGraph; Data.Material.MaskTexture = RampMask; Data.Material.bDissolve = 1; Data.Dissolve = 0.5f; Data.Emission = 1.0f; Data.Material.EdgeWidth = 0.2f; Data.Material.EdgeEmission = 2.0f; Data.Material.EdgeColor = { 255, 0, 0, 255 };
	Render( Data, Vertices, Indices );
	const FColor32 EdgeNear = Pixel( 72, 64 );
	const FColor32 EdgeFar = Pixel( 76, 64 );
	Data.Material.EdgeWidth = 0.0f;
	Render( Data, Vertices, Indices );
	const FColor32 EdgeDisabled = Pixel( 72, 64 );
	C.Check( EdgeNear.r > EdgeDisabled.r + 75 && EdgeNear.g >= 58 && EdgeNear.g <= 70 && EdgeNear.b >= 58 && EdgeNear.b <= 70 && EdgeFar.r >= 58 && EdgeFar.r <= 78 && EdgeFar.g >= 58 && EdgeFar.g <= 70, "edge発光は生存画素の赤だけを境界近くへ加え、幅0では消える (near=%d,%d,%d far=%d,%d,%d zero=%d,%d,%d)", EdgeNear.r, EdgeNear.g, EdgeNear.b, EdgeFar.r, EdgeFar.g, EdgeFar.b, EdgeDisabled.r, EdgeDisabled.g, EdgeDisabled.b );
	Data.Material.MaskTexture = -1; Data.Material.bDissolve = 0; Data.Material.EdgeWidth = 0.0f; Data.Material.EdgeEmission = 2.0f; Data.Material.EdgeColor = { 255, 128, 32, 255 }; Data.Dissolve = 0.0f;
	// 固定RGBAを半分のalphaで重ねる頂点配列。
	FMeshVertex ColoredVertices[4]{};
	std::memcpy( ColoredVertices, Vertices, sizeof( Vertices ) );
	for ( int32 Index = 0; Index < 4; ++Index ) { ColoredVertices[Index].Color[0] = 128; ColoredVertices[Index].Color[1] = 64; ColoredVertices[Index].Color[2] = 32; ColoredVertices[Index].Color[3] = 128; }
	Data.Texture = WhiteGraph; Data.Emission = 1.0f; Data.Material.MaskTexture = -1; Data.Material.bDissolve = 0; Data.Material.EdgeWidth = 0.0f; Data.DistortionStrength = 0.0f; Data.DistortionTexture = -1;
	Render( Data, ColoredVertices, Indices, nullptr, nullptr, EDrawBlend::Alpha );
	const FColor32 VertexAlphaPixel = Pixel( 48, 64 );
	Render( Data, ColoredVertices, Indices, nullptr, nullptr, EDrawBlend::Add );
	const FColor32 VertexAddPixel = Pixel( 48, 64 );
	C.Check( NMath::Abs( static_cast<int32>( VertexAlphaPixel.r ) - 70 ) <= 7 && NMath::Abs( static_cast<int32>( VertexAlphaPixel.g ) - 38 ) <= 7 && NMath::Abs( static_cast<int32>( VertexAlphaPixel.b ) - 22 ) <= 7 && NMath::Abs( static_cast<int32>( VertexAddPixel.r ) - 76 ) <= 7 && NMath::Abs( static_cast<int32>( VertexAddPixel.g ) - 44 ) <= 7 && NMath::Abs( static_cast<int32>( VertexAddPixel.b ) - 28 ) <= 7, "頂点RGBAとalpha/add合成は既知背景12・source128/64/32から独立期待値になる (alpha=%d,%d,%d add=%d,%d,%d)", VertexAlphaPixel.r, VertexAlphaPixel.g, VertexAlphaPixel.b, VertexAddPixel.r, VertexAddPixel.g, VertexAddPixel.b );
	Data.Texture = DimGraph;
	// 四角い穴を持つringの頂点配列と24個の三角形index。
	FMeshVertex RingVertices[8]{};
	uint32 RingIndices[24]{};
	MakeRing( RingVertices, RingIndices );
	FDrawMaterialMeshData RingData = Data;
	RingData.NumVertices = 8; RingData.NumIndices = 24; RingData.Emission = 1.0f;
	Render( RingData, RingVertices, RingIndices );
	const FColor32 RingCenter = Pixel( 64, 64 );
	const FColor32 RingBand = Pixel( 42, 64 );
	C.Check( RingCenter.r < 20 && RingCenter.g < 20 && RingCenter.b < 20 && RingBand.r > 45, "面積の異なるring三角形を描き、中央の幾何穴を背景のまま残す" );
	// 2枚を異なる深度へずらして遮蔽を読む描画手順。
	const auto RenderDepthPair = [&]( FDrawMaterialMeshData NearData, FDrawMaterialMeshData FarData, int32 NearWrite )
	{
		// 前後位置を分けた頂点配列。
		FMeshVertex NearVertices[4]{};
		FMeshVertex FarVertices[4]{};
		std::memcpy( NearVertices, Vertices, sizeof( Vertices ) ); std::memcpy( FarVertices, Vertices, sizeof( Vertices ) );
		for ( int32 Index = 0; Index < 4; ++Index ) { NearVertices[Index].Position[2] = -1.0f; FarVertices[Index].Position[2] = 1.0f; }
		NearData.NumVertices = 4; NearData.NumIndices = 6; FarData.NumVertices = 4; FarData.NumIndices = 6;
		NearData.Material.DepthTest = 1; NearData.Material.DepthWrite = NearWrite;
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialMesh3D( NearData, NearVertices, Indices ); Draw.MaterialMesh3D( FarData, FarVertices, Indices );
		Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
	};
	// 四組の深度flagが比較と書込みを独立に反映するかを確認する。
	C.Group = "effect-material-mesh-depth";
	for ( int32 DepthTest = 0; DepthTest <= 1; ++DepthTest ) for ( int32 DepthWrite = 0; DepthWrite <= 1; ++DepthWrite )
	{
		// 深度書込みを切り替えた手前赤と、比較有効の奥青。
		FDrawMaterialMeshData WriteNear = Data; WriteNear.Texture = RedGraph;
		FDrawMaterialMeshData WriteFar = Data; WriteFar.Texture = BlueGraph; WriteFar.Material.DepthTest = 1; WriteFar.Material.DepthWrite = 0;
		RenderDepthPair( WriteNear, WriteFar, DepthWrite );
		const FColor32 WriteResult = Pixel( 64, 64 );
		C.Check( ( DepthWrite == 1 ? WriteResult.r > WriteResult.b : WriteResult.b > WriteResult.r ), "DepthWrite=%d は後続の奥側meshを遮蔽する (RGB=%u,%u,%u)", DepthWrite, WriteResult.r, WriteResult.g, WriteResult.b );
		// 固定した手前深度を奥青の比較flagだけで通すか試す。
		FDrawMaterialMeshData TestNear = Data; TestNear.Texture = RedGraph;
		FDrawMaterialMeshData TestFar = Data; TestFar.Texture = BlueGraph; TestFar.Material.DepthTest = DepthTest; TestFar.Material.DepthWrite = DepthWrite;
		RenderDepthPair( TestNear, TestFar, 1 );
		const FColor32 TestResult = Pixel( 64, 64 );
		C.Check( ( DepthTest == 1 ? TestResult.r > TestResult.b : TestResult.b > TestResult.r ), "DepthTest=%d は手前meshと奥meshの比較結果を選ぶ (RGB=%u,%u,%u)", DepthTest, TestResult.r, TestResult.g, TestResult.b );
	}
	// caller rasterが裏面を捨てる条件と、mesh側の両面指定を確認する。
	C.Group = "effect-material-mesh-two-sided";
	ID3D11Device* Device = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
	ID3D11RasterizerState* OriginalRaster = nullptr;
	ID3D11RasterizerState* BackCullRaster = nullptr;
	if ( Device != nullptr && Context != nullptr )
	{
		Context->RSGetState( &OriginalRaster );
		D3D11_RASTERIZER_DESC CullDesc{};
		if ( OriginalRaster != nullptr ) OriginalRaster->GetDesc( &CullDesc ); else { CullDesc.FillMode = D3D11_FILL_SOLID; CullDesc.DepthClipEnable = TRUE; }
		CullDesc.CullMode = D3D11_CULL_BACK; CullDesc.FrontCounterClockwise = FALSE;
		const HRESULT CullCreated = Device->CreateRasterizerState( &CullDesc, &BackCullRaster );
		C.Check( SUCCEEDED( CullCreated ), "裏面比較用のcaller raster stateを作れる (HRESULT=0x%08X)", static_cast<unsigned int>( CullCreated ) );
		if ( SUCCEEDED( CullCreated ) )
		{
			// 外向き三角形を反転したindex順。
			uint32 ReverseIndices[6] = { 0, 2, 1, 0, 3, 2 };
			RasterAfterFlush = BackCullRaster;
			FDrawMaterialMeshData BackFace = Data; BackFace.Texture = DimGraph; BackFace.bTwoSided = 0;
			Render( BackFace, Vertices, ReverseIndices, RasterAfterFlush );
			const FColor32 Culled = Pixel( 64, 64 );
			BackFace.bTwoSided = 1; Render( BackFace, Vertices, ReverseIndices, RasterAfterFlush );
			const FColor32 Visible = Pixel( 64, 64 );
			C.Check( Culled.r < 20 && Culled.g < 20 && Culled.b < 20 && Visible.r > 45 && Visible.g > 45 && Visible.b > 45, "TwoSided=0はcallerの裏面cullingを継続し、1は裏面も描く" );
		}
		Context->RSSetState( OriginalRaster );
	}
	C.Check( Device != nullptr && Context != nullptr, "両面判定でDirect3D device/contextが取得できる" );
	if ( BackCullRaster != nullptr ) BackCullRaster->Release();
	if ( OriginalRaster != nullptr ) OriginalRaster->Release();
	//材質描画が変更するnative slotを別状態で埋めて完全復元を検査する。
	C.Group = "effect-material-mesh-native-state";
	ID3D11Device* NativeDevice = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
	ID3D11DeviceContext* NativeContext = Context;
	// VSとGSで非nullのstage sentinelを作る小さなshader source。
	const char* SentinelShaderSource = "struct V { float4 P : SV_POSITION; }; V VSMain(uint I : SV_VertexID) { V O; O.P = float4(0,0,0,1); return O; } [maxvertexcount(1)] void GSMain(point V I[1], inout PointStream<V> O) { O.Append(I[0]); }";
	// shader compile時の診断とentry/profileを受け取るblob。
	ID3DBlob* SentinelBlob = nullptr;
	ID3DBlob* SentinelErrors = nullptr;
	const auto CompileSentinel = [&]( const char* Entry, const char* Profile )
	{
		if ( SentinelErrors != nullptr ) { SentinelErrors->Release(); SentinelErrors = nullptr; }
		if ( SentinelBlob != nullptr ) { SentinelBlob->Release(); SentinelBlob = nullptr; }
		return SUCCEEDED( D3DCompile( SentinelShaderSource, std::strlen( SentinelShaderSource ), nullptr, nullptr, nullptr, Entry, Profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &SentinelBlob, &SentinelErrors ) );
	};
	// rendererが上書きするvertex/geometry stage用sentinel shader。
	ID3D11VertexShader* SentinelVS = nullptr;
	ID3D11GeometryShader* SentinelGS = nullptr;
	HRESULT NativeCreateResult = E_FAIL;
	if ( NativeDevice != nullptr && CompileSentinel( "VSMain", "vs_5_0" ) ) NativeCreateResult = NativeDevice->CreateVertexShader( SentinelBlob->GetBufferPointer(), SentinelBlob->GetBufferSize(), nullptr, &SentinelVS );
	if ( SUCCEEDED( NativeCreateResult ) && CompileSentinel( "GSMain", "gs_5_0" ) ) NativeCreateResult = NativeDevice->CreateGeometryShader( SentinelBlob->GetBufferPointer(), SentinelBlob->GetBufferSize(), nullptr, &SentinelGS );
	// IA sentinel vertex/index bufferとshader定数buffer。
	ID3D11Buffer* SentinelVB = nullptr;
	ID3D11Buffer* SentinelIB = nullptr;
	ID3D11Buffer* SentinelVSCB = nullptr;
	ID3D11Buffer* SentinelPSCB = nullptr;
	D3D11_BUFFER_DESC SentinelBufferDesc{};
	SentinelBufferDesc.ByteWidth = 256; SentinelBufferDesc.Usage = D3D11_USAGE_DEFAULT; SentinelBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateBuffer( &SentinelBufferDesc, nullptr, &SentinelVB );
	SentinelBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateBuffer( &SentinelBufferDesc, nullptr, &SentinelIB );
	SentinelBufferDesc.ByteWidth = 16; SentinelBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateBuffer( &SentinelBufferDesc, nullptr, &SentinelVSCB );
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateBuffer( &SentinelBufferDesc, nullptr, &SentinelPSCB );
	// 3つのPS resource slotへ結ぶnoise SRV。
	ID3D11ShaderResourceView* SentinelSRV = nullptr;
	const void* SentinelTexture = GetGraphID3D11Texture2D( NoiseGraph );
	if ( SUCCEEDED( NativeCreateResult ) && SentinelTexture != nullptr ) NativeCreateResult = NativeDevice->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( SentinelTexture ) ), nullptr, &SentinelSRV );
	else if ( SentinelTexture == nullptr ) NativeCreateResult = E_FAIL;
	// point-border sampleを行うcaller sampler。
	ID3D11SamplerState* SentinelSampler = nullptr;
	D3D11_SAMPLER_DESC SentinelSamplerDesc{};
	SentinelSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; SentinelSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER; SentinelSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER; SentinelSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER; SentinelSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateSamplerState( &SentinelSamplerDesc, &SentinelSampler );
	// 元材質blendと異なるcaller blend descriptor。
	ID3D11BlendState* SentinelBlend = nullptr;
	D3D11_BLEND_DESC SentinelBlendDesc{};
	SentinelBlendDesc.RenderTarget[0].BlendEnable = TRUE; SentinelBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; SentinelBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO; SentinelBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; SentinelBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; SentinelBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; SentinelBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; SentinelBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateBlendState( &SentinelBlendDesc, &SentinelBlend );
	// stencil referenceを変えて復元するcaller depth state。
	ID3D11DepthStencilState* SentinelDepth = nullptr;
	D3D11_DEPTH_STENCIL_DESC SentinelDepthDesc{};
	SentinelDepthDesc.DepthEnable = TRUE; SentinelDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; SentinelDepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS; SentinelDepthDesc.StencilEnable = TRUE; SentinelDepthDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK; SentinelDepthDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK; SentinelDepthDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP; SentinelDepthDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP; SentinelDepthDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP; SentinelDepthDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS; SentinelDepthDesc.BackFace = SentinelDepthDesc.FrontFace;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateDepthStencilState( &SentinelDepthDesc, &SentinelDepth );
	// callerのraster sentinelは通常の材質両面状態と異なるback cull。
	ID3D11RasterizerState* SentinelRaster = nullptr;
	D3D11_RASTERIZER_DESC SentinelRasterDesc{};
	SentinelRasterDesc.FillMode = D3D11_FILL_SOLID; SentinelRasterDesc.CullMode = D3D11_CULL_BACK; SentinelRasterDesc.DepthClipEnable = TRUE;
	if ( SUCCEEDED( NativeCreateResult ) ) NativeCreateResult = NativeDevice->CreateRasterizerState( &SentinelRasterDesc, &SentinelRaster );
	const bool bNativeSentinelReady = SUCCEEDED( NativeCreateResult ) && SentinelVS != nullptr && SentinelGS != nullptr && SentinelVB != nullptr && SentinelIB != nullptr && SentinelVSCB != nullptr && SentinelPSCB != nullptr && SentinelSRV != nullptr && SentinelSampler != nullptr && SentinelBlend != nullptr && SentinelDepth != nullptr && SentinelRaster != nullptr;
	C.Check( bNativeSentinelReady && NativeContext != nullptr, "D3D11 native state sentinelを作成する (HRESULT=0x%08X)", static_cast<unsigned int>( NativeCreateResult ) );
	if ( bNativeSentinelReady && NativeContext != nullptr )
	{
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 ); ClearDrawScreen(); RenderVertex();
		// 元のDxLib native stateを検査後に戻すsnapshot。
		FMeshNativeSnapshot OriginalNativeState;
		OriginalNativeState.Save( NativeContext );
		const UINT SentinelStride = sizeof( FMeshVertex ); const UINT SentinelOffset = 4; const UINT SentinelIndexOffset = 8;
		NativeContext->IASetVertexBuffers( 0, 1, &SentinelVB, &SentinelStride, &SentinelOffset ); NativeContext->IASetIndexBuffer( SentinelIB, DXGI_FORMAT_R32_UINT, SentinelIndexOffset ); NativeContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP );
		NativeContext->VSSetShader( SentinelVS, nullptr, 0 ); NativeContext->GSSetShader( SentinelGS, nullptr, 0 ); NativeContext->VSSetConstantBuffers( 0, 1, &SentinelVSCB ); NativeContext->PSSetConstantBuffers( 0, 1, &SentinelPSCB );
		ID3D11ShaderResourceView* SentinelResources[3] = { SentinelSRV, SentinelSRV, SentinelSRV };
		NativeContext->PSSetShaderResources( 0, 3, SentinelResources ); NativeContext->PSSetSamplers( 0, 1, &SentinelSampler );
		const FLOAT SentinelBlendFactor[4] = { 0.13f, 0.27f, 0.39f, 0.51f }; const UINT SentinelSampleMask = 0x5aa55aa5u; const UINT SentinelStencilRef = 37;
		NativeContext->OMSetBlendState( SentinelBlend, SentinelBlendFactor, SentinelSampleMask ); NativeContext->OMSetDepthStencilState( SentinelDepth, SentinelStencilRef ); NativeContext->RSSetState( SentinelRaster );
		// executor実行前にbindした全slotの照合値。
		FMeshNativeSnapshot SentinelState;
		SentinelState.Save( NativeContext );
		C.Check( SentinelState.Matches( NativeContext ), "材質前にVS/PS/GS/HS/DSとIA/IB/CB0/SRV0-2/sampler/blend/depth/raster sentinelを設定する" );
		// 左pixelのU≈0.25へ+X 0.35を足し、既知source青領域を読む材質data。
		FDrawMaterialMeshData SentinelData = Data; SentinelData.Texture = Source; SentinelData.Material.UVScale[0] = 1.0f; SentinelData.Material.UVScale[1] = 1.0f; SentinelData.Material.MaskTexture = -1; SentinelData.Material.bDissolve = 0; SentinelData.Material.DepthTest = 1; SentinelData.Material.DepthWrite = 0; SentinelData.Material.EdgeWidth = 0.0f; SentinelData.UVOffset[0] = 0.0f; SentinelData.UVOffset[1] = 0.0f; SentinelData.Dissolve = 0.0f; SentinelData.Emission = 1.0f; SentinelData.DistortionTexture = NoiseGraph; SentinelData.DistortionStrength = 0.35f; SentinelData.DistortionUVScale[0] = 1.0f; SentinelData.DistortionUVScale[1] = 1.0f; SentinelData.DistortionUVOffset[0] = 0.0f; SentinelData.DistortionUVOffset[1] = 0.0f;
		FDrawBuffer SentinelBuffer;
		FDrawContext SentinelDraw( SentinelBuffer );
		SentinelDraw.MaterialMesh3D( SentinelData, Vertices, Indices );
		// 材質命令が一つだけ記録されたことを実payloadで数える。
		int32 SentinelMeshCount = 0;
		ForEachDrawCommand( FDrawSpan { SentinelBuffer.Words.data(), static_cast<int32>( SentinelBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) ++SentinelMeshCount; } );
		Executor.Execute( FDrawSpan { SentinelBuffer.Words.data(), static_cast<int32>( SentinelBuffer.Words.size() ) } );
		C.Check( SentinelMeshCount == 1 && SentinelState.Matches( NativeContext ), "実mesh描画後にVS/PS/GS/HS/DS、IA/IB、CB0、SRV0-2、sampler、blend係数・mask、depth/stencil、rasterを復元する" );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// U≈0.25からnoise変位した左側の既知pixel。
		const FColor32 SentinelPixel = Pixel( 48, 64 );
		C.Check( SentinelMeshCount == 1 && SentinelPixel.b > 150 && SentinelPixel.r < 60, "全state sentinel中もnoise変位後の既知青pixelを実描画する (RGB=%d,%d,%d)", SentinelPixel.r, SentinelPixel.g, SentinelPixel.b );
		OriginalNativeState.Restore( NativeContext );
		C.Check( OriginalNativeState.Matches( NativeContext ), "sentinel test終了時に元のDxLib native stateを戻す" );
	}
	if ( SentinelVS != nullptr ) SentinelVS->Release(); if ( SentinelGS != nullptr ) SentinelGS->Release(); if ( SentinelVB != nullptr ) SentinelVB->Release(); if ( SentinelIB != nullptr ) SentinelIB->Release(); if ( SentinelVSCB != nullptr ) SentinelVSCB->Release(); if ( SentinelPSCB != nullptr ) SentinelPSCB->Release(); if ( SentinelSRV != nullptr ) SentinelSRV->Release(); if ( SentinelSampler != nullptr ) SentinelSampler->Release(); if ( SentinelBlend != nullptr ) SentinelBlend->Release(); if ( SentinelDepth != nullptr ) SentinelDepth->Release(); if ( SentinelRaster != nullptr ) SentinelRaster->Release();
	if ( SentinelBlob != nullptr ) SentinelBlob->Release(); if ( SentinelErrors != nullptr ) SentinelErrors->Release();
	// 壊れた側とは別領域へ描く既知の正常mesh命令を用意する。
	FDrawMaterialMeshData RecoveryData = Data;
	RecoveryData.Texture = BlueGraph;
	FMeshVertex RecoveryVertices[4]{};
	std::memcpy( RecoveryVertices, Vertices, sizeof( Vertices ) );
	for ( int32 Index = 0; Index < 4; ++Index ) RecoveryVertices[Index].Position[0] += 2.0f;
	FDrawBuffer RecoveryBuffer;
	FDrawContext RecoveryDraw( RecoveryBuffer );
	RecoveryDraw.Blend( EDrawBlend::Alpha, 255 ); RecoveryDraw.MaterialMesh3D( RecoveryData, RecoveryVertices, Indices );
	// 壊れた左側が空のままで、右側の正常meshが出るかを読む。
	const auto RenderMalformedThenValid = [&]( const std::vector<uint32>& InvalidWords )
	{
		std::vector<uint32> CombinedWords = InvalidWords;
		CombinedWords.insert( CombinedWords.end(), RecoveryBuffer.Words.begin(), RecoveryBuffer.Words.end() );
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		Executor.Execute( FDrawSpan { CombinedWords.data(), static_cast<int32>( CombinedWords.size() ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		return std::make_pair( Pixel( 56, 48 ), Pixel( 96, 64 ) );
	};
	// 固定headerより短いが次命令へ移れる材質mesh command。
	const std::vector<uint32> ShortMeshCommand = { NDrawCommand::MakeHeader( EDrawCommand::MaterialMesh3D, 0, 2 ), 0u };
	const auto AfterShortHeader = RenderMalformedThenValid( ShortMeshCommand );
	C.Check( AfterShortHeader.first.r < 20 && AfterShortHeader.first.g < 20 && AfterShortHeader.second.b > 150 && AfterShortHeader.second.r < 60, "短いmesh headerを拒否し、左を背景・右の後続meshを表示する (left=%d,%d,%d right=%d,%d,%d)", AfterShortHeader.first.r, AfterShortHeader.first.g, AfterShortHeader.first.b, AfterShortHeader.second.r, AfterShortHeader.second.g, AfterShortHeader.second.b );
	// 上限を超える頂点数を固定headerだけに詰めたcommand。
	FDrawMaterialMeshData OversizedData = RecoveryData;
	OversizedData.NumVertices = FDrawMaterialMeshData::MaxVertices + 1;
	std::vector<uint32> OversizedCommand( 1 + sizeof( OversizedData ) / 4 );
	OversizedCommand[0] = NDrawCommand::MakeHeader( EDrawCommand::MaterialMesh3D, 0, static_cast<int32>( OversizedCommand.size() ) );
	std::memcpy( OversizedCommand.data() + 1, &OversizedData, sizeof( OversizedData ) );
	const auto AfterOversizedCount = RenderMalformedThenValid( OversizedCommand );
	C.Check( AfterOversizedCount.first.r < 20 && AfterOversizedCount.first.g < 20 && AfterOversizedCount.second.b > 150 && AfterOversizedCount.second.r < 60, "頂点数上限を超えるheaderを拒否し、左背景と右meshを保つ (left=%d,%d,%d right=%d,%d,%d)", AfterOversizedCount.first.r, AfterOversizedCount.first.g, AfterOversizedCount.first.b, AfterOversizedCount.second.r, AfterOversizedCount.second.g, AfterOversizedCount.second.b );
	// 2頂点分しか含めない短い可変payload command。
	FDrawMaterialMeshData TruncatedData = RecoveryData;
	const size_t FixedMeshWords = sizeof( TruncatedData ) / 4;
	std::vector<uint32> TruncatedCommand( 1 + FixedMeshWords + 2 );
	TruncatedCommand[0] = NDrawCommand::MakeHeader( EDrawCommand::MaterialMesh3D, 0, static_cast<int32>( TruncatedCommand.size() ) );
	std::memcpy( TruncatedCommand.data() + 1, &TruncatedData, sizeof( TruncatedData ) );
	const auto AfterTruncatedPayload = RenderMalformedThenValid( TruncatedCommand );
	C.Check( AfterTruncatedPayload.first.r < 20 && AfterTruncatedPayload.first.g < 20 && AfterTruncatedPayload.second.b > 150 && AfterTruncatedPayload.second.r < 60, "期待長より短いmesh payloadを拒否し、左背景と右meshを保つ (left=%d,%d,%d right=%d,%d,%d)", AfterTruncatedPayload.first.r, AfterTruncatedPayload.first.g, AfterTruncatedPayload.first.b, AfterTruncatedPayload.second.r, AfterTruncatedPayload.second.g, AfterTruncatedPayload.second.b );
	// 通常長command内の最後indexだけを頂点範囲外へ壊す。
	FMeshVertex InvalidVertices[4]{};
	std::memcpy( InvalidVertices, Vertices, sizeof( Vertices ) );
	for ( int32 Index = 0; Index < 4; ++Index ) InvalidVertices[Index].Position[0] -= 2.0f;
	FDrawMaterialMeshData InvalidData = RecoveryData;
	InvalidData.Texture = RedGraph;
	FDrawBuffer InvalidBuffer;
	FDrawContext InvalidDraw( InvalidBuffer );
	InvalidDraw.Blend( EDrawBlend::Alpha, 255 ); InvalidDraw.MaterialMesh3D( InvalidData, InvalidVertices, Indices );
	std::vector<uint32> LateIndexCommand = InvalidBuffer.Words;
	const size_t MeshHeaderOffset = 3;
	const size_t MeshDataOffset = MeshHeaderOffset + 1;
	const size_t MeshVertexWords = static_cast<size_t>( InvalidData.NumVertices ) * sizeof( FMeshVertex ) / 4;
	LateIndexCommand[MeshDataOffset + FixedMeshWords + MeshVertexWords + InvalidData.NumIndices - 1] = static_cast<uint32>( InvalidData.NumVertices );
	const auto AfterLateIndex = RenderMalformedThenValid( LateIndexCommand );
	C.Check( AfterLateIndex.first.r < 20 && AfterLateIndex.first.g < 20 && AfterLateIndex.second.b > 150 && AfterLateIndex.second.r < 60, "後半index違反の左meshを拒否し右の正常meshを描く (left=%d,%d,%d right=%d,%d,%d)", AfterLateIndex.first.r, AfterLateIndex.first.g, AfterLateIndex.first.b, AfterLateIndex.second.r, AfterLateIndex.second.g, AfterLateIndex.second.b );
	// 最後の頂点位置をNaNにし、先頭triangleが部分描画されないことを確かめる。
	std::vector<uint32> NonFiniteVertexCommand = InvalidBuffer.Words;
	const float32 InvalidPosition = TNumericLimits<float32>::QuietNaN();
	const size_t LastVertexPositionWord = MeshDataOffset + FixedMeshWords + 3 * ( sizeof( FMeshVertex ) / 4 );
	std::memcpy( NonFiniteVertexCommand.data() + LastVertexPositionWord, &InvalidPosition, sizeof( InvalidPosition ) );
	const auto AfterNonFiniteVertex = RenderMalformedThenValid( NonFiniteVertexCommand );
	C.Check( AfterNonFiniteVertex.first.r < 20 && AfterNonFiniteVertex.first.g < 20 && AfterNonFiniteVertex.second.b > 150 && AfterNonFiniteVertex.second.r < 60, "後半NaN頂点で左triangleを描かず右meshを続ける (left=%d,%d,%d right=%d,%d,%d)", AfterNonFiniteVertex.first.r, AfterNonFiniteVertex.first.g, AfterNonFiniteVertex.first.b, AfterNonFiniteVertex.second.r, AfterNonFiniteVertex.second.g, AfterNonFiniteVertex.second.b );
	// 同じ画面へ描く旧Quadが材質meshの前後で全画素を保つか比べる。
	C.Group = "effect-material-mesh-legacy-quad";
	FDrawQuad3DData LegacyQuad;
	LegacyQuad.Handle = Source; LegacyQuad.Pos[2] = 0.0f; LegacyQuad.Width = 4.0f; LegacyQuad.Height = 4.0f; LegacyQuad.Orient = static_cast<int32>( EDrawQuadOrient::Fixed ); LegacyQuad.Color = { 255, 255, 255, 255 };
	// 旧quadのみ、または同じ材質meshの後に旧quadを描いた画面を作る。
	const auto RenderLegacyQuad = [&]( bool bDrawMeshFirst )
	{
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		FDrawBuffer Buffer; FDrawContext Draw( Buffer );
		if ( bDrawMeshFirst ) { Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.MaterialMesh3D( Data, Vertices, Indices ); }
		Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.Quad3D( LegacyQuad );
		Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
	};
	// 旧quad単独時の画素を全領域保存する。
	TArray<FColor32> LegacyQuadPixels;
	LegacyQuadPixels.SetNum( 128 * 128 );
	RenderLegacyQuad( false );
	for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) LegacyQuadPixels[Y * 128 + X] = Pixel( X, Y );
	RenderLegacyQuad( true );
	bool bLegacyQuadPixelsMatch = true;
	for ( int32 Y = 0; Y < 128 && bLegacyQuadPixelsMatch; ++Y ) for ( int32 X = 0; X < 128; ++X )
	{
		// 旧quad描画後に読み取ったpixel。
		const FColor32 Actual = Pixel( X, Y );
		const FColor32& Expected = LegacyQuadPixels[Y * 128 + X];
		if ( Actual.r != Expected.r || Actual.g != Expected.g || Actual.b != Expected.b || Actual.a != Expected.a ) { bLegacyQuadPixelsMatch = false; break; }
	}
	C.Check( bLegacyQuadPixelsMatch && LegacyQuadPixels[64 * 128 + 48].r > 150 && LegacyQuadPixels[64 * 128 + 80].b > 150, "材質meshの後の旧quadは単独時の全RGBAと既知左右色を保つ" );
	// 既存backendで作る旧Mesh3D用bufferはこのtest内で共有所有する。
	TSharedPtr<CMeshBuffer> LegacyMeshBuffer = CMeshBuffer::Create( TArrayView<const FMeshVertex>( Vertices, 4 ), TArrayView<const uint32>( Indices, 6 ) );
	C.Check( LegacyMeshBuffer.IsValid(), "従来Mesh3Dのvertex/index bufferを実backendで作成する" );
	if ( LegacyMeshBuffer.IsValid() )
	{
		// 同じ画面へ旧Meshだけ、または材質meshの後に旧Meshを描く。
		const auto RenderLegacyMesh = [&]( bool bDrawMaterialFirst )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 );
			if ( bDrawMaterialFirst ) Draw.MaterialMesh3D( Data, Vertices, Indices );
			Draw.Mesh3D( LegacyMeshBuffer->GetVertexBuffer(), LegacyMeshBuffer->GetIndexBuffer(), LegacyMeshBuffer->GetNumVertices(), 0, LegacyMeshBuffer->GetNumIndices(), Source, FMatrix4x4::Identity, true );
			Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		// 旧Mesh単独の全画素を後続比較用に保存する。
		TArray<FColor32> LegacyMeshPixels;
		LegacyMeshPixels.SetNum( 128 * 128 );
		RenderLegacyMesh( false );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) LegacyMeshPixels[Y * 128 + X] = Pixel( X, Y );
		RenderLegacyMesh( true );
		bool bLegacyMeshPixelsMatch = true;
		for ( int32 Y = 0; Y < 128 && bLegacyMeshPixelsMatch; ++Y ) for ( int32 X = 0; X < 128; ++X )
		{
			// 旧Mesh描画後に読み取った画素。
			const FColor32 Actual = Pixel( X, Y );
			const FColor32& Expected = LegacyMeshPixels[Y * 128 + X];
			if ( Actual.r != Expected.r || Actual.g != Expected.g || Actual.b != Expected.b || Actual.a != Expected.a ) { bLegacyMeshPixelsMatch = false; break; }
		}
		C.Check( bLegacyMeshPixelsMatch && LegacyMeshPixels[64 * 128 + 48].r > 150 && LegacyMeshPixels[64 * 128 + 80].b > 150, "材質mesh後の旧Mesh3Dは単独描画と全RGBA・既知左右色を保つ" );
		LegacyMeshBuffer.Reset();
	}
	// 作成した画面を呼び出し元へ戻してからgraphを解放する。
	RenderVertex(); SetDrawScreen( Saved.Screen );
	DeleteGraph( Source ); DeleteGraph( Mask ); DeleteGraph( RampMask ); DeleteGraph( SplitNoise ); DeleteGraph( DerivedNoise ); DeleteGraph( DimGraph ); DeleteGraph( NoiseGraph ); DeleteGraph( RedGraph ); DeleteGraph( BlueGraph ); DeleteGraph( WhiteGraph ); DeleteGraph( DerivedSource ); DeleteGraph( DerivedMask ); DeleteSoftImage( Soft ); DeleteGraph( Screen );
}
