// SPDX-License-Identifier: NOASSERTION
#include "EffectMaterialRenderer.h"

#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibRibbonMesh.h"
#include "Library/Math/Matrix4x4.h"

#pragma comment(lib, "d3dcompiler")

namespace
{
	DEFINE_LOG_CATEGORY_STATIC( LogEffectMaterial, Log );

	// 四角形一枚の頂点位置と画像座標を渡す値。
	struct FVertex
	{
		// 四角形の頂点位置をworld空間で渡す。
		float32 Position[3];
		// 画像を読む論理UVを渡す。
		float32 UV[2];
		// 帯で補間する元点のRGBA。四角形では白を使う。
		float32 Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		// 帯で補間する消失値と自己発光倍率。
		float32 Dissolve = 0.0f;
		float32 Emission = 0.0f;
		// 帯で補間する未折返しの画像UV移動量。
		float32 UVOffset[2] = {};
		// 帯の各点から補間する未折返しnoise移動量。
		float32 DistortionOffset[2] = {};
		// 帯の各点から補間するUVずれの強さ。
		float32 DistortionStrength = 0.0f;
	};

	// 頂点を画面へ移す変換行列を渡す値。
	struct FVertexConstants
	{
		// 頂点をclip空間へ移す4x4行列。
		float32 Wvp[16];
	};

	// 材質の画像座標・消失・発光設定を渡す値。
	struct FPixelConstants
	{
		// 元画像へ使う倍率とUV移動。
		float32 UvScaleOffset[4];
		// 消失しきい値、適用flag、mask倍率、自己発光倍率。
		float32 Values[4];
		// 元画像へ掛けるRGBA tint。
		float32 Tint[4];
		// 境界色と境界幅。
		float32 Edge[4];
		// 呼び出し元のDrawBrightとblend parameter。
		float32 BrightBlend[4];
		// mask有無とalpha合成倍率。
		float32 Options[4];
		// 元画像のgraph領域をbase textureへ写す情報。
		float32 SourceRect[4];
		// mask graph領域をbase textureへ写す情報。
		float32 MaskRect[4];
		// 元画像の端から隣の領域へ漏れない半texel幅。
		float32 SourceInset[4];
		// maskの端から隣の領域へ漏れない半texel幅。
		float32 MaskInset[4];
		// ゆがみ画像へ掛ける倍率と折返し済み移動量。
		float32 DistortionScaleOffset[4];
		// ゆがみ幅と適用flag。
		float32 DistortionValues[4];
		// ゆがみ画像のbase領域と半texel幅。
		float32 DistortionRect[4];
		float32 DistortionInset[4];
	};

	// 組み込み消失模様は4・16・64倍の周波数を重ね、画面全体に模様を散らす。
	const char* ShaderSource = R"hlsl(
cbuffer VertexConstants : register(b0) { row_major float4x4 Wvp; };
struct VertexInput { float3 Position : POSITION; float2 Uv : TEXCOORD0; float4 Color : COLOR0; float Dissolve : TEXCOORD1; float Emission : TEXCOORD2; float2 UVOffset : TEXCOORD3; float2 DistortionOffset : TEXCOORD4; float DistortionStrength : TEXCOORD5; };
struct VertexOutput { float4 Position : SV_POSITION; float2 Uv : TEXCOORD0; float4 Color : COLOR0; float Dissolve : TEXCOORD1; float Emission : TEXCOORD2; float2 UVOffset : TEXCOORD3; float2 DistortionOffset : TEXCOORD4; float DistortionStrength : TEXCOORD5; };
VertexOutput VSMain(VertexInput Input) { VertexOutput Output; Output.Position = mul(float4(Input.Position, 1.0), Wvp); Output.Uv = Input.Uv; Output.Color = Input.Color; Output.Dissolve = Input.Dissolve; Output.Emission = Input.Emission; Output.UVOffset = Input.UVOffset; Output.DistortionOffset = Input.DistortionOffset; Output.DistortionStrength = Input.DistortionStrength; return Output; }
Texture2D SourceTexture : register(t0);
Texture2D MaskTexture : register(t1);
Texture2D DistortionTexture : register(t2);
SamplerState WrapSampler : register(s0);
cbuffer PixelConstants : register(b0)
{
    float4 UvScaleOffset;
    float4 Values;
    float4 Tint;
    float4 Edge;
    float4 BrightBlend;
    float4 Options;
    float4 SourceRect;
    float4 MaskRect;
    float4 SourceInset;
    float4 MaskInset;
    float4 DistortionScaleOffset;
    float4 DistortionValues;
    float4 DistortionRect;
    float4 DistortionInset;
};
float Hash(float2 P) { return frac(sin(dot(P, float2(127.1, 311.7))) * 43758.5453); }
float Noise(float2 P) { float2 I = floor(P); float2 F = frac(P); F = F * F * (3.0 - 2.0 * F); return lerp(lerp(Hash(I), Hash(I + float2(1, 0)), F.x), lerp(Hash(I + float2(0, 1)), Hash(I + 1.0), F.x), F.y); }
float4 PSMain(VertexOutput Input) : SV_TARGET
{
    float IsRibbon = Options.w;
    float2 SourceOffset = IsRibbon > 0.5 ? Input.UVOffset : UvScaleOffset.zw;
    float2 SourceLogicalUv = Input.Uv * UvScaleOffset.xy + SourceOffset;
    if (DistortionValues.y > 0.5)
    {
        float2 PointDistortionOffset = IsRibbon > 0.5 ? Input.DistortionOffset : DistortionScaleOffset.zw;
        float PointDistortionStrength = IsRibbon > 0.5 ? Input.DistortionStrength : DistortionValues.x;
        float2 DistortionLogicalUv = frac(Input.Uv * DistortionScaleOffset.xy + PointDistortionOffset);
        DistortionLogicalUv = clamp(DistortionLogicalUv, DistortionInset.xy, 1.0 - DistortionInset.xy);
        float2 DistortionUv = DistortionRect.zw + DistortionLogicalUv * DistortionRect.xy;
        float2 Distortion = DistortionTexture.Sample(WrapSampler, DistortionUv).rg * 2.0 - 1.0;
        SourceLogicalUv += Distortion * PointDistortionStrength;
    }
    SourceLogicalUv = frac(SourceLogicalUv);
    SourceLogicalUv = clamp(SourceLogicalUv, SourceInset.xy, 1.0 - SourceInset.xy);
    float2 BaseUv = SourceRect.zw + SourceLogicalUv * SourceRect.xy;
    float2 MaskLogicalUv = frac(Input.Uv * Values.z);
    MaskLogicalUv = clamp(MaskLogicalUv, MaskInset.xy, 1.0 - MaskInset.xy);
    float2 MaskUv = MaskRect.zw + MaskLogicalUv * MaskRect.xy;
    float Mask = Options.x > 0.5 ? MaskTexture.Sample(WrapSampler, MaskUv).r : saturate(Noise(MaskUv * 4.0) * 0.5 + Noise(MaskUv * 16.0) * 0.35 + Noise(MaskUv * 64.0) * 0.15);
    float Dissolve = IsRibbon > 0.5 ? Input.Dissolve : Values.x;
    if (Values.y > 0.5) { if (Dissolve >= 1.0) discard; clip(Mask - Dissolve); }
    float EdgeAmount = 0.0;
    if (Values.y > 0.5 && Dissolve > 0.0 && Dissolve < 1.0 && Edge.a > 0.0) EdgeAmount = 1.0 - smoothstep(0.0, Edge.a, Mask - Dissolve);
    float4 Source = SourceTexture.Sample(WrapSampler, BaseUv);
    float4 PixelTint = IsRibbon > 0.5 ? Input.Color : Tint;
    float Emission = IsRibbon > 0.5 ? Input.Emission : Values.w;
    if (Source.a * PixelTint.a * Options.y <= 0.0) discard;
    float3 Emissive = Source.rgb * PixelTint.rgb * Emission + Edge.rgb * EdgeAmount;
    float3 Rgb = min(Emissive * BrightBlend.rgb, 1.0);
    float Alpha = Source.a * PixelTint.a * Options.y;
    return float4(Rgb, Alpha);
}
)hlsl";

	// 3成分の配列をDxLibの位置・方向型へ変換する。
	VECTOR ArrayVector( const float32 ( &Values )[3] ) { return VGet( Values[0], Values[1], Values[2] ); }

	// HLSL entryを指定版へcompileし、失敗時は診断を記録する。
	bool CompileShader( ID3D11Device* Device, const char* Entry, const char* Target, ID3DBlob** Blob )
	{
		// compilerが返す失敗理由。診断が無い場合はnullのまま。
		ID3DBlob* Errors = nullptr;
		// 指定entryとprofileのcompile結果。
		const HRESULT Result = D3DCompile( ShaderSource, NCString::Length( ShaderSource ), "EffectMaterial", nullptr, nullptr, Entry, Target, D3DCOMPILE_ENABLE_STRICTNESS, 0, Blob, &Errors );
		if ( FAILED( Result ) && Errors != nullptr ) LOG( LogEffectMaterial, Error, "材質シェーダーのコンパイルに失敗しました: %s", static_cast<const char*>( Errors->GetBufferPointer() ) );
		if ( Errors != nullptr ) Errors->Release();
		return SUCCEEDED( Result ) && Device != nullptr;
	}

	// 一つのshader段階とそのclass instanceを保存・復元する値。
	struct FSavedShader
	{
		// getterで保持し、同じstageへ戻すshader参照。
		ID3D11DeviceChild* Shader = nullptr;
		// shaderが参照するclass instance一式。
		ID3D11ClassInstance* Instances[256] = {};
		// getterへ渡す上限値と、取得後に解放する実数。
		UINT Count = 256;

		// 頂点shader段階の現在値を参照付きで保存する。
		void SaveVertex( ID3D11DeviceContext* Context ) { Context->VSGetShader( reinterpret_cast<ID3D11VertexShader**>( &Shader ), Instances, &Count ); }
		// pixel shader段階の現在値を参照付きで保存する。
		void SavePixel( ID3D11DeviceContext* Context ) { Context->PSGetShader( reinterpret_cast<ID3D11PixelShader**>( &Shader ), Instances, &Count ); }
		// geometry shader段階の現在値を参照付きで保存する。
		void SaveGeometry( ID3D11DeviceContext* Context ) { Context->GSGetShader( reinterpret_cast<ID3D11GeometryShader**>( &Shader ), Instances, &Count ); }
		// hull shader段階の現在値を参照付きで保存する。
		void SaveHull( ID3D11DeviceContext* Context ) { Context->HSGetShader( reinterpret_cast<ID3D11HullShader**>( &Shader ), Instances, &Count ); }
		// domain shader段階の現在値を参照付きで保存する。
		void SaveDomain( ID3D11DeviceContext* Context ) { Context->DSGetShader( reinterpret_cast<ID3D11DomainShader**>( &Shader ), Instances, &Count ); }
		// 保存した頂点shaderとclass instanceを復元する。
		void RestoreVertex( ID3D11DeviceContext* Context ) { Context->VSSetShader( static_cast<ID3D11VertexShader*>( Shader ), Instances, Count ); }
		// 保存したpixel shaderとclass instanceを復元する。
		void RestorePixel( ID3D11DeviceContext* Context ) { Context->PSSetShader( static_cast<ID3D11PixelShader*>( Shader ), Instances, Count ); }
		// 保存したgeometry shaderとclass instanceを復元する。
		void RestoreGeometry( ID3D11DeviceContext* Context ) { Context->GSSetShader( static_cast<ID3D11GeometryShader*>( Shader ), Instances, Count ); }
		// 保存したhull shaderとclass instanceを復元する。
		void RestoreHull( ID3D11DeviceContext* Context ) { Context->HSSetShader( static_cast<ID3D11HullShader*>( Shader ), Instances, Count ); }
		// 保存したdomain shaderとclass instanceを復元する。
		void RestoreDomain( ID3D11DeviceContext* Context ) { Context->DSSetShader( static_cast<ID3D11DomainShader*>( Shader ), Instances, Count ); }

		void Clear()
		{
			// getterが増やしたshaderとclass instanceの参照を解放する。
			if ( Shader != nullptr ) Shader->Release();
			for ( UINT Index = 0; Index < Count; ++Index ) if ( Instances[Index] != nullptr ) Instances[Index]->Release();
			Shader = nullptr;
			Count = 0;
		}
	};

	// 材質描画が触るnative context状態を一時保存して戻す値。
	struct FNativeState
	{
		// input形式と頂点bufferを描画前へ戻す参照と範囲。
		ID3D11InputLayout* InputLayout = nullptr;
		ID3D11Buffer* VertexBuffer = nullptr;
		// 頂点bufferの一要素サイズと開始位置。
		UINT VertexStride = 0;
		UINT VertexOffset = 0;
		// 元の頂点列の読み方。
		D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		// shader段階slot 0の定数buffer。
		ID3D11Buffer* VertexConstants = nullptr;
		ID3D11Buffer* PixelConstants = nullptr;
		// pixel shader slot 0/1の画像参照とslot 0のsampler。
		ID3D11ShaderResourceView* Source = nullptr;
		ID3D11ShaderResourceView* Mask = nullptr;
		ID3D11ShaderResourceView* Distortion = nullptr;
		ID3D11SamplerState* Sampler = nullptr;
		// 色合成、深度比較、stencil参照、rasterizerの元状態。
		ID3D11BlendState* Blend = nullptr;
		ID3D11DepthStencilState* Depth = nullptr;
		UINT StencilRef = 0;
		ID3D11RasterizerState* Raster = nullptr;
		// blendへ渡す4成分係数とsample mask。
		FLOAT BlendFactor[4] = {};
		UINT SampleMask = 0;
		// 各shader段階とclass instanceの参照を保存する。
		FSavedShader Vs, Ps, Gs, Hs, Ds;

		// native contextから描画で触る状態を取得する。
		void Save( ID3D11DeviceContext* Context )
		{
			Context->IAGetInputLayout( &InputLayout );
			Context->IAGetVertexBuffers( 0, 1, &VertexBuffer, &VertexStride, &VertexOffset );
			Context->IAGetPrimitiveTopology( &Topology );
			Context->VSGetConstantBuffers( 0, 1, &VertexConstants );
			Context->PSGetConstantBuffers( 0, 1, &PixelConstants );
			Context->PSGetShaderResources( 0, 1, &Source );
			Context->PSGetShaderResources( 1, 1, &Mask );
			Context->PSGetShaderResources( 2, 1, &Distortion );
			Context->PSGetSamplers( 0, 1, &Sampler );
			Context->OMGetBlendState( &Blend, BlendFactor, &SampleMask );
			Context->OMGetDepthStencilState( &Depth, &StencilRef );
			Context->RSGetState( &Raster );
			Vs.SaveVertex( Context ); Ps.SavePixel( Context ); Gs.SaveGeometry( Context ); Hs.SaveHull( Context ); Ds.SaveDomain( Context );
		}

		// 取得した状態をcontextへ戻し、getterの参照を解放する。
		void Restore( ID3D11DeviceContext* Context )
		{
			Context->IASetInputLayout( InputLayout );
			Context->IASetVertexBuffers( 0, 1, &VertexBuffer, &VertexStride, &VertexOffset );
			Context->IASetPrimitiveTopology( Topology );
			Context->VSSetConstantBuffers( 0, 1, &VertexConstants );
			Context->PSSetConstantBuffers( 0, 1, &PixelConstants );
			Context->PSSetShaderResources( 0, 1, &Source );
			Context->PSSetShaderResources( 1, 1, &Mask );
			Context->PSSetShaderResources( 2, 1, &Distortion );
			Context->PSSetSamplers( 0, 1, &Sampler );
			Context->OMSetBlendState( Blend, BlendFactor, SampleMask );
			Context->OMSetDepthStencilState( Depth, StencilRef );
			Context->RSSetState( Raster );
			Vs.RestoreVertex( Context ); Ps.RestorePixel( Context ); Gs.RestoreGeometry( Context ); Hs.RestoreHull( Context ); Ds.RestoreDomain( Context );
			Vs.Clear(); Ps.Clear(); Gs.Clear(); Hs.Clear(); Ds.Clear();
			if ( InputLayout != nullptr ) InputLayout->Release();
			if ( VertexBuffer != nullptr ) VertexBuffer->Release();
			if ( VertexConstants != nullptr ) VertexConstants->Release();
			if ( PixelConstants != nullptr ) PixelConstants->Release();
			if ( Source != nullptr ) Source->Release();
			if ( Mask != nullptr ) Mask->Release();
			if ( Distortion != nullptr ) Distortion->Release();
			if ( Sampler != nullptr ) Sampler->Release();
			if ( Blend != nullptr ) Blend->Release();
			if ( Depth != nullptr ) Depth->Release();
			if ( Raster != nullptr ) Raster->Release();
		}
	};
}

