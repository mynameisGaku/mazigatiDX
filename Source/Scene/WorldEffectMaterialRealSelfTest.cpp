// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <functional>
#include <limits>

#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/EffectMaterialRenderer.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// このテストが変更する画面・カメラ・色・混合設定を、描画終了後に戻す。
	struct FMaterialCallerState
	{
		// 使用前の描画先とscreen切替設定。
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 使用前のcamera範囲と画面中心。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 DotAspect = GetCameraDotAspect();
		float32 Center[2] = {};
		// 使用前のcamera行列と各変換行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		// 描画範囲と色・合成設定。
		RECT DrawArea{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// 現在のDxLib設定を保存し、screen切替時の自動設定変更を止める。
		FMaterialCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] );
			GetTransformToWorldMatrix( &World );
			GetTransformToViewMatrix( &View );
			GetTransformToProjectionMatrix( &Projection );
			GetTransformToViewportMatrix( &Viewport );
			GetDrawArea( &DrawArea );
			GetDrawBlendMode( &Blend[0], &Blend[1] );
			GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
			GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 変更したscreen・camera・色・合成設定を呼び出し元へ戻す。
		~FMaterialCallerState()
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
			SetDrawArea( DrawArea.left, DrawArea.top, DrawArea.right, DrawArea.bottom );
			SetDrawBlendMode( Blend[0], Blend[1] );
			SetDrawBright( Bright[0], Bright[1], Bright[2] );
			SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] );
			SetUseZBuffer3D( FALSE );
			SetWriteZBuffer3D( FALSE );
			SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// 指定色で8x8の画像を作り、実機描画で使うDxLibグラフを返す。
	// 指定色の8x8画像を作り、実機描画用のgraph handleを返す。
	int32 MakeColorGraph( uint8 R, uint8 G, uint8 B, uint8 A )
	{
		// 画素を埋める一時soft image。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( Soft, X, Y, R, G, B, A );
		// soft imageの内容から作るDxLib graph handle。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// 横半分ずつ色が異なる画像を作り、UV位置を画素色で判定できるようにする。
	int32 MakeSplitGraph( bool bMask )
	{
		// 左右の色・mask値を設定する一時soft image。
		const int32 Soft = MakeARGB8ColorSoftImage( 8, 8 );
		if ( Soft < 0 ) return -1;
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X )
		{
			if ( bMask ) DrawPixelSoftImage( Soft, X, Y, X < 4 ? 0 : 255, 0, 0, 255 );
			else DrawPixelSoftImage( Soft, X, Y, X < 4 ? 255 : 0, 0, X < 4 ? 0 : 255, 255 );
		}
		// 完成した左右分割graph handle。
		const int32 Graph = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Graph;
	}

	// Direct3D状態をCOM参照付きで撮影し、材質描画の前後を比較する。
	struct FNativeStateSnapshot
	{
		// 入力形式と頂点データの元状態。
		ID3D11InputLayout* Layout = nullptr;
		ID3D11Buffer* VertexBuffer = nullptr;
		// 頂点bufferの要素サイズ・開始位置・読み方。
		UINT Stride = 0;
		UINT Offset = 0;
		D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		// VSとPSの先頭定数buffer。
		ID3D11Buffer* VsConstant = nullptr;
		ID3D11Buffer* PsConstant = nullptr;
		// PSが使う先頭二つの画像とsampler。
		ID3D11ShaderResourceView* Srv0 = nullptr;
		ID3D11ShaderResourceView* Srv1 = nullptr;
		ID3D11SamplerState* Sampler = nullptr;
		// 色混合、深度、stencil参照、rasterizerの元状態。
		ID3D11BlendState* Blend = nullptr;
		// 色合成係数とsample mask、深度/stencil状態。
		FLOAT BlendFactor[4] = {};
		UINT SampleMask = 0;
		ID3D11DepthStencilState* Depth = nullptr;
		UINT StencilRef = 0;
		ID3D11RasterizerState* Raster = nullptr;
		// 描画先と表示・切り取り範囲。
		ID3D11RenderTargetView* RenderTarget = nullptr;
		ID3D11DepthStencilView* DepthTarget = nullptr;
		// native viewportとscissorの現在値・有効件数。
		D3D11_VIEWPORT Viewports[16] = {};
		UINT ViewportCount = 16;
		D3D11_RECT Scissors[16] = {};
		UINT ScissorCount = 16;
		// 全shader段階と、そのshaderが使うclass instance。
		ID3D11DeviceChild* Shaders[5] = {};
		ID3D11ClassInstance* Instances[5][256] = {};
		UINT InstanceCounts[5] = { 256, 256, 256, 256, 256 };

		// 指定contextからstateとCOM参照を取得する。
		explicit FNativeStateSnapshot( ID3D11DeviceContext* Context )
		{
			Context->IAGetInputLayout( &Layout ); Context->IAGetVertexBuffers( 0, 1, &VertexBuffer, &Stride, &Offset ); Context->IAGetPrimitiveTopology( &Topology );
			Context->VSGetConstantBuffers( 0, 1, &VsConstant ); Context->PSGetConstantBuffers( 0, 1, &PsConstant ); Context->PSGetShaderResources( 0, 1, &Srv0 ); Context->PSGetShaderResources( 1, 1, &Srv1 ); Context->PSGetSamplers( 0, 1, &Sampler );
			Context->OMGetBlendState( &Blend, BlendFactor, &SampleMask ); Context->OMGetDepthStencilState( &Depth, &StencilRef ); Context->RSGetState( &Raster );
			Context->OMGetRenderTargets( 1, &RenderTarget, &DepthTarget ); Context->RSGetViewports( &ViewportCount, Viewports ); Context->RSGetScissorRects( &ScissorCount, Scissors );
			Context->VSGetShader( reinterpret_cast<ID3D11VertexShader**>( &Shaders[0] ), Instances[0], &InstanceCounts[0] ); Context->PSGetShader( reinterpret_cast<ID3D11PixelShader**>( &Shaders[1] ), Instances[1], &InstanceCounts[1] ); Context->GSGetShader( reinterpret_cast<ID3D11GeometryShader**>( &Shaders[2] ), Instances[2], &InstanceCounts[2] ); Context->HSGetShader( reinterpret_cast<ID3D11HullShader**>( &Shaders[3] ), Instances[3], &InstanceCounts[3] ); Context->DSGetShader( reinterpret_cast<ID3D11DomainShader**>( &Shaders[4] ), Instances[4], &InstanceCounts[4] );
		}

		// getterが増やしたすべてのCOM参照を解放する。
		~FNativeStateSnapshot()
		{
			if ( Layout != nullptr ) Layout->Release(); if ( VertexBuffer != nullptr ) VertexBuffer->Release(); if ( VsConstant != nullptr ) VsConstant->Release(); if ( PsConstant != nullptr ) PsConstant->Release(); if ( Srv0 != nullptr ) Srv0->Release(); if ( Srv1 != nullptr ) Srv1->Release(); if ( Sampler != nullptr ) Sampler->Release(); if ( Blend != nullptr ) Blend->Release(); if ( Depth != nullptr ) Depth->Release(); if ( Raster != nullptr ) Raster->Release(); if ( RenderTarget != nullptr ) RenderTarget->Release(); if ( DepthTarget != nullptr ) DepthTarget->Release();
			for ( int32 ShaderIndex = 0; ShaderIndex < 5; ++ShaderIndex ) { if ( Shaders[ShaderIndex] != nullptr ) Shaders[ShaderIndex]->Release(); for ( UINT Index = 0; Index < InstanceCounts[ShaderIndex]; ++Index ) if ( Instances[ShaderIndex][Index] != nullptr ) Instances[ShaderIndex][Index]->Release(); }
		}

		// 現contextの値・参照が撮影時と一致するか返す。
		bool Matches( ID3D11DeviceContext* Context ) const
		{
			// 照合相手の状態を参照付きで取得する一時snapshot。
			FNativeStateSnapshot Current( Context );
			if ( Layout != Current.Layout || VertexBuffer != Current.VertexBuffer || Stride != Current.Stride || Offset != Current.Offset || Topology != Current.Topology || VsConstant != Current.VsConstant || PsConstant != Current.PsConstant || Srv0 != Current.Srv0 || Srv1 != Current.Srv1 || Sampler != Current.Sampler || Blend != Current.Blend || SampleMask != Current.SampleMask || Depth != Current.Depth || StencilRef != Current.StencilRef || Raster != Current.Raster || RenderTarget != Current.RenderTarget || DepthTarget != Current.DepthTarget || ViewportCount != Current.ViewportCount || ScissorCount != Current.ScissorCount || std::memcmp( BlendFactor, Current.BlendFactor, sizeof( BlendFactor ) ) != 0 || std::memcmp( Viewports, Current.Viewports, sizeof( D3D11_VIEWPORT ) * ViewportCount ) != 0 || std::memcmp( Scissors, Current.Scissors, sizeof( D3D11_RECT ) * ScissorCount ) != 0 ) return false;
			for ( int32 ShaderIndex = 0; ShaderIndex < 5; ++ShaderIndex )
			{
				if ( Shaders[ShaderIndex] != Current.Shaders[ShaderIndex] || InstanceCounts[ShaderIndex] != Current.InstanceCounts[ShaderIndex] ) return false;
				// 各shaderが保持するclass instanceも同じ並びか照合する。
				for ( UINT Index = 0; Index < InstanceCounts[ShaderIndex]; ++Index ) if ( Instances[ShaderIndex][Index] != Current.Instances[ShaderIndex][Index] ) return false;
			}
			return true;
		}

		// 撮影した画面先とDirect3D状態を呼出元へ戻す。
		void Restore( ID3D11DeviceContext* Context ) const
		{
			Context->OMSetRenderTargets( 1, &RenderTarget, DepthTarget ); Context->RSSetViewports( ViewportCount, Viewports ); Context->RSSetScissorRects( ScissorCount, Scissors );
			Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, &VertexBuffer, &Stride, &Offset ); Context->IASetPrimitiveTopology( Topology ); Context->VSSetConstantBuffers( 0, 1, &VsConstant ); Context->PSSetConstantBuffers( 0, 1, &PsConstant ); Context->PSSetShaderResources( 0, 1, &Srv0 ); Context->PSSetShaderResources( 1, 1, &Srv1 ); Context->PSSetSamplers( 0, 1, &Sampler ); Context->OMSetBlendState( Blend, BlendFactor, SampleMask ); Context->OMSetDepthStencilState( Depth, StencilRef ); Context->RSSetState( Raster );
			Context->VSSetShader( static_cast<ID3D11VertexShader*>( Shaders[0] ), Instances[0], InstanceCounts[0] ); Context->PSSetShader( static_cast<ID3D11PixelShader*>( Shaders[1] ), Instances[1], InstanceCounts[1] ); Context->GSSetShader( static_cast<ID3D11GeometryShader*>( Shaders[2] ), Instances[2], InstanceCounts[2] ); Context->HSSetShader( static_cast<ID3D11HullShader*>( Shaders[3] ), Instances[3], InstanceCounts[3] ); Context->DSSetShader( static_cast<ID3D11DomainShader*>( Shaders[4] ), Instances[4], InstanceCounts[4] );
		}
	};

	// 異なるshaderと固定機能資源を設定し、quadまたは呼出元command描画後の復元を照合する。
	void TestNativeStateRestore( NAssetTest::FChecker& C, IDrawExecutor& Executor, const FDrawMaterialQuadData& Quad, int32 Soft, const std::function<void()>& DrawCallback = {} )
	{
		// 実機描画で状態を設定・照合するdeviceとimmediate context。
		ID3D11Device* Device = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
		ID3D11DeviceContext* Context = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
		C.Group = DrawCallback ? "effect-material-ribbon-native-state" : "effect-material-native-state";
		if ( Device == nullptr || Context == nullptr ) { C.Check( false, "Direct3D 11の状態復元テストにdeviceとcontextがある" ); return; }
		// 各shader段階へ異なるsentinelを入れるための小さなHLSL source。
		const char* Source = "struct V{float3 p:POSITION;float2 uv:TEXCOORD0;};struct CP{float4 p:SV_POSITION;};float4 VSMain(V v):SV_POSITION{return float4(v.p,1);}float4 PSMain():SV_TARGET{return float4(1,0,1,1);} [maxvertexcount(1)] void GSMain(point float4 v[1]:SV_POSITION,inout PointStream<CP> s){CP o;o.p=v[0];s.Append(o);} struct Factors{float Edge[3]:SV_TessFactor;float Inside:SV_InsideTessFactor;};Factors Patch(InputPatch<CP,3> p,uint id:SV_PrimitiveID){Factors f;f.Edge[0]=f.Edge[1]=f.Edge[2]=f.Inside=1;return f;} [domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)][patchconstantfunc(\"Patch\")] CP HSMain(InputPatch<CP,3> p,uint id:SV_OutputControlPointID){return p[id];} [domain(\"tri\")] float4 DSMain(Factors f,float3 b:SV_DomainLocation,const OutputPatch<CP,3> p):SV_POSITION{return p[0].p*b.x+p[1].p*b.y+p[2].p*b.z;}";
		// compileした各stageのbytecode。
		ID3DBlob* VsBlob = nullptr; ID3DBlob* PsBlob = nullptr; ID3DBlob* GsBlob = nullptr; ID3DBlob* HsBlob = nullptr; ID3DBlob* DsBlob = nullptr;
		// compile失敗時の診断文を受け取るblob。
		ID3DBlob* VsErrors = nullptr; ID3DBlob* PsErrors = nullptr; ID3DBlob* GsErrors = nullptr; ID3DBlob* HsErrors = nullptr; ID3DBlob* DsErrors = nullptr;
		// VS/PS/GS/HS/DSそれぞれのcompile結果。
		const HRESULT VsResult = D3DCompile( Source, std::strlen( Source ), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &VsBlob, &VsErrors );
		const HRESULT PsResult = D3DCompile( Source, std::strlen( Source ), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &PsBlob, &PsErrors );
		const HRESULT GsResult = D3DCompile( Source, std::strlen( Source ), nullptr, nullptr, nullptr, "GSMain", "gs_4_0", 0, 0, &GsBlob, &GsErrors );
		const HRESULT HsResult = D3DCompile( Source, std::strlen( Source ), nullptr, nullptr, nullptr, "HSMain", "hs_5_0", 0, 0, &HsBlob, &HsErrors );
		const HRESULT DsResult = D3DCompile( Source, std::strlen( Source ), nullptr, nullptr, nullptr, "DSMain", "ds_5_0", 0, 0, &DsBlob, &DsErrors );
		// 失敗したshader compileだけで原因を読めるよう、compiler診断文を取り出す。
		// compile診断blobをログ用文字列へ変換する。
		const auto ErrorText = []( ID3DBlob* Errors ) { return Errors != nullptr ? static_cast<const char*>( Errors->GetBufferPointer() ) : "(compiler returned no diagnostic)"; };
		C.Check( SUCCEEDED( VsResult ), "復元用VSのcompile HRESULT=0x%08X: %s", static_cast<unsigned int>( VsResult ), ErrorText( VsErrors ) );
		C.Check( SUCCEEDED( PsResult ), "復元用PSのcompile HRESULT=0x%08X: %s", static_cast<unsigned int>( PsResult ), ErrorText( PsErrors ) );
		C.Check( SUCCEEDED( GsResult ), "復元用GSのcompile HRESULT=0x%08X: %s", static_cast<unsigned int>( GsResult ), ErrorText( GsErrors ) );
		C.Check( SUCCEEDED( HsResult ), "復元用HSのcompile HRESULT=0x%08X: %s", static_cast<unsigned int>( HsResult ), ErrorText( HsErrors ) );
		C.Check( SUCCEEDED( DsResult ), "復元用DSのcompile HRESULT=0x%08X: %s", static_cast<unsigned int>( DsResult ), ErrorText( DsErrors ) );
		if ( VsErrors != nullptr ) VsErrors->Release(); if ( PsErrors != nullptr ) PsErrors->Release(); if ( GsErrors != nullptr ) GsErrors->Release(); if ( HsErrors != nullptr ) HsErrors->Release(); if ( DsErrors != nullptr ) DsErrors->Release();
		// stageごとのshader、input layout、sentinelに使うGPU資源。
		ID3D11VertexShader* Vs = nullptr; ID3D11PixelShader* Ps = nullptr; ID3D11GeometryShader* Gs = nullptr; ID3D11HullShader* Hs = nullptr; ID3D11DomainShader* Ds = nullptr; ID3D11InputLayout* Layout = nullptr; ID3D11Buffer* Vb = nullptr; ID3D11Buffer* VsCb = nullptr; ID3D11Buffer* PsCb = nullptr; ID3D11ShaderResourceView* Srv0 = nullptr; ID3D11ShaderResourceView* Srv1 = nullptr; ID3D11SamplerState* Sampler = nullptr; ID3D11BlendState* Blend = nullptr; ID3D11DepthStencilState* Depth = nullptr; ID3D11RasterizerState* Raster = nullptr;
		// input layoutとvertex bufferへ使う位置・UV形式。
		const D3D11_INPUT_ELEMENT_DESC Elements[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
		// sentinel vertex bufferのサイズと利用方法。
		D3D11_BUFFER_DESC BufferDesc{}; BufferDesc.ByteWidth = 20; BufferDesc.Usage = D3D11_USAGE_DEFAULT; BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		// shaderごとの生成結果を分け、失敗箇所のHRESULTを検査結果へ残す。
		const HRESULT CreateVs = SUCCEEDED( VsResult ) ? Device->CreateVertexShader( VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), nullptr, &Vs ) : E_FAIL;
		const HRESULT CreatePs = SUCCEEDED( PsResult ) ? Device->CreatePixelShader( PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), nullptr, &Ps ) : E_FAIL;
		const HRESULT CreateGs = SUCCEEDED( GsResult ) ? Device->CreateGeometryShader( GsBlob->GetBufferPointer(), GsBlob->GetBufferSize(), nullptr, &Gs ) : E_FAIL;
		const HRESULT CreateHs = SUCCEEDED( HsResult ) ? Device->CreateHullShader( HsBlob->GetBufferPointer(), HsBlob->GetBufferSize(), nullptr, &Hs ) : E_FAIL;
		const HRESULT CreateDs = SUCCEEDED( DsResult ) ? Device->CreateDomainShader( DsBlob->GetBufferPointer(), DsBlob->GetBufferSize(), nullptr, &Ds ) : E_FAIL;
		const HRESULT CreateLayout = SUCCEEDED( VsResult ) ? Device->CreateInputLayout( Elements, 2, VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout ) : E_FAIL;
		C.Check( SUCCEEDED( CreateVs ), "復元用VSの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateVs ) );
		C.Check( SUCCEEDED( CreatePs ), "復元用PSの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreatePs ) );
		C.Check( SUCCEEDED( CreateGs ), "復元用GSの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateGs ) );
		C.Check( SUCCEEDED( CreateHs ), "復元用HSの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateHs ) );
		C.Check( SUCCEEDED( CreateDs ), "復元用DSの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateDs ) );
		C.Check( SUCCEEDED( CreateLayout ), "復元用input layoutの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateLayout ) );
		const bool bShadersReady = SUCCEEDED( CreateVs ) && SUCCEEDED( CreatePs ) && SUCCEEDED( CreateGs ) && SUCCEEDED( CreateHs ) && SUCCEEDED( CreateDs ) && SUCCEEDED( CreateLayout );
		if ( VsBlob != nullptr ) VsBlob->Release(); if ( PsBlob != nullptr ) PsBlob->Release(); if ( GsBlob != nullptr ) GsBlob->Release(); if ( HsBlob != nullptr ) HsBlob->Release(); if ( DsBlob != nullptr ) DsBlob->Release();
		// vertex bufferと2種類の定数bufferの作成失敗を個別に示す。
		const HRESULT CreateVb = bShadersReady ? Device->CreateBuffer( &BufferDesc, nullptr, &Vb ) : E_FAIL;
		C.Check( SUCCEEDED( CreateVb ), "復元用vertex bufferの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateVb ) );
		const bool bBuffersReady = SUCCEEDED( CreateVb );
		BufferDesc.ByteWidth = 16; BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		const HRESULT CreateVsCb = bBuffersReady ? Device->CreateBuffer( &BufferDesc, nullptr, &VsCb ) : E_FAIL;
		const HRESULT CreatePsCb = SUCCEEDED( CreateVsCb ) ? Device->CreateBuffer( &BufferDesc, nullptr, &PsCb ) : E_FAIL;
		C.Check( SUCCEEDED( CreateVsCb ), "復元用VS定数bufferの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateVsCb ) );
		C.Check( SUCCEEDED( CreatePsCb ), "復元用PS定数bufferの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreatePsCb ) );
		const bool bConstantsReady = SUCCEEDED( CreateVsCb ) && SUCCEEDED( CreatePsCb );
		// 本体と異なるaddress/filterを持つsentinel sampler。
		D3D11_SAMPLER_DESC SamplerDesc{}; SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER; SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR; SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		// 固定blend/depth/raster stateを本体の値と区別するdescriptor。
		D3D11_BLEND_DESC BlendDesc{}; BlendDesc.RenderTarget[0].BlendEnable = TRUE; BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO; BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_DEPTH_STENCIL_DESC DepthDesc{}; DepthDesc.DepthEnable = TRUE; DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; DepthDesc.DepthFunc = D3D11_COMPARISON_GREATER;
		D3D11_RASTERIZER_DESC RasterDesc{}; RasterDesc.FillMode = D3D11_FILL_SOLID; RasterDesc.CullMode = D3D11_CULL_FRONT; RasterDesc.DepthClipEnable = TRUE; RasterDesc.ScissorEnable = FALSE;
		// DxLib画像からsentinel用の別SRVを作る元texture。
		const void* Texture0 = GetGraphID3D11Texture2D( Quad.Quad.Handle );
		const void* Texture1 = GetGraphID3D11Texture2D( Quad.MaskTexture );
		// 画像参照と固定機能状態を順番に作り、最初の失敗を記録する。
		const HRESULT CreateSrv0 = bConstantsReady && Texture0 != nullptr ? Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( Texture0 ) ), nullptr, &Srv0 ) : E_FAIL;
		const HRESULT CreateSrv1 = SUCCEEDED( CreateSrv0 ) && Texture1 != nullptr ? Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( Texture1 ) ), nullptr, &Srv1 ) : E_FAIL;
		const HRESULT CreateSampler = SUCCEEDED( CreateSrv1 ) ? Device->CreateSamplerState( &SamplerDesc, &Sampler ) : E_FAIL;
		const HRESULT CreateBlend = SUCCEEDED( CreateSampler ) ? Device->CreateBlendState( &BlendDesc, &Blend ) : E_FAIL;
		const HRESULT CreateDepth = SUCCEEDED( CreateBlend ) ? Device->CreateDepthStencilState( &DepthDesc, &Depth ) : E_FAIL;
		const HRESULT CreateRaster = SUCCEEDED( CreateDepth ) ? Device->CreateRasterizerState( &RasterDesc, &Raster ) : E_FAIL;
		C.Check( Texture0 != nullptr, "復元用元画像handle=%dからD3D11 textureを取得できる", Quad.Quad.Handle );
		C.Check( Texture1 != nullptr, "復元用mask handle=%dからD3D11 textureを取得できる", Quad.MaskTexture );
		C.Check( SUCCEEDED( CreateSrv0 ), "復元用SRV0の生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateSrv0 ) );
		C.Check( SUCCEEDED( CreateSrv1 ), "復元用SRV1の生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateSrv1 ) );
		C.Check( SUCCEEDED( CreateSampler ), "復元用samplerの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateSampler ) );
		C.Check( SUCCEEDED( CreateBlend ), "復元用blend stateの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateBlend ) );
		C.Check( SUCCEEDED( CreateDepth ), "復元用depth stateの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateDepth ) );
		C.Check( SUCCEEDED( CreateRaster ), "復元用raster stateの生成 HRESULT=0x%08X", static_cast<unsigned int>( CreateRaster ) );
		const bool bOtherReady = SUCCEEDED( CreateRaster );
		if ( !bOtherReady ) C.Check( false, "native状態復元用の異なるshader・定数・画像・固定機能資源を作れる" );
		else
		{
			ClearDrawScreen(); RenderVertex();
			// sentinelを入れる直前のDxLib由来native状態を保持する。
			FNativeStateSnapshot Original( Context );
			// sentinel頂点bufferの読み方と識別しやすいblend factor。
			const UINT Stride = 20; const UINT Offset = 0; const FLOAT BlendFactor[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
			Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, &Vb, &Stride, &Offset ); Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP ); Context->VSSetShader( Vs, nullptr, 0 ); Context->PSSetShader( Ps, nullptr, 0 ); Context->GSSetShader( Gs, nullptr, 0 ); Context->HSSetShader( Hs, nullptr, 0 ); Context->DSSetShader( Ds, nullptr, 0 ); Context->VSSetConstantBuffers( 0, 1, &VsCb ); Context->PSSetConstantBuffers( 0, 1, &PsCb ); Context->PSSetShaderResources( 0, 1, &Srv0 ); Context->PSSetShaderResources( 1, 1, &Srv1 ); Context->PSSetSamplers( 0, 1, &Sampler ); Context->OMSetBlendState( Blend, BlendFactor, 0x12345678u ); Context->OMSetDepthStencilState( Depth, 19 ); Context->RSSetState( Raster );
			// sentinelを置いた直後の期待値。Execute後、readback前に照合する。
			FNativeStateSnapshot Before( Context );
			// 1命令だけを所有bufferへ記録し実際のexecutorへ渡す。
			if ( DrawCallback ) DrawCallback();
			else
			{
				FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.MaterialQuad3D( Quad );
				SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 );
				if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			}
			C.Check( Before.Matches( Context ), "材質描画直後にshader・class instance・layout・VB・topology・CB・SRV・sampler・blend・depth・rasterを復元する" );
			GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			// shader sentinelに差し替わらず描けたかを確認する画素。
			int32 PixelR = 0, PixelG = 0, PixelB = 0, PixelA = 0; GetPixelSoftImage( Soft, 64, 64, &PixelR, &PixelG, &PixelB, &PixelA );
			C.Check( PixelR > 50 && PixelG > 20, "sentinel shaderや画像状態があっても材質の既知色を画面へ実際に描く" );
			Original.Restore( Context );
		}
		if ( Raster != nullptr ) Raster->Release(); if ( Depth != nullptr ) Depth->Release(); if ( Blend != nullptr ) Blend->Release(); if ( Sampler != nullptr ) Sampler->Release(); if ( Srv0 != nullptr ) Srv0->Release(); if ( Srv1 != nullptr ) Srv1->Release(); if ( PsCb != nullptr ) PsCb->Release(); if ( VsCb != nullptr ) VsCb->Release(); if ( Vb != nullptr ) Vb->Release(); if ( Layout != nullptr ) Layout->Release(); if ( Ds != nullptr ) Ds->Release(); if ( Hs != nullptr ) Hs->Release(); if ( Gs != nullptr ) Gs->Release(); if ( Ps != nullptr ) Ps->Release(); if ( Vs != nullptr ) Vs->Release();
	}

	// 入力値検証、所有コピー、実機D3D11の材質画素を一つのcheckerへ記録する。
	void TestMaterialQuad( NAssetTest::FChecker& C, IDrawExecutor& Executor )
	{
		C.Group = "effect-material-quad-record";
		// 不正値と所有コピーを調べる描画buffer・context・入力値。
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		FDrawMaterialQuadData Data;
		Data.Quad.Handle = 11;
		// 新commandが記録される直前のbuffer長。
		const int32 StartWords = Draw.GetNumWords();
		Draw.MaterialQuad3D( Data );
		C.Check( Draw.GetNumWords() == StartWords + 1 + sizeof( Data ) / 4 && static_cast<EDrawCommand>( Buffer.Words[StartWords] & 0xFFu ) == EDrawCommand::MaterialQuad3D, "材質付き四角形は固定長の専用描画命令として記録される" );
		// command payloadから読み戻す独立した所有コピー。
		FDrawMaterialQuadData Recorded{};
		std::memcpy( &Recorded, Buffer.Words.data() + StartWords + 1, sizeof( Recorded ) );
		Data.Dissolve = 0.75f;
		C.Check( Recorded.Dissolve == 0.0f && Recorded.Quad.Handle == 11 && Recorded.DepthTest == 1 && Recorded.DepthWrite == 0, "記録は入力値を所有し、既定の深度契約を保持する" );
		const int32 BeforeInvalid = Draw.GetNumWords();
		Data.Dissolve = TNumericLimits<float32>::QuietNaN();
		Draw.MaterialQuad3D( Data );
		Data.Dissolve = 0.0f; Data.DepthTest = 2;
		Draw.MaterialQuad3D( Data );
		Data.DepthTest = 1; Data.Quad.Width = 0.0f;
		Draw.MaterialQuad3D( Data );
		C.Check( Draw.GetNumWords() == BeforeInvalid, "非有限値・無効な深度指定・ゼロ寸法は命令全体を拒否する" );

		C.Group = "effect-material-quad-d3d11";
		C.Check( Executor.SupportsEffectMaterials(), "通常のDxLib Direct3D 11環境で材質描画能力を公開する" );
		if ( !Executor.SupportsEffectMaterials() ) return;
		// 自己確認終了後に復元する呼び出し元状態とテスト資源handle。
		const FMaterialCallerState Saved;
		const int32 Screen = MakeScreen( 128, 128, TRUE );
		const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
		const int32 Flat = MakeColorGraph( 80, 40, 20, 255 );
		const int32 Transparent = MakeColorGraph( 80, 40, 20, 0 );
		int32 HalfAlpha = -1;
		const int32 Split = MakeSplitGraph( false );
		const int32 SplitMask = MakeSplitGraph( true );
		int32 DerivedRight = -1;
		int32 DerivedMaskRight = -1;
		int32 DerivedMaskLeft = -1;
		// 既知の左右maskを作る一時画像と、その完成graph。
		const int32 MaskSoft = MakeARGB8ColorSoftImage( 8, 8 );
		int32 Mask = -1;
		if ( MaskSoft >= 0 )
		{
			for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) DrawPixelSoftImage( MaskSoft, X, Y, X < 4 ? 0 : 255, 0, 0, 255 );
			Mask = CreateGraphFromSoftImage( MaskSoft );
		}
		// 部分失敗でも作成済みgraphとsoft imageをすべて解放する。
		const auto Cleanup = [&]()
		{
			if ( Mask >= 0 ) DeleteGraph( Mask );
			if ( MaskSoft >= 0 ) DeleteSoftImage( MaskSoft );
			if ( SplitMask >= 0 ) DeleteGraph( SplitMask );
			if ( Split >= 0 ) DeleteGraph( Split );
			if ( DerivedMaskLeft >= 0 ) DeleteGraph( DerivedMaskLeft );
			if ( DerivedMaskRight >= 0 ) DeleteGraph( DerivedMaskRight );
			if ( DerivedRight >= 0 ) DeleteGraph( DerivedRight );
			if ( Transparent >= 0 ) DeleteGraph( Transparent );
			if ( HalfAlpha >= 0 ) DeleteGraph( HalfAlpha );
			if ( Flat >= 0 ) DeleteGraph( Flat );
			if ( Soft >= 0 ) DeleteSoftImage( Soft );
			if ( Screen >= 0 ) DeleteGraph( Screen );
		};
		C.Check( Screen >= 0 && Soft >= 0 && Flat >= 0 && Transparent >= 0 && Split >= 0 && SplitMask >= 0 && Mask >= 0, "描画先・readback画像・既知色・二色画像と二分割マスクを作れる" );
		if ( Screen < 0 || Soft < 0 || Flat < 0 || Transparent < 0 || Split < 0 || SplitMask < 0 || Mask < 0 ) { Cleanup(); return; }
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		{
			// 128x128画面の中央へworld描画範囲を移すlogical viewport。
			MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport );
			const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity );
		}
		SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE ); SetDrawBright( 255, 255, 255 );
		SetBackgroundColor( 0, 0, 0, 255 );
		// 描画各条件で値を変更する基準材質quad。
		FDrawMaterialQuadData Quad;
		Quad.Quad.Handle = Flat; Quad.Quad.Width = 4.0f; Quad.Quad.Height = 4.0f; Quad.DepthTest = 1; Quad.DepthWrite = 0;
		// 画面初期化からreadbackまでを行う材質描画窓口。
		const auto Render = [&]( const FDrawMaterialQuadData& Material, EDrawBlend BlendMode = EDrawBlend::Alpha, int32 BlendParameter = 255, int32 BrightValue = 255 )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); SetDrawBright( BrightValue, BrightValue, BrightValue ); ClearDrawScreen();
			FDrawBuffer Commands; FDrawContext Context( Commands ); Context.Blend( BlendMode, BlendParameter ); Context.MaterialQuad3D( Material );
			if ( !Commands.Words.empty() ) Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		// 直近readback画像の指定pixelをRGBA値で返す。
		const auto Pixel = [&]( int32 X, int32 Y )
		{
			int32 R = 0, G = 0, B = 0, A = 0; GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A ); return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		};
		// 実機結果をこのselftest sandboxのSaved配下へPNG保存する。
		const auto SaveProof = [&]( const char* Name )
		{
			const FString Directory = FString( "Saved" ) / "EffectMaterialProof";
			if ( NFileSystem::EnsureDirectory( Directory ) ) SaveDrawScreenToPNG( 0, 0, 128, 128, *( Directory / FString::FromUtf8( Name ) ) );
		};
		Quad.bDissolve = 0; Render( Quad );
		C.Check( Pixel( 64, 64 ).r > 65 && Pixel( 64, 64 ).g > 25, "消失無効では既知の元画像が四角形内へ描かれる" );
		SaveProof( "effect-material-opaque.png" );
		Quad.bDissolve = 1; Quad.MaskTexture = Mask; Quad.Dissolve = 0.0f; Render( Quad );
		C.Check( Pixel( 48, 64 ).r > 50 && Pixel( 80, 64 ).r > 50, "しきい値0ではマスク値0を含め四角形全体が残る" );
		Quad.Dissolve = 1.0f; Render( Quad );
		C.Check( Pixel( 64, 64 ).r < 8, "しきい値1では境界を含めて完全に消える" );
		SaveProof( "effect-material-dissolve-full.png" );
		Quad.Dissolve = 0.5f; Quad.EdgeWidth = 0.25f; Quad.EdgeEmission = 4.0f; Render( Quad );
		C.Check( Pixel( 48, 64 ).r < 8 && Pixel( 80, 64 ).r > 50 && Pixel( 64, 64 ).g > Pixel( 80, 64 ).g, "中間しきい値は実際の穴を作り、生存側境界の既知画素64を強く光らせる" );
		SaveProof( "effect-material-hole-edge.png" );
		Quad.EdgeWidth = 0.0f; Render( Quad );
		C.Check( Pixel( 80, 64 ).g < 60, "境界幅0では境界発光を描かない" );
		Quad.MaskTexture = Mask; Quad.EdgeWidth = 0.25f; Quad.EdgeEmission = 8.0f; Quad.Quad.Handle = Transparent; Quad.bDissolve = 1; Quad.Dissolve = 0.5f; Render( Quad );
		C.Check( Pixel( 64, 64 ).r < 8 && Pixel( 64, 64 ).g < 8, "境界発光域でも元画像alpha0の画素は復活しない" );
		Quad.Quad.Handle = Flat; Quad.bDissolve = 0; Quad.Emission = 1.0f; Render( Quad ); const uint8 EmissionOne = Pixel( 64, 64 ).r;
		Quad.Emission = 2.0f; Render( Quad );
		C.Check( Pixel( 64, 64 ).r > EmissionOne + 45, "Emission 2はEmission 1より元画像を明るくする" );
		SaveProof( "effect-material-emission.png" );
		SetBackgroundColor( 0, 0, 0, 255 );
		Quad.bDissolve = 0; Quad.Emission = 1.0f; Render( Quad, EDrawBlend::Alpha, 128 );
		C.Check( Pixel( 64, 64 ).r >= 35 && Pixel( 64, 64 ).r <= 45, "呼び出し元のblend parameter 128は材質の透明度へ反映する" );
		Render( Quad, EDrawBlend::Alpha, 255, 128 );
		C.Check( Pixel( 64, 64 ).r >= 35 && Pixel( 64, 64 ).r <= 45, "呼び出し元のDrawBright 128は材質RGBへ反映する" );
		Render( Quad, EDrawBlend::Opaque );
		C.Check( Pixel( 64, 64 ).r < 8, "対応しないOpaque合成は別の合成へ置き換えず明示的に描画を見送る" );
		Quad.MaskTexture = -1; Quad.Quad.Handle = Flat; Quad.bDissolve = 1; Quad.Dissolve = 0.5f; Quad.EdgeWidth = 0.0f; Render( Quad );
		// 中間しきい値で四角形全体の生存・消失を記録する。
		uint8 FirstNoise[4096] = {};
		// 中間しきい値で色が残った画素の総数。
		int32 NoiseAlive = 0;
		for ( int32 Y = 0; Y < 64; ++Y ) for ( int32 X = 0; X < 64; ++X ) { FirstNoise[Y * 64 + X] = Pixel( 32 + X, 32 + Y ).r > 20 ? 1 : 0; NoiseAlive += FirstNoise[Y * 64 + X]; }
		SaveProof( "effect-material-noise.png" );
		// しきい値ごとの全64×64画素を数え、端点と単調な減少を確認する。
		const auto CountNoiseAtThreshold = [&]( float32 Threshold ) { Quad.Dissolve = Threshold; Render( Quad ); int32 Alive = 0; for ( int32 Y = 0; Y < 64; ++Y ) for ( int32 X = 0; X < 64; ++X ) if ( Pixel( 32 + X, 32 + Y ).r > 20 ) ++Alive; return Alive; };
		// 全64x64領域で測るしきい値0、.25、.75、1の生存画素数。
		const int32 NoiseAt0 = CountNoiseAtThreshold( 0.0f );
		const int32 NoiseAt25 = CountNoiseAtThreshold( 0.25f );
		const int32 NoiseAt75 = CountNoiseAtThreshold( 0.75f );
		const int32 NoiseAt100 = CountNoiseAtThreshold( 1.0f );
		Quad.Dissolve = 0.5f;
		Render( Quad );
		// 同じ条件の再描画を比較するため、二回目の生存判定を保持する。
		uint8 SecondNoise[4096] = {};
		for ( int32 Y = 0; Y < 64; ++Y ) for ( int32 X = 0; X < 64; ++X ) SecondNoise[Y * 64 + X] = Pixel( 32 + X, 32 + Y ).r > 20 ? 1 : 0;
		// 同じ入力を再描画した際に変化した分類数を記録する。
		int32 NoiseDifferent = 0;
		for ( int32 Index = 0; Index < 4096; ++Index ) if ( FirstNoise[Index] != SecondNoise[Index] ) ++NoiseDifferent;
		C.Check( NoiseAt0 == 4096 && NoiseAt25 >= NoiseAlive && NoiseAlive >= NoiseAt75 && NoiseAt100 == 0 && NoiseAlive > 0 && NoiseAlive < 4096 && NoiseDifferent == 0, "組み込み模様の生存画素: 0=%d/4096 .25=%d/4096 .5=%d/4096 .75=%d/4096 1=%d/4096、.5の再描画差=%d/4096", NoiseAt0, NoiseAt25, NoiseAlive, NoiseAt75, NoiseAt100, NoiseDifferent );

		C.Group = "effect-material-uv-derived";
		Quad.Quad.Handle = Split; Quad.Quad.Width = 4.0f; Quad.Quad.Height = 4.0f; Quad.MaskTexture = -1; Quad.Emission = 1.0f; Quad.bDissolve = 0; Quad.UVScale[0] = Quad.UVScale[1] = 1.0f; Quad.UVOffset[0] = Quad.UVOffset[1] = 0.0f; Render( Quad );
		C.Check( Pixel( 48, 64 ).r > 220 && Pixel( 48, 64 ).b < 20 && Pixel( 80, 64 ).b > 220 && Pixel( 80, 64 ).r < 20, "非対称の既知画像で左赤・右青のUV基準位置を読む" );
		Quad.UVOffset[0] = 0.5f; Render( Quad );
		C.Check( Pixel( 48, 64 ).b > 220 && Pixel( 80, 64 ).r > 220, "正のUV移動0.5で既知の左右色が入れ替わる" );
		Quad.UVOffset[0] = -0.5f; Render( Quad );
		C.Check( Pixel( 48, 64 ).b > 220 && Pixel( 80, 64 ).r > 220, "負のUV移動も範囲外をwrapして既知の左右色を入れ替える" );
		Quad.UVOffset[0] = 0.0f; Quad.UVScale[0] = 2.0f; Render( Quad );
		C.Check( Pixel( 40, 64 ).r > 220 && Pixel( 56, 64 ).b > 220, "UV倍率2で画像が反復しWrap境界をまたいでも色が崩れない" );
		Quad.UVScale[0] = 1.0f;
		DerivedRight = DerivationGraph( 4, 0, 4, 8, Split );
		DerivedMaskRight = DerivationGraph( 4, 0, 4, 8, SplitMask );
		DerivedMaskLeft = DerivationGraph( 0, 0, 4, 8, SplitMask );
		C.Check( DerivedRight >= 0 && DerivedMaskRight >= 0 && DerivedMaskLeft >= 0, "左右二色画像から右半分の派生画像を作る" );
		if ( DerivedRight >= 0 && DerivedMaskRight >= 0 && DerivedMaskLeft >= 0 )
		{
			Quad.Quad.Handle = DerivedRight; Render( Quad );
			C.Check( Pixel( 48, 64 ).b > 220 && Pixel( 80, 64 ).b > 220 && Pixel( 48, 64 ).r < 20, "派生元の右半分を画像全体として青一色に表示する" );
			Quad.Quad.Handle = Flat; Quad.MaskTexture = DerivedMaskRight; Quad.bDissolve = 1; Quad.Dissolve = 0.5f; Render( Quad );
			C.Check( Pixel( 48, 64 ).r > 50 && Pixel( 80, 64 ).r > 50, "派生マスクの右半分は白一色として全体を残す" );
			Quad.MaskTexture = DerivedMaskLeft; Render( Quad );
			C.Check( Pixel( 48, 64 ).r < 8 && Pixel( 80, 64 ).r < 8, "派生マスクの左半分は黒一色として全体を消す" );
			DeleteGraph( DerivedMaskLeft ); DerivedMaskLeft = -1; DeleteGraph( DerivedMaskRight ); DerivedMaskRight = -1; DeleteGraph( DerivedRight ); DerivedRight = -1;
		}
		Quad.Quad.Handle = Flat; Quad.MaskTexture = Mask; Quad.Dissolve = 0.5f; Quad.bDissolve = 1; Quad.EdgeWidth = 0.25f; Quad.EdgeEmission = 4.0f;
		// DrawAreaとlogical viewportの条件を変えて同じquadを撮影する。
		const auto RenderCustomArea = [&]( const RECT& Area, const MATRIX& Viewport )
		{
			SetDrawScreen( Screen ); ClearDrawScreen(); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom ); SetTransformToViewport( &Viewport );
			FDrawBuffer Commands; FDrawContext Context( Commands ); Context.Blend( EDrawBlend::Alpha, 255 ); Context.MaterialQuad3D( Quad ); if ( !Commands.Words.empty() ) Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		};
		// 比較に使う通常viewport、狭いdraw area、縮小logical viewport。
		MATRIX FullViewport{}; CreateViewportMatrix( &FullViewport, 64.0f, 64.0f, 128.0f, 128.0f );
		RECT TightArea{ 48, 48, 80, 80 }; RenderCustomArea( TightArea, FullViewport );
		C.Check( Pixel( 64, 64 ).r > 50 && Pixel( 40, 64 ).r < 8 && Pixel( 88, 64 ).r < 8, "狭いDrawAreaは四角形を拡縮せず範囲だけを切り取る" );
		RECT FullArea{ 0, 0, 128, 128 }; MATRIX LogicalViewport{}; CreateViewportMatrix( &LogicalViewport, 32.0f, 32.0f, 64.0f, 64.0f ); RenderCustomArea( FullArea, LogicalViewport );
		C.Check( Pixel( 32, 32 ).r > 50 && Pixel( 64, 64 ).r < 8, "論理viewport変換を適用して位置と大きさを保つ" );
		SetDrawAreaFull(); SetTransformToViewport( &FullViewport ); Quad.EdgeWidth = 0.0f; Quad.bDissolve = 0;
		TestNativeStateRestore( C, Executor, Quad, Soft );
		C.Group = "effect-material-depth-contract";
		const int32 DepthScreen = MakeScreen( 128, 128, TRUE );
		const int32 RedGraph = MakeColorGraph( 220, 20, 10, 255 );
		const int32 BlueGraph = MakeColorGraph( 10, 20, 220, 255 );
		const bool bDepthReady = DepthScreen >= 0 && RedGraph >= 0 && BlueGraph >= 0 && SetUseGraphZBuffer( DepthScreen, TRUE ) == 0;
		C.Check( bDepthReady, "深度付きscreenと前後判定用の色画像を用意する" );
		if ( bDepthReady )
		{
			SetDrawScreen( DepthScreen ); SetDrawAreaFull(); SetTransformToViewport( &FullViewport ); SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE );
			// 指定した深度契約の2枚を近い順に記録して遮蔽結果を読む。
			const auto RenderDepthPair = [&]( int32 FirstTest, int32 FirstWrite, int32 SecondTest, int32 SecondWrite )
			{
				FDrawMaterialQuadData NearQuad = Quad; NearQuad.Quad.Handle = RedGraph; NearQuad.Quad.Pos[2] = -1.0f; NearQuad.DepthTest = FirstTest; NearQuad.DepthWrite = FirstWrite;
				FDrawMaterialQuadData FarQuad = Quad; FarQuad.Quad.Handle = BlueGraph; FarQuad.Quad.Pos[2] = 1.0f; FarQuad.DepthTest = SecondTest; FarQuad.DepthWrite = SecondWrite;
				ClearDrawScreen(); FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( EDrawBlend::Alpha ); Draw.MaterialQuad3D( NearQuad ); Draw.MaterialQuad3D( FarQuad ); Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			};
			RenderDepthPair( 1, 0, 1, 0 );
			C.Check( Pixel( 64, 64 ).b > Pixel( 64, 64 ).r, "depth test有効・write無効では後ろの図形が深度を残さず描ける" );
			RenderDepthPair( 1, 1, 1, 0 );
			C.Check( Pixel( 64, 64 ).r > Pixel( 64, 64 ).b, "depth testとwriteの両方を有効にすると手前の図形が後ろを遮る" );
			RenderDepthPair( 0, 0, 0, 0 );
			C.Check( Pixel( 64, 64 ).b > Pixel( 64, 64 ).r, "depth testとwriteを無効にすると後描画が前後関係を問わず見える" );
			RenderDepthPair( 0, 1, 1, 0 );
			C.Check( Pixel( 64, 64 ).r > Pixel( 64, 64 ).b, "testなしwriteありの手前図形は後続testあり図形を遮る" );
			SaveProof( "effect-material-depth.png" );
		}
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetTransformToViewport( &FullViewport );
		if ( BlueGraph >= 0 ) DeleteGraph( BlueGraph ); if ( RedGraph >= 0 ) DeleteGraph( RedGraph ); if ( DepthScreen >= 0 ) DeleteGraph( DepthScreen );

		C.Group = "effect-material-plain-quad-regression";
		Quad.Quad.Handle = Flat; Quad.Quad.Pos[0] = -1.5f; Quad.Quad.Pos[2] = 0.0f; Quad.Quad.Width = 2.0f; Quad.Quad.Height = 2.0f; Quad.Emission = 1.0f; Quad.bDissolve = 0; Quad.EdgeWidth = 0.0f; Quad.Quad.Color = { 255, 255, 255, 255 };
		// 右側へ単独描画する既存Quad3Dの比較対象。
		FDrawQuad3DData PlainQuad = Quad.Quad; PlainQuad.Pos[0] = 1.5f;
		ClearDrawScreen();
		{
			FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( EDrawBlend::Alpha ); Draw.MaterialQuad3D( Quad ); Draw.Quad3D( PlainQuad ); Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		}
		// 材質描画の左側と通常quadの右側の単独・連続描画画素。
		const FColor32 MaterialPixel = Pixel( 48, 64 );
		const FColor32 PlainAfterMaterial = Pixel( 80, 64 );
		// 通常quad領域のRGB全画素を、連続・単独描画間で比較する。
		uint8 PlainRegionAfterMaterial[32 * 32 * 3] = {};
		for ( int32 Y = 0; Y < 32; ++Y ) for ( int32 X = 0; X < 32; ++X ) { const FColor32 Value = Pixel( 72 + X, 48 + Y ); const int32 Offset = ( Y * 32 + X ) * 3; PlainRegionAfterMaterial[Offset] = Value.r; PlainRegionAfterMaterial[Offset + 1] = Value.g; PlainRegionAfterMaterial[Offset + 2] = Value.b; }
		SaveProof( "effect-material-then-plain-quad.png" );
		ClearDrawScreen();
		{
			FDrawBuffer Commands; FDrawContext Draw( Commands ); Draw.Blend( EDrawBlend::Alpha ); Draw.Quad3D( PlainQuad ); Executor.Execute( FDrawSpan { Commands.Words.data(), static_cast<int32>( Commands.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		}
		const FColor32 PlainOnly = Pixel( 80, 64 );
		uint8 PlainRegionOnly[32 * 32 * 3] = {};
		for ( int32 Y = 0; Y < 32; ++Y ) for ( int32 X = 0; X < 32; ++X ) { const FColor32 Value = Pixel( 72 + X, 48 + Y ); const int32 Offset = ( Y * 32 + X ) * 3; PlainRegionOnly[Offset] = Value.r; PlainRegionOnly[Offset + 1] = Value.g; PlainRegionOnly[Offset + 2] = Value.b; }
		C.Check( MaterialPixel.r > 50 && ( PlainOnly.r > 10 || PlainOnly.g > 10 || PlainOnly.b > 10 ) && PlainAfterMaterial.r == PlainOnly.r && PlainAfterMaterial.g == PlainOnly.g && PlainAfterMaterial.b == PlainOnly.b && std::memcmp( PlainRegionAfterMaterial, PlainRegionOnly, sizeof( PlainRegionAfterMaterial ) ) == 0, "材質描画後の通常Quad3Dは既知画像の色を画素領域全体で単独描画時と一致させる" );

		C.Group = "effect-material-malformed-command";
		SetBackgroundColor( 0, 0, 0, 255 );
		ClearDrawScreen();
		// payloadが短い偽造commandとdepth値が範囲外の完全payload。
		const uint32 ShortCommand[2] = { static_cast<uint32>( EDrawCommand::MaterialQuad3D ) | ( 2u << 16 ), 0u };
		Executor.Execute( FDrawSpan { ShortCommand, 2 } );
		FDrawMaterialQuadData InvalidGpuData = Quad; InvalidGpuData.DepthTest = 7;
		uint32 InvalidCommand[1 + sizeof( FDrawMaterialQuadData ) / 4] = {};
		InvalidCommand[0] = static_cast<uint32>( EDrawCommand::MaterialQuad3D ) | ( static_cast<uint32>( 1 + sizeof( FDrawMaterialQuadData ) / 4 ) << 16 );
		std::memcpy( InvalidCommand + 1, &InvalidGpuData, sizeof( InvalidGpuData ) );
		Executor.Execute( FDrawSpan { InvalidCommand, static_cast<int32>( 1 + sizeof( FDrawMaterialQuadData ) / 4 ) } );
		InvalidGpuData.DepthTest = 0; InvalidGpuData.DepthWrite = 2;
		std::memcpy( InvalidCommand + 1, &InvalidGpuData, sizeof( InvalidGpuData ) );
		Executor.Execute( FDrawSpan { InvalidCommand, static_cast<int32>( 1 + sizeof( FDrawMaterialQuadData ) / 4 ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		C.Check( Pixel( 64, 64 ).r < 8 && Pixel( 64, 64 ).g < 8, "短い命令と偽造depth値を描画せず安全に読み飛ばす" );

		C.Group = "effect-material-resource-lifecycle";
		// 同じrendererの明示解放・再初期化を反復した全成否。
		bool bRepeatedLifetime = true;
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			FEffectMaterialRenderer Renderer;
			bRepeatedLifetime = bRepeatedLifetime && Renderer.Initialize() && Renderer.IsAvailable();
			Renderer.Deinitialize();
			bRepeatedLifetime = bRepeatedLifetime && Renderer.Initialize() && Renderer.IsAvailable();
			Renderer.Deinitialize();
		}
		C.Check( bRepeatedLifetime, "native材質rendererのGPU資源を8回作成・解放・再作成できる" );
		Quad.Quad.Pos[0] = 0.0f; Quad.Quad.Pos[1] = 0.0f; Quad.Quad.Pos[2] = 0.0f; Quad.Quad.Width = 4.0f; Quad.Quad.Height = 4.0f; Quad.Quad.Color = { 255, 255, 255, 255 };
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetTransformToViewport( &FullViewport ); SetDrawBright( 255, 255, 255 );
		Quad.Quad.Pos[0] = 0.0f; Quad.Quad.Pos[1] = 0.0f; Quad.Quad.Pos[2] = 0.0f; Quad.Quad.Width = 4.0f; Quad.Quad.Height = 4.0f;
		SetBackgroundColor( 40, 30, 20, 255 );
		HalfAlpha = MakeColorGraph( 80, 40, 20, 128 );
		C.Check( HalfAlpha >= 0, "加算とalphaのpixel期待値用に半透明画像を作る" );
		if ( HalfAlpha >= 0 )
		{
			Quad.Quad.Handle = HalfAlpha; Quad.Emission = 1.0f; Render( Quad, EDrawBlend::Add ); const uint8 AddPixel = Pixel( 64, 64 ).r; Render( Quad, EDrawBlend::Alpha ); const uint8 AlphaPixel = Pixel( 64, 64 ).r;
			C.Check( AddPixel >= 75 && AddPixel <= 85 && AlphaPixel >= 55 && AlphaPixel <= 65, "加算とalphaは半透明の既知色・背景から独立した合成値を作る" );
			DeleteGraph( HalfAlpha ); HalfAlpha = -1;
		}
		SetDrawScreen( Saved.Screen );
		Cleanup();
	}
}

// 登録済みworld selftestへ材質描画の実機確認を提供する。
void NWorldTest::RunEffectMaterialRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor )
{
	TestMaterialQuad( C, Executor );
}

namespace NWorldTest
{
	// 共通sentinel資源で材質帯のnative state復元と実画素を検査する。
	void RunEffectMaterialRibbonNativeStateRestore( NAssetTest::FChecker& C, IDrawExecutor& Executor, const FDrawMaterialQuadData& SentinelQuad, int32 Soft, const std::function<void()>& DrawCallback )
	{
		TestNativeStateRestore( C, Executor, SentinelQuad, Soft, DrawCallback );
	}
}
