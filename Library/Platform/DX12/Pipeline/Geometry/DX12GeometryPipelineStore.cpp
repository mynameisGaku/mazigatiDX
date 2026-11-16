// SPDX-License-Identifier: MIT
#include "DX12GeometryPipelineStore.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Platform/DX12/Pipeline/DX12BlendState.h"

bool CDX12GeometryPipelineStore::Initialize( ID3D12Device *InDevice )
{
	if ( Device || !InDevice || FAILED( InDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Device = InDevice;
	const char Source[] = "#if defined(INVERT)\n#define FIN(c) float4(1.0-(c).rgb,(c).a)\n#elif defined(MULA)\n#define FIN(c) float4(lerp(float3(1.0,1.0,1.0),(c).rgb,(c).a),(c).a)\n#else\n#define FIN(c) (c)\n#endif\n"
		"struct V { float4 p:POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; struct P { float4 p:SV_POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; P VS(V v) { P p; p.p=v.p; p.c=v.c; p.uv=v.uv; return p; } float4 PS(P p):SV_TARGET { return FIN(p.c); } Texture2D image:register(t0); SamplerState sampling:register(s0); float4 TexturePS(P p):SV_TARGET { float4 color=image.Sample(sampling,p.uv)*p.c; if(color.a<=0) discard; return FIN(color); }"
		"Texture2D maskImage:register(t1); cbuffer cbMask:register(b0) { uint4 maskInfo; }; "
		"float MaskValue(float4 position) { uint w,h; maskImage.GetDimensions(w,h); uint2 q=uint2(position.xy); if(q.x>=w||q.y>=h) { return 1.0; } float4 t=maskImage.Load(int3(q,0)); float m=maskInfo.x==0?t.a:(maskInfo.x==1?t.r:(maskInfo.x==2?t.g:t.b)); return maskInfo.y!=0?1.0-m:m; } "
		"float4 MaskedPS(P p):SV_TARGET { float4 color=p.c; color.a*=MaskValue(p.p); return FIN(color); } "
		"float4 MaskedTexturePS(P p):SV_TARGET { float4 color=image.Sample(sampling,p.uv)*p.c; color.a*=MaskValue(p.p); if(color.a<=0) discard; return FIN(color); }"; // 透視補間はGPUが行う。完全な透明画素は色・深度のどちらも書かない。マスクは、描画先の画素の座標で読む（画像の外は薄めない）。
	if ( FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VertexShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &PixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "TexturePS", "ps_5_0", 0, 0, &TexturedPixelShader, nullptr ) )
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "MaskedPS", "ps_5_0", 0, 0, &MaskedPixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "MaskedTexturePS", "ps_5_0", 0, 0, &MaskedTexturedPixelShader, nullptr ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	const D3D_SHADER_MACRO Invert[] = { { "INVERT", "1" }, { nullptr, nullptr } }; // 描く色を反転する版。
	if ( FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", Invert, nullptr, "PS", "ps_5_0", 0, 0, &InvertedPixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", Invert, nullptr, "TexturePS", "ps_5_0", 0, 0, &InvertedTexturedPixelShader, nullptr ) )
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", Invert, nullptr, "MaskedPS", "ps_5_0", 0, 0, &InvertedMaskedPixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", Invert, nullptr, "MaskedTexturePS", "ps_5_0", 0, 0, &InvertedMaskedTexturedPixelShader, nullptr ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	const D3D_SHADER_MACRO MultiplyAlpha[] = { { "MULA", "1" }, { nullptr, nullptr } }; // 色へアルファを掛けて乗算する版。
	if ( FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", MultiplyAlpha, nullptr, "PS", "ps_5_0", 0, 0, &MultiplyAlphaPixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", MultiplyAlpha, nullptr, "TexturePS", "ps_5_0", 0, 0, &MultiplyAlphaTexturedPixelShader, nullptr ) )
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", MultiplyAlpha, nullptr, "MaskedPS", "ps_5_0", 0, 0, &MultiplyAlphaMaskedPixelShader, nullptr ) ) || FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", MultiplyAlpha, nullptr, "MaskedTexturePS", "ps_5_0", 0, 0, &MultiplyAlphaMaskedTexturedPixelShader, nullptr ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	D3D12_DESCRIPTOR_RANGE Ranges[3] = {}; // 画像と補間を独立したheapから参照する。3つ目は、マスクの画像（t1）。
	Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	Ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	Ranges[2].BaseShaderRegister = 1;
	D3D12_ROOT_PARAMETER Parameters[4] = {}; // t0・s0・t1をピクセルシェーダーへ渡し、4つ目は、マスクの値（b0）。
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		Ranges[Index].NumDescriptors = 1;
		Parameters[Index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[Index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		Parameters[Index].DescriptorTable.NumDescriptorRanges = 1;
		Parameters[Index].DescriptorTable.pDescriptorRanges = &Ranges[Index];
	}
	Parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	Parameters[3].Constants.Num32BitValues = 4;
	D3D12_ROOT_SIGNATURE_DESC RootDesc = {}; // 画像なしのPSではdescriptorを参照しない。
	RootDesc.NumParameters = 4;
	RootDesc.pParameters = Parameters;
	RootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	Microsoft::WRL::ComPtr<ID3DBlob> Serialized; // GPUへ渡す設定。
	if ( FAILED( D3D12SerializeRootSignature( &RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &Serialized, nullptr ) ) || FAILED( Device->CreateRootSignature( 0, Serialized->GetBufferPointer(), Serialized->GetBufferSize(), IID_PPV_ARGS( &RootSignature ) ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}

	return true;
}

ID3D12RootSignature *CDX12GeometryPipelineStore::GetRootSignature() const
{
	return RootSignature.Get();
}

void CDX12GeometryPipelineStore::ResetAfterGPUIdle()
{
	Pipelines.Reset();
	MultiplyAlphaMaskedTexturedPixelShader.Reset();
	MultiplyAlphaMaskedPixelShader.Reset();
	MultiplyAlphaTexturedPixelShader.Reset();
	MultiplyAlphaPixelShader.Reset();
	InvertedMaskedTexturedPixelShader.Reset();
	InvertedMaskedPixelShader.Reset();
	InvertedTexturedPixelShader.Reset();
	InvertedPixelShader.Reset();
	MaskedTexturedPixelShader.Reset();
	MaskedPixelShader.Reset();
	TexturedPixelShader.Reset();
	PixelShader.Reset();
	VertexShader.Reset();
	RootSignature.Reset();
	Device.Reset();
}

ID3D12PipelineState *CDX12GeometryPipelineStore::GetOrCreatePipeline( const FGeometryDrawState &State, bool bTextured )
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) || State.Topology > EGeometryTopology::Triangles || State.Compare > EGeometryCompare::Always || State.Culling > EGeometryCulling::FrontFaces || State.AddressU > EGeometryTextureAddress::Border || State.AddressV > EGeometryTextureAddress::Border || State.Blend > EGeometryBlend::Custom || ( State.bAlphaBlend && State.Blend == EGeometryBlend::Custom && !State.CustomBlend.IsValid() ) )
	{
		return nullptr;
	}
	FGeometryPipelineKey Key; // PSOを決める全要素を含める。補間はsamplerで指定するためPSOに入れない。
	Key.State = ( static_cast<uint64>( static_cast<uint32>( State.DepthBias ) ) << 32 ) | static_cast<uint64>( State.Topology ) | ( static_cast<uint64>( State.Compare ) << 2 ) | ( static_cast<uint64>( State.Culling ) << 5 ) | ( static_cast<uint64>( State.bTestDepth ) << 7 ) | ( static_cast<uint64>( State.bWriteDepth ) << 8 ) | ( static_cast<uint64>( State.bAlphaBlend ) << 9 ) | ( static_cast<uint64>( bTextured ) << 10 ) | ( static_cast<uint64>( State.bAlphaBlend ? State.Blend : EGeometryBlend::Alpha ) << 11 ) | ( static_cast<uint64>( State.Mask.IsActive() ) << 15 );
	Key.Custom = State.bAlphaBlend && State.Blend == EGeometryBlend::Custom ? State.CustomBlend.Pack() : 0;
	if ( const auto *Found = Pipelines.Find( Key ) )
	{
		return Found->Resource.Get();
	}
	if ( Pipelines.Num() >= 128 )
	{
		return nullptr;
	}
	const D3D12_INPUT_ELEMENT_DESC Elements[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } }; // 共通頂点の配置。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = {}; // 状態を呼び出し単位で固定する。
	Desc.pRootSignature = RootSignature.Get();
	Desc.VS = { VertexShader->GetBufferPointer(), VertexShader->GetBufferSize() };
	const bool bMasked = State.Mask.IsActive(); // マスクの有無でもPSを分ける。
	const bool bInverted = State.bAlphaBlend && State.Blend == EGeometryBlend::Invert;
	const bool bMultiplyAlpha = State.bAlphaBlend && State.Blend == EGeometryBlend::MultiplyAlpha; // 色の側でアルファを掛ける版。 // 色の反転の有無でもPSを分ける（Blend は鍵に入っているので、PSOも分かれる）。
	ID3DBlob *Pixel = bMultiplyAlpha ? ( bMasked ? ( bTextured ? MultiplyAlphaMaskedTexturedPixelShader.Get() : MultiplyAlphaMaskedPixelShader.Get() ) : ( bTextured ? MultiplyAlphaTexturedPixelShader.Get() : MultiplyAlphaPixelShader.Get() ) )
		: bInverted ? ( bMasked ? ( bTextured ? InvertedMaskedTexturedPixelShader.Get() : InvertedMaskedPixelShader.Get() ) : ( bTextured ? InvertedTexturedPixelShader.Get() : InvertedPixelShader.Get() ) )
		: ( bMasked ? ( bTextured ? MaskedTexturedPixelShader.Get() : MaskedPixelShader.Get() ) : ( bTextured ? TexturedPixelShader.Get() : PixelShader.Get() ) ); // 画像の有無でPSを固定する。
	Desc.PS = { Pixel->GetBufferPointer(), Pixel->GetBufferSize() };
	Desc.InputLayout = { Elements, 3 };
	Desc.SampleMask = UINT_MAX;
	Desc.PrimitiveTopologyType = State.Topology == EGeometryTopology::Points  ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
								 : State.Topology == EGeometryTopology::Lines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
																			  : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	Desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.RasterizerState.CullMode = State.Culling == EGeometryCulling::BackFaces	? D3D12_CULL_MODE_BACK
									: State.Culling == EGeometryCulling::FrontFaces ? D3D12_CULL_MODE_FRONT
																					: D3D12_CULL_MODE_NONE;
	Desc.RasterizerState.DepthClipEnable = TRUE;
	Desc.RasterizerState.DepthBias = State.DepthBias;
	Desc.DepthStencilState.DepthEnable = State.bTestDepth || State.bWriteDepth;
	Desc.DepthStencilState.DepthWriteMask = State.bWriteDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	Desc.DepthStencilState.DepthFunc = State.bTestDepth ? static_cast<D3D12_COMPARISON_FUNC>( static_cast<int32>( State.Compare ) + 1 ) : D3D12_COMPARISON_FUNC_ALWAYS;
	Desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	Desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	Desc.DepthStencilState.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
	Desc.DepthStencilState.BackFace = Desc.DepthStencilState.FrontFace;
	NDX12BlendState::Configure( Desc.BlendState.RenderTarget[0], State.bAlphaBlend, State.Blend, State.CustomBlend ); // 混ぜ方は、利用者のシェーダーと同じ表。
	Desc.NumRenderTargets = 1;
	Desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	Desc.SampleDesc.Count = 1;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline; // 失敗時はキャッシュを変更しない。
	if ( FAILED( Device->CreateGraphicsPipelineState( &Desc, IID_PPV_ARGS( &Pipeline ) ) ) )
	{
		return nullptr;
	}
	ID3D12PipelineState *Result = Pipeline.Get(); // キャッシュへ所有権を渡す前の借用値。
	Pipelines.Add( Key, FPipelineRecord{ MoveTemp( Pipeline ) } );
	return Result;
}