// DxLibのDirect3D 11資源と描画処理を所有する実装本体。
struct FEffectMaterialRenderer::FImpl
{
	// 初期化時に参照したdeviceとimmediate context。
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;
	// 材質quadを描くshaderとinput layout。
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* Layout = nullptr;
	// 頂点とVS/PS定数を送るdynamic buffer。
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* VertexConstants = nullptr;
	ID3D11Buffer* PixelConstants = nullptr;
	// 元samplerと分離したwrap sampler。
	ID3D11SamplerState* Sampler = nullptr;
	// この命令が対応するalphaとaddのblend state。
	ID3D11BlendState* AlphaBlend = nullptr;
	ID3D11BlendState* AddBlend = nullptr;
	// depth test/writeの全組み合わせを選ぶstate。
	ID3D11DepthStencilState* DepthStates[4] = {};
	// 現caller raster設定を保った両面描画用state。
	ID3D11RasterizerState* Raster = nullptr;
	// raster stateを再利用するために保存する直前の設定値。
	D3D11_RASTERIZER_DESC RasterDescCache{};
	// raster cacheに有効な設定値があるか。
	bool bRasterDescValid = false;
	// 元画像とmask画像のSRV cache。
	ID3D11ShaderResourceView* SourceView = nullptr;
	ID3D11ShaderResourceView* MaskView = nullptr;
	ID3D11ShaderResourceView* DistortionView = nullptr;
	// SRVが参照する画像のgraph handle。
	int32 SourceHandle = -2;
	int32 MaskHandle = -2;
	int32 DistortionHandle = -2;
	// handle再利用後の別textureを見分けるidentity pointer。
	const void* SourceTextureIdentity = nullptr;
	const void* MaskTextureIdentity = nullptr;
	const void* DistortionTextureIdentity = nullptr;
	// 失敗、未対応blend、状態不一致、不正payloadの警告を一度に抑える。
	bool bFailedLogged = false;
	bool bUnsupportedBlendLogged = false;
	bool bStateMismatchLogged = false;
	bool bInvalidDataLogged = false;

	// owner破棄時にもGPU参照を残さない。
	~FImpl() { Deinitialize(); }

	// DxLib終了前にdevice依存資源と画像cacheをすべて解放する。
	void Deinitialize()
	{
		for ( ID3D11DepthStencilState*& State : DepthStates ) if ( State != nullptr ) State->Release();
		if ( Raster != nullptr ) Raster->Release();
		if ( AlphaBlend != nullptr ) AlphaBlend->Release();
		if ( AddBlend != nullptr ) AddBlend->Release();
		if ( Sampler != nullptr ) Sampler->Release();
		if ( PixelConstants != nullptr ) PixelConstants->Release();
		if ( VertexConstants != nullptr ) VertexConstants->Release();
		if ( VertexBuffer != nullptr ) VertexBuffer->Release();
		if ( Layout != nullptr ) Layout->Release();
		if ( PixelShader != nullptr ) PixelShader->Release();
		if ( VertexShader != nullptr ) VertexShader->Release();
		if ( SourceView != nullptr ) SourceView->Release();
		if ( MaskView != nullptr ) MaskView->Release();
		if ( DistortionView != nullptr ) DistortionView->Release();
		if ( Context != nullptr ) Context->Release();
		if ( Device != nullptr ) Device->Release();
		DepthStates[0] = DepthStates[1] = DepthStates[2] = DepthStates[3] = nullptr;
		Raster = nullptr; bRasterDescValid = false; AlphaBlend = nullptr; AddBlend = nullptr; Sampler = nullptr; PixelConstants = nullptr; VertexConstants = nullptr; VertexBuffer = nullptr; Layout = nullptr; PixelShader = nullptr; VertexShader = nullptr; SourceView = nullptr; MaskView = nullptr; DistortionView = nullptr; Context = nullptr; Device = nullptr;
		SourceHandle = MaskHandle = DistortionHandle = -2; SourceTextureIdentity = MaskTextureIdentity = DistortionTextureIdentity = nullptr;
	}

	// 現在のDxLib device/context向けshaderと固定資源を作る。失敗時はfalse。
	bool Initialize()
	{
		// DxLibが公開する現在のD3D11 device。
		ID3D11Device* NewDevice = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
		// DxLibが公開する現在のimmediate context。
		ID3D11DeviceContext* NewContext = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
		if ( NewDevice == nullptr || NewContext == nullptr )
		{
			Deinitialize();
			if ( !bFailedLogged ) { LOG( LogEffectMaterial, Warning, "材質描画に必要なDirect3D 11を使用できません" ); bFailedLogged = true; }
			return false;
		}
		if ( Device == NewDevice && Context == NewContext && VertexShader != nullptr ) return true;
		Deinitialize();
		Device = NewDevice; Device->AddRef();
		Context = NewContext; Context->AddRef();
		// VS/PS bytecode。VSはinput layoutの定義にも使う。
		ID3DBlob* VsBlob = nullptr;
		ID3DBlob* PsBlob = nullptr;
		if ( !CompileShader( Device, "VSMain", "vs_4_0", &VsBlob ) || !CompileShader( Device, "PSMain", "ps_4_0", &PsBlob ) ) goto Failed;
		if ( FAILED( Device->CreateVertexShader( VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), nullptr, &VertexShader ) ) || FAILED( Device->CreatePixelShader( PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), nullptr, &PixelShader ) ) ) goto Failed;
		{
			// 頂点位置、色、材質値、source移動、noise移動と強さをshaderへ対応付ける。
			const D3D11_INPUT_ELEMENT_DESC Elements[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
			if ( FAILED( Device->CreateInputLayout( Elements, 8, VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout ) ) ) goto Failed;
		}
		if ( VsBlob != nullptr ) { VsBlob->Release(); VsBlob = nullptr; }
		if ( PsBlob != nullptr ) { PsBlob->Release(); PsBlob = nullptr; }
		{
			// 六頂点とVS/PS定数をCPUから更新できるbufferとして作る。
			D3D11_BUFFER_DESC Desc{}; Desc.ByteWidth = sizeof( FVertex ) * ( FDrawRibbonData::MaxPoints - 1 ) * 6; Desc.Usage = D3D11_USAGE_DYNAMIC; Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if ( FAILED( Device->CreateBuffer( &Desc, nullptr, &VertexBuffer ) ) ) goto Failed;
			Desc.ByteWidth = sizeof( FVertexConstants ); Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if ( FAILED( Device->CreateBuffer( &Desc, nullptr, &VertexConstants ) ) ) goto Failed;
			Desc.ByteWidth = sizeof( FPixelConstants );
			if ( FAILED( Device->CreateBuffer( &Desc, nullptr, &PixelConstants ) ) ) goto Failed;
		}
		{
			// 他のsampler状態に触れない専用wrap sampler。
			D3D11_SAMPLER_DESC Desc{}; Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; Desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; Desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; Desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; Desc.MinLOD = 0.0f; Desc.MaxLOD = 0.0f;
			if ( FAILED( Device->CreateSamplerState( &Desc, &Sampler ) ) ) goto Failed;
		}
		{
			// 公開契約にあるalphaとaddの2種類のblend state。
			D3D11_BLEND_DESC Desc{}; Desc.RenderTarget[0].BlendEnable = TRUE; Desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; Desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; Desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; Desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; Desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; Desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; Desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			if ( FAILED( Device->CreateBlendState( &Desc, &AlphaBlend ) ) ) goto Failed;
			Desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
			if ( FAILED( Device->CreateBlendState( &Desc, &AddBlend ) ) ) goto Failed;
		}
		// bit0をdepth test、bit1をdepth writeとして全4状態を準備する。
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			D3D11_DEPTH_STENCIL_DESC Desc{}; Desc.DepthEnable = ( Index & 3 ) != 0; Desc.DepthWriteMask = ( Index & 2 ) != 0 ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; Desc.DepthFunc = ( Index & 1 ) != 0 ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_ALWAYS;
			if ( FAILED( Device->CreateDepthStencilState( &Desc, &DepthStates[Index] ) ) ) goto Failed;
		}
		return true;

Failed:
		if ( VsBlob != nullptr ) VsBlob->Release();
		if ( PsBlob != nullptr ) PsBlob->Release();
		Deinitialize();
		if ( !bFailedLogged ) { LOG( LogEffectMaterial, Error, "材質描画用のDirect3D 11資源を作成できません" ); bFailedLogged = true; }
		return false;
	}

	// graph handleとtexture identityを照合し、対応するSRVを返す。
	ID3D11ShaderResourceView* GetTexture( int32 Handle, ID3D11ShaderResourceView*& Cached, int32& CachedHandle, const void*& CachedTexture )
	{
		if ( Handle < 0 ) return nullptr;
		// 削除後に同じhandleが別画像へ再利用されたかを判別する。
		const void* Texture = GetGraphID3D11Texture2D( Handle );
		if ( CachedHandle == Handle && CachedTexture == Texture && Cached != nullptr ) return Cached;
		if ( Cached != nullptr ) Cached->Release();
		Cached = nullptr; CachedHandle = Handle; CachedTexture = Texture;
		if ( Texture == nullptr || FAILED( Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( Texture ) ), nullptr, &Cached ) ) ) return nullptr;
		return Cached;
	}

	// graph領域と半texel内側をbase texture全体に対するUVへ写す。
	bool GetGraphUvMapping( int32 Handle, float32* Rect, float32* Inset )
	{
		// graphが参照するbase texture。
		const void* TexturePointer = GetGraphID3D11Texture2D( Handle );
		if ( TexturePointer == nullptr ) return false;
		// base texture全体の画素寸法。
		D3D11_TEXTURE2D_DESC TextureDesc{};
		static_cast<ID3D11Texture2D*>( const_cast<void*>( TexturePointer ) )->GetDesc( &TextureDesc );
		if ( TextureDesc.Width == 0 || TextureDesc.Height == 0 ) return false;
		// graphが使うbase内矩形。通常画像は原点から画像全体を使う。
		int32 X = 0, Y = 0, Width = 0, Height = 0;
		if ( GetGraphUseBaseGraphArea( Handle, &X, &Y, &Width, &Height ) != 0 || Width <= 0 || Height <= 0 )
		{
			if ( GetGraphSize( Handle, &Width, &Height ) != 0 || Width <= 0 || Height <= 0 ) return false;
			X = 0; Y = 0;
		}
		if ( X < 0 || Y < 0 || static_cast<uint64>( X ) + static_cast<uint64>( Width ) > TextureDesc.Width || static_cast<uint64>( Y ) + static_cast<uint64>( Height ) > TextureDesc.Height ) return false;
		Rect[0] = static_cast<float32>( Width ) / TextureDesc.Width; Rect[1] = static_cast<float32>( Height ) / TextureDesc.Height; Rect[2] = static_cast<float32>( X ) / TextureDesc.Width; Rect[3] = static_cast<float32>( Y ) / TextureDesc.Height;
		Inset[0] = 0.5f / Width; Inset[1] = 0.5f / Height;
		return true;
	}

	// 検証済み材質情報をnative D3D11で一枚描く。無効値や未対応状態ではfalse。
	bool Draw( const FDrawMaterialQuadData& Data )
	{
		return Draw( Data, nullptr );
	}

	// ゆがみ画像の追加値を検証して、共有材質描画経路へ渡す。
	bool DrawDistorted( const FDrawDistortedMaterialQuadData& Data )
	{
		if ( !NDrawDistortedMaterialQuad::IsValid( Data ) ) return false;
		return Draw( Data.Material, &Data );
	}

	// 検証済み材質設定と任意のゆがみ画像を同じnative描画経路で実行する。
	bool Draw( const FDrawMaterialQuadData& Data, const FDrawDistortedMaterialQuadData* DistortionData )
	{
		// 既存quadの位置、向き、寸法、元画像情報。
		const FDrawQuad3DData& Quad = Data.Quad;
		if ( ( Data.DepthTest != 0 && Data.DepthTest != 1 ) || ( Data.DepthWrite != 0 && Data.DepthWrite != 1 ) || Quad.Handle < 0 || Data.MaskTexture < -1 || !NMath::IsFinite( Quad.Width ) || !NMath::IsFinite( Quad.Height ) || Quad.Width <= 0.0f || Quad.Height <= 0.0f || !NMath::IsFinite( Data.UVScale[0] ) || !NMath::IsFinite( Data.UVScale[1] ) || !NMath::IsFinite( Data.UVOffset[0] ) || !NMath::IsFinite( Data.UVOffset[1] ) || !NMath::IsFinite( Data.MaskScale ) || Data.MaskScale <= 0.0f || !NMath::IsFinite( Data.Dissolve ) || Data.Dissolve < 0.0f || Data.Dissolve > 1.0f || !NMath::IsFinite( Data.Emission ) || Data.Emission < 0.0f || !NMath::IsFinite( Data.EdgeWidth ) || Data.EdgeWidth < 0.0f || !NMath::IsFinite( Data.EdgeEmission ) || Data.EdgeEmission < 0.0f || ( Data.bDissolve != 0 && Data.bDissolve != 1 ) )
		{
			if ( !bInvalidDataLogged ) { LOG( LogEffectMaterial, Warning, "不正な材質付き四角形を描画せずに読み飛ばしました" ); bInvalidDataLogged = true; }
			return false;
		}
		if ( !Initialize() ) return false;
		// 元画像と任意maskのshader参照を取得する。
		const ID3D11ShaderResourceView* Source = GetTexture( Data.Quad.Handle, SourceView, SourceHandle, SourceTextureIdentity );
		const ID3D11ShaderResourceView* Mask = Data.MaskTexture >= 0 ? GetTexture( Data.MaskTexture, MaskView, MaskHandle, MaskTextureIdentity ) : nullptr;
		const ID3D11ShaderResourceView* Distortion = DistortionData != nullptr ? GetTexture( DistortionData->DistortionTexture, DistortionView, DistortionHandle, DistortionTextureIdentity ) : nullptr;
		if ( Source == nullptr || ( Data.MaskTexture >= 0 && Mask == nullptr ) || ( DistortionData != nullptr && Distortion == nullptr ) )
		{
			if ( !bFailedLogged ) { LOG( LogEffectMaterial, Warning, "材質描画用またはUVゆがみ用の画像をDirect3D 11から取得できません" ); bFailedLogged = true; }
			return false;
		}
		// 呼び出し元が選んだ合成種類と強さを反映する。
		int32 BlendMode = DX_BLENDMODE_ALPHA;
		int32 BlendParam = 255;
		GetDrawBlendMode( &BlendMode, &BlendParam );
		BlendParam = NMath::Min( 255, NMath::Max( 0, BlendParam ) );
		ID3D11BlendState* BlendState = BlendMode == DX_BLENDMODE_ALPHA ? AlphaBlend : BlendMode == DX_BLENDMODE_ADD ? AddBlend : nullptr;
		if ( BlendState == nullptr )
		{
			if ( !bUnsupportedBlendLogged ) { LOG( LogEffectMaterial, Warning, "材質描画はAlphaとAdd以外の合成方法を扱えません" ); bUnsupportedBlendLogged = true; }
			return false;
		}
		RenderVertex();
		// DxLibが選んだ描画先と、このcontextへ現在結ばれた描画先を照合する。
		const int32 DrawScreen = GetDrawScreen();
		ID3D11RenderTargetView* ExpectedTarget = DrawScreen == DX_SCREEN_BACK ? const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetUseDirect3D11BackBufferRenderTargetView() ) ) : const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetGraphID3D11RenderTargetView( DrawScreen ) ) );
		ID3D11RenderTargetView* CurrentTarget = nullptr;
		Context->OMGetRenderTargets( 1, &CurrentTarget, nullptr );
		// 論理描画範囲に一致したnative viewportかを確かめる。
		RECT DrawArea{};
		GetDrawArea( &DrawArea );
		D3D11_VIEWPORT CurrentViewport{};
		UINT ViewportCount = 1;
		Context->RSGetViewports( &ViewportCount, &CurrentViewport );
		// contextがDxLib指定の描画先・表示範囲を使っているか判定する。
		const bool bTargetMatches = ExpectedTarget != nullptr && CurrentTarget == ExpectedTarget;
		const bool bViewportMatches = ViewportCount == 1 && CurrentViewport.TopLeftX == static_cast<float32>( DrawArea.left ) && CurrentViewport.TopLeftY == static_cast<float32>( DrawArea.top ) && CurrentViewport.Width == static_cast<float32>( DrawArea.right - DrawArea.left ) && CurrentViewport.Height == static_cast<float32>( DrawArea.bottom - DrawArea.top );
		if ( CurrentTarget != nullptr ) CurrentTarget->Release();
		if ( !bTargetMatches || !bViewportMatches )
		{
			if ( !bStateMismatchLogged ) { LOG( LogEffectMaterial, Warning, "Direct3Dの描画先または表示範囲がDxLibの現在設定と一致しません" ); bStateMismatchLogged = true; }
			return false;
		}
		// 描画後にDxLibへ戻すnative stateを参照付きで確保する。
		FNativeState Saved;
		Saved.Save( Context );
		{
			// 描画中にDxLibがcontextを差し替えた場合は元状態を戻して中断する。
			ID3D11DeviceContext* Current = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
			if ( Current != Context )
			{
				Saved.Restore( Context );
				return false;
			}
		}
		// DxLibのworld/view/projection/logical viewportをclip座標用に合成する。
		MATRIX World{}, View{}, Projection{}, Viewport{}, ApiViewport{};
		GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetTransformToAPIViewportMatrix( &ApiViewport );
		// DxLib native viewport変換を逆にし、logical viewportの変換だけを残す。
		const MATRIX ApiViewportInverse = MInverse( ApiViewport );
		MATRIX Wvp = MMult( MMult( MMult( MMult( World, View ), Projection ), Viewport ), ApiViewportInverse );
		for ( int Row = 0; Row < 4; ++Row ) for ( int Column = 0; Column < 4; ++Column ) if ( !NMath::IsFinite( Wvp.m[Row][Column] ) ) { Saved.Restore( Context ); return false; }
		// quad位置と既定方向を既存DrawQuad3Dと同じ基準で用意する。
		const VECTOR Position = ArrayVector( Data.Quad.Pos );
		VECTOR Right = ArrayVector( Data.Quad.Right );
		VECTOR Up = ArrayVector( Data.Quad.Up );
		if ( static_cast<EDrawQuadOrient>( Data.Quad.Orient ) == EDrawQuadOrient::CameraY )
		{
			// 高さを無視したカメラ方向。真上・真下ではカメラ右方向を使う。
			VECTOR Forward = VGet( Position.x - GetCameraPosition().x, 0.0f, Position.z - GetCameraPosition().z );
			Up = VGet( 0.0f, 1.0f, 0.0f );
			if ( VSquareSize( Forward ) < 1.0e-8f ) Right = GetCameraRightVector();
			else Right = VCross( Up, VNorm( Forward ) );
		}
		else if ( static_cast<EDrawQuadOrient>( Data.Quad.Orient ) == EDrawQuadOrient::Camera )
		{
			// camera facingの横・上軸をAngleだけ回す。
			const VECTOR CameraRight = GetCameraRightVector(); const VECTOR CameraUp = GetCameraUpVector(); const float32 Cos = NMath::Cos( Data.Quad.Angle ); const float32 Sin = NMath::Sin( Data.Quad.Angle );
			Right = VAdd( VScale( CameraRight, Cos ), VScale( CameraUp, Sin ) ); Up = VAdd( VScale( CameraUp, Cos ), VScale( CameraRight, -Sin ) );
		}
		// pivotを基準にした四隅の左右・上下距離。
		const float32 Left = -Data.Quad.PivotX * Data.Quad.Width; const float32 RightEdge = ( 1.0f - Data.Quad.PivotX ) * Data.Quad.Width; const float32 Top = Data.Quad.PivotY * Data.Quad.Height; const float32 Bottom = -( 1.0f - Data.Quad.PivotY ) * Data.Quad.Height;
		// 基準位置からRight/Up方向へ移動した角のworld位置を作る。
		const auto Corner = [&]( float32 X, float32 Y ) { return VAdd( Position, VAdd( VScale( Right, X ), VScale( Up, Y ) ) ); };
		// 位置と画像座標を一つの頂点へまとめる。
		const auto MakeVertex = [&]( const VECTOR& Point, float32 U, float32 V ) { return FVertex { { Point.x, Point.y, Point.z }, { U, V } }; };
		const FVertex Vertices[6] = { MakeVertex( Corner( Left, Top ), Data.Quad.U0, Data.Quad.V0 ), MakeVertex( Corner( RightEdge, Top ), Data.Quad.U1, Data.Quad.V0 ), MakeVertex( Corner( Left, Bottom ), Data.Quad.U0, Data.Quad.V1 ), MakeVertex( Corner( RightEdge, Top ), Data.Quad.U1, Data.Quad.V0 ), MakeVertex( Corner( RightEdge, Bottom ), Data.Quad.U1, Data.Quad.V1 ), MakeVertex( Corner( Left, Bottom ), Data.Quad.U0, Data.Quad.V1 ) };
		// 元画像・mask・ゆがみ画像のgraph領域。未指定領域は通常の全画像。
		float32 SourceRect[4] = {}, SourceInset[4] = {}, MaskRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, MaskInset[4] = {}, DistortionRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, DistortionInset[4] = {};
		if ( !GetGraphUvMapping( Data.Quad.Handle, SourceRect, SourceInset ) || ( Data.MaskTexture >= 0 && !GetGraphUvMapping( Data.MaskTexture, MaskRect, MaskInset ) ) || ( DistortionData != nullptr && !GetGraphUvMapping( DistortionData->DistortionTexture, DistortionRect, DistortionInset ) ) ) { Saved.Restore( Context ); return false; }
		// CPU更新中のvertex buffer内データ位置。
		D3D11_MAPPED_SUBRESOURCE Mapped{};
		if ( FAILED( Context->Map( VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, Vertices, sizeof( Vertices ) ); Context->Unmap( VertexBuffer, 0 );
		// vertex shaderへ渡すworldからclipまでの変換。
		FVertexConstants VsConstants{};
		FMemory::Memcpy( VsConstants.Wvp, &Wvp, sizeof( VsConstants.Wvp ) );
		// pixel shaderへ渡すuv・色・消失・発光の設定一式。
		FPixelConstants PsConstants{};
		PsConstants.UvScaleOffset[0] = Data.UVScale[0]; PsConstants.UvScaleOffset[1] = Data.UVScale[1]; PsConstants.UvScaleOffset[2] = Data.UVOffset[0]; PsConstants.UvScaleOffset[3] = Data.UVOffset[1];
		PsConstants.Values[0] = Data.Dissolve; PsConstants.Values[1] = static_cast<float32>( Data.bDissolve ); PsConstants.Values[2] = Data.MaskScale; PsConstants.Values[3] = Data.Emission;
		PsConstants.Tint[0] = Data.Quad.Color.R / 255.0f; PsConstants.Tint[1] = Data.Quad.Color.G / 255.0f; PsConstants.Tint[2] = Data.Quad.Color.B / 255.0f; PsConstants.Tint[3] = Data.Quad.Color.A / 255.0f;
		PsConstants.Edge[0] = Data.EdgeColor.R / 255.0f * Data.EdgeColor.A / 255.0f * Data.EdgeEmission; PsConstants.Edge[1] = Data.EdgeColor.G / 255.0f * Data.EdgeColor.A / 255.0f * Data.EdgeEmission; PsConstants.Edge[2] = Data.EdgeColor.B / 255.0f * Data.EdgeColor.A / 255.0f * Data.EdgeEmission; PsConstants.Edge[3] = Data.EdgeWidth;
		PsConstants.BrightBlend[0] = 1.0f; PsConstants.BrightBlend[1] = 1.0f; PsConstants.BrightBlend[2] = 1.0f; PsConstants.BrightBlend[3] = 1.0f;
		// 呼び出し元のDrawBrightを各RGB成分へ反映する。
		int32 Bright[3] = { 255, 255, 255 }; GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
		PsConstants.BrightBlend[0] *= Bright[0] / 255.0f; PsConstants.BrightBlend[1] *= Bright[1] / 255.0f; PsConstants.BrightBlend[2] *= Bright[2] / 255.0f;
		PsConstants.Options[0] = Data.MaskTexture >= 0 ? 1.0f : 0.0f; PsConstants.Options[1] = BlendParam / 255.0f;
		FMemory::Memcpy( PsConstants.SourceRect, SourceRect, sizeof( SourceRect ) ); FMemory::Memcpy( PsConstants.SourceInset, SourceInset, sizeof( SourceInset ) ); FMemory::Memcpy( PsConstants.MaskRect, MaskRect, sizeof( MaskRect ) ); FMemory::Memcpy( PsConstants.MaskInset, MaskInset, sizeof( MaskInset ) );
		if ( DistortionData != nullptr )
		{
			PsConstants.DistortionScaleOffset[0] = DistortionData->UVScale[0]; PsConstants.DistortionScaleOffset[1] = DistortionData->UVScale[1]; PsConstants.DistortionScaleOffset[2] = DistortionData->UVOffset[0]; PsConstants.DistortionScaleOffset[3] = DistortionData->UVOffset[1];
			PsConstants.DistortionValues[0] = DistortionData->Strength; PsConstants.DistortionValues[1] = 1.0f;
			FMemory::Memcpy( PsConstants.DistortionRect, DistortionRect, sizeof( DistortionRect ) ); FMemory::Memcpy( PsConstants.DistortionInset, DistortionInset, sizeof( DistortionInset ) );
		}
		if ( FAILED( Context->Map( VertexConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &VsConstants, sizeof( VsConstants ) ); Context->Unmap( VertexConstants, 0 );
		if ( FAILED( Context->Map( PixelConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &PsConstants, sizeof( PsConstants ) ); Context->Unmap( PixelConstants, 0 );
		// native drawへ渡すvertex・constant・SRV参照とblend係数。
		const UINT Stride = sizeof( FVertex ); const UINT Offset = 0; ID3D11Buffer* Vb = VertexBuffer; ID3D11Buffer* VsCb = VertexConstants; ID3D11Buffer* PsCb = PixelConstants; ID3D11ShaderResourceView* Srvs[3] = { const_cast<ID3D11ShaderResourceView*>( Source ), const_cast<ID3D11ShaderResourceView*>( Mask ), const_cast<ID3D11ShaderResourceView*>( Distortion ) }; const FLOAT BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, &Vb, &Stride, &Offset ); Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		Context->VSSetShader( VertexShader, nullptr, 0 ); Context->PSSetShader( PixelShader, nullptr, 0 ); Context->GSSetShader( nullptr, nullptr, 0 ); Context->HSSetShader( nullptr, nullptr, 0 ); Context->DSSetShader( nullptr, nullptr, 0 );
		Context->VSSetConstantBuffers( 0, 1, &VsCb ); Context->PSSetConstantBuffers( 0, 1, &PsCb ); Context->PSSetShaderResources( 0, 3, Srvs ); Context->PSSetSamplers( 0, 1, &Sampler );
		// callerのraster設定を保ち、両面描画に必要な項目だけ変える。
		D3D11_RASTERIZER_DESC RasterDesc{}; if ( Saved.Raster != nullptr ) Saved.Raster->GetDesc( &RasterDesc ); else { RasterDesc.FillMode = D3D11_FILL_SOLID; RasterDesc.DepthClipEnable = TRUE; }
		RasterDesc.CullMode = D3D11_CULL_NONE;
		if ( !bRasterDescValid || FMemory::Memcmp( &RasterDescCache, &RasterDesc, sizeof( RasterDesc ) ) != 0 )
		{
			if ( Raster != nullptr ) Raster->Release();
			Raster = nullptr;
			if ( FAILED( Device->CreateRasterizerState( &RasterDesc, &Raster ) ) ) { bRasterDescValid = false; Saved.Restore( Context ); return false; }
			RasterDescCache = RasterDesc; bRasterDescValid = true;
		}
		Context->OMSetBlendState( BlendState, BlendFactor, 0xffffffffu ); Context->OMSetDepthStencilState( DepthStates[Data.DepthTest | ( Data.DepthWrite << 1 )], 0 ); Context->RSSetState( Raster ); Context->Draw( 6, 0 );
		Saved.Restore( Context );
		return true;
	}

	// 連続UVと点ごとの材質値を補間し、共有Ribbon meshをnative D3D11で描く。
	bool DrawRibbon( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points )
	{
		return DrawRibbonInternal( Data, Points, nullptr, nullptr );
	}

	// 共通入力を検証し、帯へ点別のnoise移動と強さを渡す。
	bool DrawDistortedRibbon( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points )
	{
		if ( !NDrawDistortedMaterialRibbon::IsValid( Data, Points ) ) return false;
		return DrawRibbonInternal( Data.Material, nullptr, &Data, Points );
	}

	// 旧点列とゆがみ付き点列を同じmesh・native状態経路で描画する。
	bool DrawRibbonInternal( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points, const FDrawDistortedMaterialRibbonData* DistortionData, const FDrawDistortedMaterialRibbonData::FPoint* DistortionPoints )
	{
		// mesh展開とshader設定へ渡すRibbon側の基本値。
		const FDrawRibbonData& Ribbon = Data.Ribbon;
		const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
		if ( ( Points == nullptr && DistortionPoints == nullptr ) || ( ( DistortionData == nullptr ) != ( DistortionPoints == nullptr ) ) || Ribbon.NumPoints < 2 || Ribbon.NumPoints > FDrawRibbonData::MaxPoints || Ribbon.Texture < 0 || !NMath::IsFinite( Ribbon.UVRepeat ) || Ribbon.UVRepeat <= 0.0f || ( Material.DepthTest != 0 && Material.DepthTest != 1 ) || ( Material.DepthWrite != 0 && Material.DepthWrite != 1 ) || Material.MaskTexture < -1 || ( Material.bDissolve != 0 && Material.bDissolve != 1 ) ) return false;
		// 呼び出し元の形式から既存材質部分を参照する関数。
		const auto MaterialPoint = [&]( int32 Index ) -> const FDrawMaterialRibbonData::FPoint& { return DistortionPoints != nullptr ? DistortionPoints[Index].Material : Points[Index]; };
		// 共通helperが作るworld位置・連続UV・三角形番号。
		FDxLibRibbonMesh Mesh;
		// 40byte材質点からhelperの旧24byte経路点だけを複写する配列。
		TArray<FDrawRibbonData::FPoint> PathPoints;
		PathPoints.SetNum( Ribbon.NumPoints );
		// index展開後にGPUへ送る非indexed頂点列。
		TArray<FVertex> Vertices;
		// 共通経路点へmiter形状を構築する変換loop。
		for ( int32 Index = 0; Index < Ribbon.NumPoints; ++Index ) PathPoints[Index] = MaterialPoint( Index ).Path;
		if ( !NDxLibRibbonMesh::Build( Ribbon, PathPoints.GetData(), false, Mesh ) ) return false;
		if ( Mesh.Indices.Num() < 3 || Mesh.Indices.Num() > ( Ribbon.NumPoints - 1 ) * 6 ) return false;
		Vertices.SetNum( Mesh.Indices.Num() );
		// 三角形番号ごとに頂点属性をshader入力へ複写するloop。
		for ( int32 Index = 0; Index < Mesh.Indices.Num(); ++Index )
		{
			// 展開meshから読み出す左右頂点のindex。
			const unsigned short SourceIndex = Mesh.Indices[Index];
			// 左右頂点が由来する元経路点の番号。
			const int32 PointIndex = SourceIndex / 2;
			if ( PointIndex < 0 || PointIndex >= Ribbon.NumPoints ) return false;
			// GPU頂点へ位置とUVを渡す元mesh頂点。
			const VERTEX3D& Source = Mesh.Vertices[SourceIndex];
			// 色・消失・発光・移動量を渡す元材質点。
			const FDrawMaterialRibbonData::FPoint& Point = MaterialPoint( PointIndex );
			// GPUへ送る今回の三角形頂点。
			FVertex& Vertex = Vertices[Index];
			Vertex.Position[0] = Source.pos.x; Vertex.Position[1] = Source.pos.y; Vertex.Position[2] = Source.pos.z;
			Vertex.UV[0] = Source.u; Vertex.UV[1] = Source.v;
			Vertex.Color[0] = static_cast<float32>( Point.Path.Color & 0xFFu ) / 255.0f; Vertex.Color[1] = static_cast<float32>( ( Point.Path.Color >> 8 ) & 0xFFu ) / 255.0f; Vertex.Color[2] = static_cast<float32>( ( Point.Path.Color >> 16 ) & 0xFFu ) / 255.0f; Vertex.Color[3] = static_cast<float32>( ( Point.Path.Color >> 24 ) & 0xFFu ) / 255.0f;
			Vertex.Dissolve = Point.Dissolve; Vertex.Emission = Point.Emission;
			Vertex.UVOffset[0] = Point.UVOffset[0]; Vertex.UVOffset[1] = Point.UVOffset[1];
			if ( DistortionPoints != nullptr ) { Vertex.DistortionOffset[0] = DistortionPoints[PointIndex].UVOffset[0]; Vertex.DistortionOffset[1] = DistortionPoints[PointIndex].UVOffset[1]; Vertex.DistortionStrength = DistortionPoints[PointIndex].Strength; }
		}
		if ( !Initialize() ) return false;
		// graph crop領域を反映したsourceとoptional mask shader view。
		const ID3D11ShaderResourceView* Source = GetTexture( Ribbon.Texture, SourceView, SourceHandle, SourceTextureIdentity );
		const ID3D11ShaderResourceView* Mask = Material.MaskTexture >= 0 ? GetTexture( Material.MaskTexture, MaskView, MaskHandle, MaskTextureIdentity ) : nullptr;
		const ID3D11ShaderResourceView* Distortion = DistortionData != nullptr ? GetTexture( DistortionData->DistortionTexture, DistortionView, DistortionHandle, DistortionTextureIdentity ) : nullptr;
		if ( Source == nullptr || ( Material.MaskTexture >= 0 && Mask == nullptr ) || ( DistortionData != nullptr && Distortion == nullptr ) )
		{
			if ( !bFailedLogged ) { LOG( LogEffectMaterial, Warning, "材質付き帯の画像をDirect3D 11から取得できません" ); bFailedLogged = true; }
			return false;
		}
		// 現在のDxLib合成種類と濃さ。
		int32 BlendMode = DX_BLENDMODE_ALPHA;
		int32 BlendParam = 255;
		GetDrawBlendMode( &BlendMode, &BlendParam );
		BlendParam = NMath::Min( 255, NMath::Max( 0, BlendParam ) );
		// shaderへ設定するAlphaまたはAdd専用blend state。
		ID3D11BlendState* BlendState = BlendMode == DX_BLENDMODE_ALPHA ? AlphaBlend : BlendMode == DX_BLENDMODE_ADD ? AddBlend : nullptr;
		if ( BlendState == nullptr )
		{
			if ( !bUnsupportedBlendLogged ) { LOG( LogEffectMaterial, Warning, "材質付き帯はAlphaとAdd以外の合成方法を扱えません" ); bUnsupportedBlendLogged = true; }
			return false;
		}
		RenderVertex();
		// DxLibが選択している描画先handle。
		const int32 DrawScreen = GetDrawScreen();
		// 現在の画面に対応するnative render target。
		ID3D11RenderTargetView* ExpectedTarget = DrawScreen == DX_SCREEN_BACK ? const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetUseDirect3D11BackBufferRenderTargetView() ) ) : const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetGraphID3D11RenderTargetView( DrawScreen ) ) );
		// native contextから撮影する現在のrender target。
		ID3D11RenderTargetView* CurrentTarget = nullptr;
		Context->OMGetRenderTargets( 1, &CurrentTarget, nullptr );
		// DxLib論理設定とnative viewportを比較する矩形。
		RECT DrawArea{};
		GetDrawArea( &DrawArea );
		// DxLibの描画範囲が反映されたnative viewport。
		D3D11_VIEWPORT CurrentViewport{};
		// 現在設定されているviewport数。
		UINT ViewportCount = 1;
		Context->RSGetViewports( &ViewportCount, &CurrentViewport );
		// 描画先と表示範囲がDxLib設定に追随しているか。
		const bool bTargetMatches = ExpectedTarget != nullptr && CurrentTarget == ExpectedTarget;
		const bool bViewportMatches = ViewportCount == 1 && CurrentViewport.TopLeftX == static_cast<float32>( DrawArea.left ) && CurrentViewport.TopLeftY == static_cast<float32>( DrawArea.top ) && CurrentViewport.Width == static_cast<float32>( DrawArea.right - DrawArea.left ) && CurrentViewport.Height == static_cast<float32>( DrawArea.bottom - DrawArea.top );
		if ( CurrentTarget != nullptr ) CurrentTarget->Release();
		if ( !bTargetMatches || !bViewportMatches )
		{
			if ( !bStateMismatchLogged ) { LOG( LogEffectMaterial, Warning, "材質付き帯の描画先または表示範囲がDxLib設定と一致しません" ); bStateMismatchLogged = true; }
			return false;
		}
		// native drawで変更する全stateとCOM参照の退避先。
		FNativeState Saved;
		Saved.Save( Context );
		{
			// 描画時にDxLibが現在使うimmediate context。
			ID3D11DeviceContext* Current = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
			if ( Current != Context ) { Saved.Restore( Context ); return false; }
		}
		// world座標のRibbonを画面へ移すDxLibの論理変換行列。
		MATRIX View{}, Projection{}, Viewport{}, ApiViewport{};
		GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetTransformToAPIViewportMatrix( &ApiViewport );
		// native viewportと論理viewportの二重変換を避ける逆行列。
		const MATRIX ApiViewportInverse = MInverse( ApiViewport );
		// 頂点shaderへ渡すview・projection・論理viewport補正の積。
		MATRIX Wvp = MMult( MMult( MMult( View, Projection ), Viewport ), ApiViewportInverse );
		for ( int Row = 0; Row < 4; ++Row ) for ( int Column = 0; Column < 4; ++Column ) if ( !NMath::IsFinite( Wvp.m[Row][Column] ) ) { Saved.Restore( Context ); return false; }
		// 派生画像のsource/mask領域とfilter端の漏れを避ける幅。
		float32 SourceRect[4] = {}, SourceInset[4] = {}, MaskRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, MaskInset[4] = {}, DistortionRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, DistortionInset[4] = {};
		if ( !GetGraphUvMapping( Ribbon.Texture, SourceRect, SourceInset ) || ( Material.MaskTexture >= 0 && !GetGraphUvMapping( Material.MaskTexture, MaskRect, MaskInset ) ) || ( DistortionData != nullptr && !GetGraphUvMapping( DistortionData->DistortionTexture, DistortionRect, DistortionInset ) ) ) { Saved.Restore( Context ); return false; }
		// map結果をvertexまたはconstant bufferへ順次書き込む領域。
		D3D11_MAPPED_SUBRESOURCE Mapped{};
		if ( FAILED( Context->Map( VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, Vertices.GetData(), static_cast<size_t>( Vertices.Num() ) * sizeof( FVertex ) ); Context->Unmap( VertexBuffer, 0 );
		// vertex shaderの行列bufferへ渡す値。
		FVertexConstants VsConstants{};
		FMemory::Memcpy( VsConstants.Wvp, &Wvp, sizeof( VsConstants.Wvp ) );
		// pixel shaderのUV・mask・edge・blend設定値。
		FPixelConstants PsConstants{};
		PsConstants.UvScaleOffset[0] = Material.UVScale[0]; PsConstants.UvScaleOffset[1] = Material.UVScale[1];
		PsConstants.Values[0] = 0.0f; PsConstants.Values[1] = static_cast<float32>( Material.bDissolve ); PsConstants.Values[2] = Material.MaskScale; PsConstants.Values[3] = 0.0f;
		PsConstants.Tint[0] = 1.0f; PsConstants.Tint[1] = 1.0f; PsConstants.Tint[2] = 1.0f; PsConstants.Tint[3] = 1.0f;
		PsConstants.Edge[0] = Material.EdgeColor.R / 255.0f * Material.EdgeColor.A / 255.0f * Material.EdgeEmission; PsConstants.Edge[1] = Material.EdgeColor.G / 255.0f * Material.EdgeColor.A / 255.0f * Material.EdgeEmission; PsConstants.Edge[2] = Material.EdgeColor.B / 255.0f * Material.EdgeColor.A / 255.0f * Material.EdgeEmission; PsConstants.Edge[3] = Material.EdgeWidth;
		PsConstants.BrightBlend[0] = 1.0f; PsConstants.BrightBlend[1] = 1.0f; PsConstants.BrightBlend[2] = 1.0f; PsConstants.BrightBlend[3] = 1.0f;
		// DxLib callerが指定するRGB明度。
		int32 Bright[3] = { 255, 255, 255 }; GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
		PsConstants.BrightBlend[0] *= Bright[0] / 255.0f; PsConstants.BrightBlend[1] *= Bright[1] / 255.0f; PsConstants.BrightBlend[2] *= Bright[2] / 255.0f;
		PsConstants.Options[0] = Material.MaskTexture >= 0 ? 1.0f : 0.0f; PsConstants.Options[1] = BlendParam / 255.0f; PsConstants.Options[3] = 1.0f;
		FMemory::Memcpy( PsConstants.SourceRect, SourceRect, sizeof( SourceRect ) ); FMemory::Memcpy( PsConstants.SourceInset, SourceInset, sizeof( SourceInset ) ); FMemory::Memcpy( PsConstants.MaskRect, MaskRect, sizeof( MaskRect ) ); FMemory::Memcpy( PsConstants.MaskInset, MaskInset, sizeof( MaskInset ) );
		if ( DistortionData != nullptr ) { PsConstants.DistortionScaleOffset[0] = DistortionData->UVScale[0]; PsConstants.DistortionScaleOffset[1] = DistortionData->UVScale[1]; PsConstants.DistortionValues[1] = 1.0f; FMemory::Memcpy( PsConstants.DistortionRect, DistortionRect, sizeof( DistortionRect ) ); FMemory::Memcpy( PsConstants.DistortionInset, DistortionInset, sizeof( DistortionInset ) ); }
		if ( FAILED( Context->Map( VertexConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &VsConstants, sizeof( VsConstants ) ); Context->Unmap( VertexConstants, 0 );
		if ( FAILED( Context->Map( PixelConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &PsConstants, sizeof( PsConstants ) ); Context->Unmap( PixelConstants, 0 );
		// 先頭slotへ結び直す頂点stride・offset・buffer・shader resource群。
		const UINT Stride = sizeof( FVertex ); const UINT Offset = 0; ID3D11Buffer* Vb = VertexBuffer; ID3D11Buffer* VsCb = VertexConstants; ID3D11Buffer* PsCb = PixelConstants; ID3D11ShaderResourceView* Srvs[3] = { const_cast<ID3D11ShaderResourceView*>( Source ), const_cast<ID3D11ShaderResourceView*>( Mask ), const_cast<ID3D11ShaderResourceView*>( Distortion ) }; const FLOAT BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, &Vb, &Stride, &Offset ); Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		Context->VSSetShader( VertexShader, nullptr, 0 ); Context->PSSetShader( PixelShader, nullptr, 0 ); Context->GSSetShader( nullptr, nullptr, 0 ); Context->HSSetShader( nullptr, nullptr, 0 ); Context->DSSetShader( nullptr, nullptr, 0 );
		Context->VSSetConstantBuffers( 0, 1, &VsCb ); Context->PSSetConstantBuffers( 0, 1, &PsCb ); Context->PSSetShaderResources( 0, DistortionData != nullptr ? 3u : 2u, Srvs ); Context->PSSetSamplers( 0, 1, &Sampler );
		// caller raster設定を保ちつつ帯の両面を描くdescriptor。
		D3D11_RASTERIZER_DESC RasterDesc{}; if ( Saved.Raster != nullptr ) Saved.Raster->GetDesc( &RasterDesc ); else { RasterDesc.FillMode = D3D11_FILL_SOLID; RasterDesc.DepthClipEnable = TRUE; }
		RasterDesc.CullMode = D3D11_CULL_NONE;
		if ( !bRasterDescValid || FMemory::Memcmp( &RasterDescCache, &RasterDesc, sizeof( RasterDesc ) ) != 0 )
		{
			if ( Raster != nullptr ) Raster->Release();
			Raster = nullptr;
			if ( FAILED( Device->CreateRasterizerState( &RasterDesc, &Raster ) ) ) { bRasterDescValid = false; Saved.Restore( Context ); return false; }
			RasterDescCache = RasterDesc; bRasterDescValid = true;
		}
		Context->OMSetBlendState( BlendState, BlendFactor, 0xffffffffu ); Context->OMSetDepthStencilState( DepthStates[Material.DepthTest | ( Material.DepthWrite << 1 )], 0 ); Context->RSSetState( Raster ); Context->Draw( Vertices.Num(), 0 );
		Saved.Restore( Context );
		return true;
	}

	// index順に頂点属性を展開し、Ribbonと同じnative材質shaderでworld meshを描く。
	bool DrawMeshInternal( const FDrawMaterialMeshData& Data, const FMeshVertex* SourceVertices, const uint32* Indices, bool bUseRegularCulling, const FMatrix4x4* World, int32 NativeCull, float32 Opacity, bool bOverrideNativeCull )
	{
		if ( !NDrawMaterialMesh::IsValid( Data, SourceVertices, Indices ) )
		{
			if ( !bInvalidDataLogged ) { LOG( LogEffectMaterial, Warning, "材質付きメッシュの頂点またはindexが不正なため描画を読み飛ばしました" ); bInvalidDataLogged = true; }
			return false;
		}
		if ( !Initialize() ) return false;
		// 元画像と任意のmask・noiseをnative shader用に取得する。
		// 元画像を参照するSRV。
		const ID3D11ShaderResourceView* Source = GetTexture( Data.Texture, SourceView, SourceHandle, SourceTextureIdentity );
		// 消失maskがある場合だけ使うSRV。
		const ID3D11ShaderResourceView* Mask = Data.Material.MaskTexture >= 0 ? GetTexture( Data.Material.MaskTexture, MaskView, MaskHandle, MaskTextureIdentity ) : nullptr;
		// 正の変位強度でnoise画像を使うか。
		const bool bUseDistortion = Data.DistortionStrength > 0.0f;
		// ゆがみ画像を参照するSRV。
		const ID3D11ShaderResourceView* Distortion = bUseDistortion ? GetTexture( Data.DistortionTexture, DistortionView, DistortionHandle, DistortionTextureIdentity ) : nullptr;
		if ( Source == nullptr || ( Data.Material.MaskTexture >= 0 && Mask == nullptr ) || ( bUseDistortion && Distortion == nullptr ) )
		{
			if ( !bFailedLogged ) { LOG( LogEffectMaterial, Warning, "材質付きメッシュの画像をDirect3D 11から取得できません" ); bFailedLogged = true; }
			return false;
		}
		// 描画設定から対応するalphaまたはadd合成stateを選ぶ。
		// DxLibが設定した合成種類。
		int32 BlendMode = DX_BLENDMODE_ALPHA;
		// callerが設定した合成の強さ。
		int32 BlendParam = 255;
		GetDrawBlendMode( &BlendMode, &BlendParam );
		BlendParam = NMath::Min( 255, NMath::Max( 0, BlendParam ) );
		// 対応するalpha/add用native state。
		ID3D11BlendState* BlendState = BlendMode == DX_BLENDMODE_ALPHA ? AlphaBlend : BlendMode == DX_BLENDMODE_ADD ? AddBlend : nullptr;
		if ( BlendState == nullptr )
		{
			if ( !bUnsupportedBlendLogged ) { LOG( LogEffectMaterial, Warning, "材質付きメッシュはAlphaとAdd以外の合成方法を扱えません" ); bUnsupportedBlendLogged = true; }
			return false;
		}
		// DxLibの保留中頂点を先にflushしてnative drawの順序を保つ。
		RenderVertex();
		// DxLib選択先のRTとviewportがnative contextへ反映済みか調べる。
		// 現在DxLibが選んだ画面handle。
		const int32 DrawScreen = GetDrawScreen();
		// 描画先画面から得るべきnative target。
		ID3D11RenderTargetView* ExpectedTarget = DrawScreen == DX_SCREEN_BACK ? const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetUseDirect3D11BackBufferRenderTargetView() ) ) : const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetGraphID3D11RenderTargetView( DrawScreen ) ) );
		// contextから参照を取得する現在の描画先。
		ID3D11RenderTargetView* CurrentTarget = nullptr;
		Context->OMGetRenderTargets( 1, &CurrentTarget, nullptr );
		// DxLibが論理的に指定しているpixel範囲。
		RECT DrawArea{};
		GetDrawArea( &DrawArea );
		// contextが現在使っているviewport。
		D3D11_VIEWPORT CurrentViewport{};
		// viewport設定の件数。
		UINT ViewportCount = 1;
		Context->RSGetViewports( &ViewportCount, &CurrentViewport );
		// 期待した描画先がcontextへ反映済みか。
		const bool bTargetMatches = ExpectedTarget != nullptr && CurrentTarget == ExpectedTarget;
		// native viewportがDxLibのDrawAreaに一致するか。
		const bool bViewportMatches = ViewportCount == 1 && CurrentViewport.TopLeftX == static_cast<float32>( DrawArea.left ) && CurrentViewport.TopLeftY == static_cast<float32>( DrawArea.top ) && CurrentViewport.Width == static_cast<float32>( DrawArea.right - DrawArea.left ) && CurrentViewport.Height == static_cast<float32>( DrawArea.bottom - DrawArea.top );
		if ( CurrentTarget != nullptr ) CurrentTarget->Release();
		if ( !bTargetMatches || !bViewportMatches )
		{
			if ( !bStateMismatchLogged ) { LOG( LogEffectMaterial, Warning, "材質付きメッシュの描画先または表示範囲がDxLib設定と一致しません" ); bStateMismatchLogged = true; }
			return false;
		}
		// native drawで変更する全stateを参照付きで退避する。
		FNativeState Saved;
		Saved.Save( Context );
		{
			// 描画直前にDxLibのimmediate contextが入れ替わっていないか確認する。
			// DxLibから取得した現在context。
			ID3D11DeviceContext* Current = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
			if ( Current != Context ) { Saved.Restore( Context ); return false; }
		}
		// 入力頂点は既にworld座標なのでworld行列を掛けずview以降を合成する。
		// DxLibが設定したview・projection・論理viewport。
		MATRIX View{}, Projection{}, Viewport{}, ApiViewport{};
		GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetTransformToAPIViewportMatrix( &ApiViewport );
		// native viewport変換の重複を避ける逆行列。
		const MATRIX ApiViewportInverse = MInverse( ApiViewport );
		// shaderへ送るworld座標からclip座標への変換。
		MATRIX Wvp = MMult( MMult( MMult( View, Projection ), Viewport ), ApiViewportInverse );
		if ( World != nullptr ) Wvp = MMult( static_cast<MATRIX>( *World ), Wvp );
		for ( int Row = 0; Row < 4; ++Row ) for ( int Column = 0; Column < 4; ++Column ) if ( !NMath::IsFinite( Wvp.m[Row][Column] ) ) { Saved.Restore( Context ); return false; }
		// source・mask・noise graph領域をbase texture内へ写す値。
		// 元画像のbase内矩形と半texel幅。
		float32 SourceRect[4] = {}, SourceInset[4] = {}, MaskRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, MaskInset[4] = {}, DistortionRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f }, DistortionInset[4] = {};
		if ( !GetGraphUvMapping( Data.Texture, SourceRect, SourceInset ) || ( Data.Material.MaskTexture >= 0 && !GetGraphUvMapping( Data.Material.MaskTexture, MaskRect, MaskInset ) ) || ( bUseDistortion && !GetGraphUvMapping( Data.DistortionTexture, DistortionRect, DistortionInset ) ) ) { Saved.Restore( Context ); return false; }
		// index順に展開した非indexed頂点をGPUへ送る配列。
		TArray<FVertex> Vertices;
		Vertices.SetNum( Data.NumIndices );
		// 各三角形頂点へ位置・色・UVと共通材質値を写す添字。
		for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
		{
			// 元meshから読む頂点。
			const FMeshVertex& Input = SourceVertices[Indices[Index]];
			// GPUへ送る展開済み頂点。
			FVertex& Output = Vertices[Index];
			Output.Position[0] = Input.Position[0]; Output.Position[1] = Input.Position[1]; Output.Position[2] = Input.Position[2];
			Output.UV[0] = Input.U; Output.UV[1] = Input.V;
			Output.Color[0] = Input.Color[0] / 255.0f; Output.Color[1] = Input.Color[1] / 255.0f; Output.Color[2] = Input.Color[2] / 255.0f;
			if ( World != nullptr ) Output.Color[3] = Input.Color[3] / 255.0f * Opacity;
			else Output.Color[3] = Input.Color[3] / 255.0f;
			Output.Dissolve = Data.Dissolve; Output.Emission = Data.Emission;
			Output.UVOffset[0] = Data.UVOffset[0]; Output.UVOffset[1] = Data.UVOffset[1];
			if ( bUseDistortion ) { Output.DistortionOffset[0] = Data.DistortionUVOffset[0]; Output.DistortionOffset[1] = Data.DistortionUVOffset[1]; Output.DistortionStrength = Data.DistortionStrength; }
		}
		// dynamic vertex bufferへ展開済み三角形を転送する。
		D3D11_MAPPED_SUBRESOURCE Mapped{};
		if ( FAILED( Context->Map( VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, Vertices.GetData(), static_cast<size_t>( Vertices.Num() ) * sizeof( FVertex ) ); Context->Unmap( VertexBuffer, 0 );
		// 頂点shaderへ渡すworld座標からclip座標への変換。
		FVertexConstants VsConstants{};
		FMemory::Memcpy( VsConstants.Wvp, &Wvp, sizeof( VsConstants.Wvp ) );
		// pixel shaderへ渡す元画像・mask・発光・ゆがみ設定。
		FPixelConstants PsConstants{};
		PsConstants.UvScaleOffset[0] = Data.Material.UVScale[0]; PsConstants.UvScaleOffset[1] = Data.Material.UVScale[1]; PsConstants.UvScaleOffset[2] = Data.UVOffset[0]; PsConstants.UvScaleOffset[3] = Data.UVOffset[1];
		PsConstants.Values[1] = static_cast<float32>( Data.Material.bDissolve ); PsConstants.Values[2] = Data.Material.MaskScale;
		PsConstants.Tint[0] = 1.0f; PsConstants.Tint[1] = 1.0f; PsConstants.Tint[2] = 1.0f; PsConstants.Tint[3] = 1.0f;
		PsConstants.Edge[0] = Data.Material.EdgeColor.R / 255.0f * Data.Material.EdgeColor.A / 255.0f * Data.Material.EdgeEmission; PsConstants.Edge[1] = Data.Material.EdgeColor.G / 255.0f * Data.Material.EdgeColor.A / 255.0f * Data.Material.EdgeEmission; PsConstants.Edge[2] = Data.Material.EdgeColor.B / 255.0f * Data.Material.EdgeColor.A / 255.0f * Data.Material.EdgeEmission; PsConstants.Edge[3] = Data.Material.EdgeWidth;
		PsConstants.BrightBlend[0] = 1.0f; PsConstants.BrightBlend[1] = 1.0f; PsConstants.BrightBlend[2] = 1.0f; PsConstants.BrightBlend[3] = 1.0f;
		// 呼び出し元のDrawBrightを各RGBへ掛ける。
		// callerの明度倍率を得るRGB値。
		int32 Bright[3] = { 255, 255, 255 };
		GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
		PsConstants.BrightBlend[0] *= Bright[0] / 255.0f; PsConstants.BrightBlend[1] *= Bright[1] / 255.0f; PsConstants.BrightBlend[2] *= Bright[2] / 255.0f;
		PsConstants.Options[0] = Data.Material.MaskTexture >= 0 ? 1.0f : 0.0f; PsConstants.Options[1] = BlendParam / 255.0f; PsConstants.Options[3] = 1.0f;
		FMemory::Memcpy( PsConstants.SourceRect, SourceRect, sizeof( SourceRect ) ); FMemory::Memcpy( PsConstants.SourceInset, SourceInset, sizeof( SourceInset ) ); FMemory::Memcpy( PsConstants.MaskRect, MaskRect, sizeof( MaskRect ) ); FMemory::Memcpy( PsConstants.MaskInset, MaskInset, sizeof( MaskInset ) );
		if ( bUseDistortion ) { PsConstants.DistortionScaleOffset[0] = Data.DistortionUVScale[0]; PsConstants.DistortionScaleOffset[1] = Data.DistortionUVScale[1]; PsConstants.DistortionValues[0] = Data.DistortionStrength; PsConstants.DistortionValues[1] = 1.0f; FMemory::Memcpy( PsConstants.DistortionRect, DistortionRect, sizeof( DistortionRect ) ); FMemory::Memcpy( PsConstants.DistortionInset, DistortionInset, sizeof( DistortionInset ) ); }
		if ( FAILED( Context->Map( VertexConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &VsConstants, sizeof( VsConstants ) ); Context->Unmap( VertexConstants, 0 );
		if ( FAILED( Context->Map( PixelConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped ) ) ) { Saved.Restore( Context ); return false; }
		FMemory::Memcpy( Mapped.pData, &PsConstants, sizeof( PsConstants ) ); Context->Unmap( PixelConstants, 0 );
		// native drawへ結ぶvertex buffer・constant buffer・SRVと合成係数。
		const UINT Stride = sizeof( FVertex ); const UINT Offset = 0; ID3D11Buffer* Vb = VertexBuffer; ID3D11Buffer* VsCb = VertexConstants; ID3D11Buffer* PsCb = PixelConstants; ID3D11ShaderResourceView* Srvs[3] = { const_cast<ID3D11ShaderResourceView*>( Source ), const_cast<ID3D11ShaderResourceView*>( Mask ), const_cast<ID3D11ShaderResourceView*>( Distortion ) }; const FLOAT BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Context->IASetInputLayout( Layout ); Context->IASetVertexBuffers( 0, 1, &Vb, &Stride, &Offset ); Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		Context->VSSetShader( VertexShader, nullptr, 0 ); Context->PSSetShader( PixelShader, nullptr, 0 ); Context->GSSetShader( nullptr, nullptr, 0 ); Context->HSSetShader( nullptr, nullptr, 0 ); Context->DSSetShader( nullptr, nullptr, 0 );
		Context->VSSetConstantBuffers( 0, 1, &VsCb ); Context->PSSetConstantBuffers( 0, 1, &PsCb ); Context->PSSetShaderResources( 0, bUseDistortion ? 3u : 2u, Srvs ); Context->PSSetSamplers( 0, 1, &Sampler );
		if ( Data.bTwoSided != 0 || bUseRegularCulling || bOverrideNativeCull )
		{
			// callerのraster設定を保ち、必要なmesh cullingだけ一時反映する。
			D3D11_RASTERIZER_DESC RasterDesc{};
			if ( Saved.Raster != nullptr ) Saved.Raster->GetDesc( &RasterDesc ); else { RasterDesc.FillMode = D3D11_FILL_SOLID; RasterDesc.DepthClipEnable = TRUE; }
			if ( Data.bTwoSided != 0 ) RasterDesc.CullMode = D3D11_CULL_NONE;
			else if ( bOverrideNativeCull )
			{
				if ( NativeCull == DX_CULLING_NONE ) RasterDesc.CullMode = D3D11_CULL_NONE;
				else if ( NativeCull == DX_CULLING_LEFT ) RasterDesc.CullMode = D3D11_CULL_BACK;
				else if ( NativeCull == DX_CULLING_RIGHT ) RasterDesc.CullMode = D3D11_CULL_FRONT;
				else { Saved.Restore( Context ); return false; }
			}
			else
			{
				// 論理的な面除去設定をGPUの方式へ対応させる。
				const int32 CallerCullMode = GetUseBackCulling();
				if ( CallerCullMode == DX_CULLING_NONE ) RasterDesc.CullMode = D3D11_CULL_NONE;
				else if ( CallerCullMode == DX_CULLING_LEFT ) RasterDesc.CullMode = D3D11_CULL_BACK;
				else if ( CallerCullMode == DX_CULLING_RIGHT ) RasterDesc.CullMode = D3D11_CULL_FRONT;
				else { Saved.Restore( Context ); return false; }
			}
			if ( !bRasterDescValid || FMemory::Memcmp( &RasterDescCache, &RasterDesc, sizeof( RasterDesc ) ) != 0 )
			{
				if ( Raster != nullptr ) Raster->Release();
				Raster = nullptr;
				if ( FAILED( Device->CreateRasterizerState( &RasterDesc, &Raster ) ) ) { bRasterDescValid = false; Saved.Restore( Context ); return false; }
				RasterDescCache = RasterDesc; bRasterDescValid = true;
			}
			Context->RSSetState( Raster );
		}
		Context->OMSetBlendState( BlendState, BlendFactor, 0xffffffffu ); Context->OMSetDepthStencilState( DepthStates[Data.Material.DepthTest | ( Data.Material.DepthWrite << 1 )], 0 ); Context->Draw( Vertices.Num(), 0 );
		Saved.Restore( Context );
		return true;
	}

	// 元mesh描画に必要なnative画像とUV変換が揃っているか事前確認する。
	bool CanDrawModelMesh( const FDrawMaterialMeshData& Data, const FMatrix4x4* World )
	{
		if ( !NDrawMaterialMesh::IsValidHeader( Data ) || !Initialize() || Context == nullptr ) return false;
		// 元画像へ接続するshader参照。
		const ID3D11ShaderResourceView* Source = GetTexture( Data.Texture, SourceView, SourceHandle, SourceTextureIdentity );
		if ( Source == nullptr ) return false;
		if ( Data.Material.MaskTexture >= 0 )
		{
			// mask画像へ接続するshader参照。
			const ID3D11ShaderResourceView* Mask = GetTexture( Data.Material.MaskTexture, MaskView, MaskHandle, MaskTextureIdentity );
			if ( Mask == nullptr ) return false;
		}
		if ( Data.DistortionStrength > 0.0f )
		{
			// 変位画像へ接続するshader参照。
			const ID3D11ShaderResourceView* Distortion = GetTexture( Data.DistortionTexture, DistortionView, DistortionHandle, DistortionTextureIdentity );
			if ( Distortion == nullptr ) return false;
		}
		// graph矩形と半texel内側の値。
		float32 Rect[4] = {}, Inset[4] = {};
		if ( !GetGraphUvMapping( Data.Texture, Rect, Inset ) ) return false;
		if ( Data.Material.MaskTexture >= 0 && !GetGraphUvMapping( Data.Material.MaskTexture, Rect, Inset ) ) return false;
		if ( Data.DistortionStrength > 0.0f && !GetGraphUvMapping( Data.DistortionTexture, Rect, Inset ) ) return false;
		// DxLibが保留した頂点をflushし、描画先とviewportをnativeへ反映する。
		RenderVertex();
		// DxLibが現在選んだ描画先。
		const int32 DrawScreen = GetDrawScreen();
		// 論理描画先に対応するnative target。
		ID3D11RenderTargetView* ExpectedTarget = DrawScreen == DX_SCREEN_BACK ? const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetUseDirect3D11BackBufferRenderTargetView() ) ) : const_cast<ID3D11RenderTargetView*>( static_cast<const ID3D11RenderTargetView*>( GetGraphID3D11RenderTargetView( DrawScreen ) ) );
		// native contextが参照するtarget。
		ID3D11RenderTargetView* CurrentTarget = nullptr;
		Context->OMGetRenderTargets( 1, &CurrentTarget, nullptr );
		// DxLibが選んだ描画範囲。
		RECT DrawArea{};
		GetDrawArea( &DrawArea );
		// native contextのviewport。
		D3D11_VIEWPORT CurrentViewport{};
		UINT ViewportCount = 1;
		Context->RSGetViewports( &ViewportCount, &CurrentViewport );
		const bool bTargetMatches = ExpectedTarget != nullptr && CurrentTarget == ExpectedTarget;
		const bool bViewportMatches = ViewportCount == 1 && CurrentViewport.TopLeftX == static_cast<float32>( DrawArea.left ) && CurrentViewport.TopLeftY == static_cast<float32>( DrawArea.top ) && CurrentViewport.Width == static_cast<float32>( DrawArea.right - DrawArea.left ) && CurrentViewport.Height == static_cast<float32>( DrawArea.bottom - DrawArea.top );
		if ( CurrentTarget != nullptr ) CurrentTarget->Release();
		if ( !bTargetMatches || !bViewportMatches || const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) ) != Context ) return false;
		// view以降と個体行列を合成したclip変換。
		MATRIX View{}, Projection{}, Viewport{}, ApiViewport{};
		GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetTransformToAPIViewportMatrix( &ApiViewport );
		MATRIX Wvp = MMult( MMult( MMult( View, Projection ), Viewport ), MInverse( ApiViewport ) );
		if ( World != nullptr ) Wvp = MMult( static_cast<MATRIX>( *World ), Wvp );
		for ( int32 Row = 0; Row < 4; ++Row ) for ( int32 Column = 0; Column < 4; ++Column ) if ( !NMath::IsFinite( Wvp.m[Row][Column] ) ) return false;
		return true;
	}

	// 元モデルのローカル形状を個体行列とalpha倍率で描く。
	bool DrawModelMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* SourceVertices, const uint32* Indices, const FMatrix4x4& World, int32 NativeCull, float32 Opacity )
	{
		if ( SourceVertices == nullptr || Indices == nullptr || !NMath::IsFinite( Opacity ) || Opacity < 0.0f || Opacity > 1.0f || ( NativeCull != DX_CULLING_LEFT && NativeCull != DX_CULLING_RIGHT && NativeCull != DX_CULLING_NONE ) ) return false;
		return DrawMeshInternal( Data, SourceVertices, Indices, false, &World, NativeCull, Opacity, true );
	}

	// 従来mesh命令を既存の頂点・world状態のまま描く。
	bool DrawMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* SourceVertices, const uint32* Indices, bool bUseRegularCulling )
	{
		return DrawMeshInternal( Data, SourceVertices, Indices, bUseRegularCulling, nullptr, 0, 1.0f, false );
	}
};

FEffectMaterialRenderer::FEffectMaterialRenderer() : Impl( new FImpl() ) {}
FEffectMaterialRenderer::~FEffectMaterialRenderer() { Deinitialize(); delete Impl; Impl = nullptr; }
bool FEffectMaterialRenderer::Initialize() { return Impl != nullptr && Impl->Initialize(); }
bool FEffectMaterialRenderer::Draw( const FDrawMaterialQuadData& Data ) { return Impl != nullptr && Impl->Draw( Data ); }
bool FEffectMaterialRenderer::DrawDistorted( const FDrawDistortedMaterialQuadData& Data ) { return Impl != nullptr && Impl->DrawDistorted( Data ); }
bool FEffectMaterialRenderer::DrawRibbon( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points ) { return Impl != nullptr && Impl->DrawRibbon( Data, Points ); }
bool FEffectMaterialRenderer::DrawDistortedRibbon( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points ) { return Impl != nullptr && Impl->DrawDistortedRibbon( Data, Points ); }
void FEffectMaterialRenderer::Deinitialize() { if ( Impl != nullptr ) Impl->Deinitialize(); }
bool FEffectMaterialRenderer::IsAvailable() const { return Impl != nullptr && Impl->VertexShader != nullptr; }
bool FEffectMaterialRenderer::IsRibbonAvailable() const { return Impl != nullptr && Impl->VertexShader != nullptr && Impl->PixelShader != nullptr && Impl->Layout != nullptr && Impl->VertexBuffer != nullptr; }
bool FEffectMaterialRenderer::IsUVDistortionAvailable() const { return Impl != nullptr && Impl->VertexShader != nullptr && Impl->PixelShader != nullptr && Impl->Layout != nullptr && Impl->VertexBuffer != nullptr && Impl->VertexConstants != nullptr && Impl->PixelConstants != nullptr && Impl->Sampler != nullptr && Impl->AlphaBlend != nullptr && Impl->AddBlend != nullptr && Impl->DepthStates[0] != nullptr && Impl->DepthStates[1] != nullptr && Impl->DepthStates[2] != nullptr && Impl->DepthStates[3] != nullptr; }
bool FEffectMaterialRenderer::IsRibbonUVDistortionAvailable() const { return IsRibbonAvailable() && IsUVDistortionAvailable(); }
bool FEffectMaterialRenderer::DrawMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, bool bUseRegularCulling ) { return Impl != nullptr && Impl->DrawMesh( Data, Vertices, Indices, bUseRegularCulling ); }
bool FEffectMaterialRenderer::CanDrawModelMesh( const FDrawMaterialMeshData& Data, const FMatrix4x4* World ) { return Impl != nullptr && Impl->CanDrawModelMesh( Data, World ); }
bool FEffectMaterialRenderer::DrawModelMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, const FMatrix4x4& World, int32 NativeCull, float32 Opacity ) { return Impl != nullptr && Impl->DrawModelMesh( Data, Vertices, Indices, World, NativeCull, Opacity ); }
bool FEffectMaterialRenderer::IsMeshAvailable() const { return IsRibbonAvailable() && IsUVDistortionAvailable(); }
