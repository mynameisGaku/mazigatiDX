// SPDX-License-Identifier: MIT
#include "DX12ForwardMeshRenderer.h"
#include "MazigatiEngineShaderSources.generated.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace
{
	// 置いておくメッシュの上限（バイト）と、使われないまま置いておく描画の回数。
	constexpr uint64 MaxMeshBytes = 512ull * 1024ull * 1024ull;
	constexpr uint64 IdleDraws = 8192;
	constexpr int32 MaxVertices = 65536;
	constexpr int32 MaxIndices = 196608;

	bool CompileEntry( const char* Entry, const char* Target, TArray<uint8>& Out )
	{
		const char* Source = NEngineShaderSources::DX12_ForwardMesh; // 資源管理とシェーダーの式を分離する。
		Microsoft::WRL::ComPtr<ID3DBlob> Code, Errors;
		const HRESULT Result = D3DCompile( Source, NCString::Length( Source ), "ForwardMesh", nullptr, nullptr, Entry, Target, 0, 0, &Code, &Errors );
		if ( FAILED( Result ) || !Code )
		{
			LOG( LogDX12, Error, "CDX12ForwardMeshRenderer: the forward shader '%s' cannot be compiled: %s", Entry, Errors ? static_cast<const char*>( Errors->GetBufferPointer() ) : "unknown error" );
			return false;
		}
		Out.Reset();
		Out.Append( static_cast<const uint8*>( Code->GetBufferPointer() ), static_cast<int32>( Code->GetBufferSize() ) );
		return true;
	}

	bool IsFiniteMatrix( const float32* Values )
	{
		for ( int32 Index = 0; Index < 16; ++Index )
		{
			if ( !NMath::IsFinite( Values[Index] ) )
			{
				return false;
			}
		}
		return true;
	}

	// 個体の定数（b0）。並びは、シェーダーの cbObject と同じ。
	struct FObjectConstants
	{
		float32 Clip[16];
		float32 World[16];
		float32 Normal[16];
		float32 Params[4];
		float32 UvTransform[8]; // HLSLの2つのfloat4と同じ並び。
		float32 Fog[20]; // カメラの奥行きと距離・高さフォグ。
	};
}

void CDX12ForwardMeshRenderer::Initialize( IGraphicsPipelineResources* InPipelines, IRenderBufferResources* InBuffers, CTextureAssetStore* InTextures )
{
	if ( Pipelines )
	{
		return; // 二重の初期化は、何もしない。
	}
	Pipelines = InPipelines;
	Buffers = InBuffers;
	Textures = InTextures;
}

void CDX12ForwardMeshRenderer::Shutdown()
{
	for ( auto& Pair : Meshes )
	{
		ReleaseMesh( Pair.Value );
	}
	Meshes.Reset();
	if ( Pipelines )
	{
		for ( const auto& Pair : PipelineCache )
		{
			if ( Pair.Value.IsValid() )
			{
				Pipelines->ReleaseGraphicsPipeline( Pair.Value );
			}
		}
	}
	PipelineCache.Reset();
	VertexCode.Reset();
	PixelCode.Reset();
	TexturedPixelCode.Reset();
	bShadersTried = false;
	bShadersReady = false;
	TotalBytes = 0;
	Pipelines = nullptr;
	Buffers = nullptr;
	Textures = nullptr;
}

bool CDX12ForwardMeshRenderer::BuildShaders()
{
	if ( !bShadersTried )
	{
		bShadersTried = true;
		bShadersReady = CompileEntry( "ForwardVS", "vs_5_0", VertexCode ) && CompileEntry( "ForwardPS", "ps_5_0", PixelCode ) && CompileEntry( "ForwardTexturedPS", "ps_5_0", TexturedPixelCode );
	}
	return bShadersReady;
}

void CDX12ForwardMeshRenderer::ReleaseMesh( FMesh& Mesh )
{
	if ( Buffers )
	{
		if ( Mesh.Vertices.IsValid() )
		{
			Buffers->ReleaseBuffer( Mesh.Vertices );
		}
		if ( Mesh.Indices.IsValid() )
		{
			Buffers->ReleaseBuffer( Mesh.Indices );
		}
	}
	TotalBytes -= NMath::Min( TotalBytes, Mesh.Bytes );
	Mesh = FMesh();
}

void CDX12ForwardMeshRenderer::Purge( uint64 Keep )
{
	TArray<uint64> Remove;
	for ( const auto& Pair : Meshes )
	{
		if ( Pair.Key != Keep && Clock - Pair.Value.LastUsed > IdleDraws )
		{
			Remove.Add( Pair.Key );
		}
	}
	// 期限切れの解放を先に反映してから、上限の超過を判定する。
	for ( const uint64 Key : Remove )
	{
		if ( FMesh* Mesh = Meshes.Find( Key ) )
		{
			ReleaseMesh( *Mesh );
			Meshes.Remove( Key );
		}
	}
	Remove.Reset();
	while ( TotalBytes > MaxMeshBytes ) // 上限を超えたら、使っていないものから先に手放す（描いているメッシュは、残す）。
	{
		uint64 Oldest = 0;
		uint64 OldestUsed = TNumericLimits<uint64>::Max();
		for ( const auto& Pair : Meshes )
		{
			if ( Pair.Key != Keep && !Remove.Contains( Pair.Key ) && Pair.Value.LastUsed < OldestUsed )
			{
				Oldest = Pair.Key;
				OldestUsed = Pair.Value.LastUsed;
			}
		}
		if ( OldestUsed == TNumericLimits<uint64>::Max() )
		{
			break;
		}
		Remove.Add( Oldest );
		TotalBytes -= NMath::Min( TotalBytes, Meshes[Oldest].Bytes );
		Meshes[Oldest].Bytes = 0;
	}
	for ( const uint64 Key : Remove )
	{
		if ( FMesh* Mesh = Meshes.Find( Key ) )
		{
			ReleaseMesh( *Mesh );
			Meshes.Remove( Key );
		}
	}
}

const CDX12ForwardMeshRenderer::FMesh* CDX12ForwardMeshRenderer::AcquireMesh( const FForwardMeshDraw& Draw )
{
	if ( FMesh* Found = Meshes.Find( Draw.MeshKey ) )
	{
		if ( Found->Version == Draw.MeshVersion && Found->NumVertices == Draw.NumVertices && Found->NumIndices == Draw.NumIndices )
		{
			Found->LastUsed = Clock;
			return Found;
		}
		ReleaseMesh( *Found ); // 中身が変わった。置き直す。
		Meshes.Remove( Draw.MeshKey );
	}
	FMesh Mesh;
	Mesh.Vertices = Buffers->CreateBuffer( TArrayView<const uint8>( reinterpret_cast<const uint8*>( Draw.Vertices ), static_cast<int32>( static_cast<usize>( Draw.NumVertices ) * sizeof( FDrawVertex3D ) ) ), ERenderBufferShaderAccess::None );
	Mesh.Indices = Buffers->CreateBuffer( TArrayView<const uint8>( reinterpret_cast<const uint8*>( Draw.Indices ), static_cast<int32>( static_cast<usize>( Draw.NumIndices ) * sizeof( uint32 ) ) ), ERenderBufferShaderAccess::None );
	if ( !Mesh.Vertices.IsValid() || !Mesh.Indices.IsValid() )
	{
		ReleaseMesh( Mesh );
		return nullptr;
	}
	Mesh.Version = Draw.MeshVersion;
	Mesh.NumVertices = Draw.NumVertices;
	Mesh.NumIndices = Draw.NumIndices;
	Mesh.Bytes = static_cast<uint64>( Draw.NumVertices ) * sizeof( FDrawVertex3D ) + static_cast<uint64>( Draw.NumIndices ) * sizeof( uint32 );
	Mesh.LastUsed = Clock;
	TotalBytes += Mesh.Bytes;
	Meshes.Add( Draw.MeshKey, Mesh );
	Purge( Draw.MeshKey );
	return Meshes.Find( Draw.MeshKey );
}

FGraphicsPipelineHandle CDX12ForwardMeshRenderer::GetPipeline( const FForwardMeshDraw& Draw, bool bTextured, ERenderTargetColorFormat TargetFormat, EComputeTextureFormat TextureFormat )
{
	const FGeometryDrawState& State = Draw.State;
	const FGeometryCustomBlend& Custom = State.CustomBlend;
	const bool bCustom = State.bAlphaBlend && State.Blend == EGeometryBlend::Custom; // 自分で決めた係数のときだけ、係数も鍵に入れる。
	const FString Key = FString::Printf( "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d,%d,%d,%d,%d,%d", bTextured ? 1 : 0, static_cast<int32>( TargetFormat ), static_cast<int32>( TextureFormat ), State.bTestDepth ? 1 : 0, State.bWriteDepth ? 1 : 0,
		static_cast<int32>( State.Compare ), static_cast<int32>( State.Culling ), State.bAlphaBlend ? 1 : 0, State.bAlphaBlend ? static_cast<int32>( State.Blend ) : 0, static_cast<int32>( State.AddressU ), State.bLinearFilter ? 1 : 0,
		bCustom ? static_cast<int32>( Custom.SrcColor ) : 0, bCustom ? static_cast<int32>( Custom.DestColor ) : 0, bCustom ? static_cast<int32>( Custom.ColorOp ) : 0, bCustom ? static_cast<int32>( Custom.SrcAlpha ) : 0, bCustom ? static_cast<int32>( Custom.DestAlpha ) : 0,
		bCustom ? static_cast<int32>( Custom.AlphaOp ) : 0 );
	if ( const FGraphicsPipelineHandle* Found = PipelineCache.Find( Key ) )
	{
		return *Found; // 作れなかった組み合わせは、無効なまま覚えて、毎回作り直さない。
	}
	// 頂点の並びは FDrawVertex3D（48 バイト）。
	const FGraphicsInputElement Elements[5] = {
		{ "POSITION", 0, EGraphicsVertexAttributeFormat::Float3, 0 },
		{ "NORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 12 },
		{ "COLOR", 0, EGraphicsVertexAttributeFormat::UByte4Norm, 24 },
		{ "COLOR", 1, EGraphicsVertexAttributeFormat::UByte4Norm, 28 },
		{ "TEXCOORD", 0, EGraphicsVertexAttributeFormat::Float2, 32 },
	};
	FGraphicsBufferSlot BufferSlots[2];
	BufferSlots[0].Binding = 0;
	BufferSlots[0].Layout = EGraphicsBufferLayout::Constant;
	BufferSlots[1].Binding = 6;
	BufferSlots[1].Layout = EGraphicsBufferLayout::Constant;
	FGraphicsTextureSlot TextureSlot;
	TextureSlot.Binding = 0;
	TextureSlot.Format = TextureFormat;
	const TArray<uint8>& Pixel = bTextured ? TexturedPixelCode : PixelCode;
	FGraphicsPipelineDesc Desc;
	Desc.VertexShaderBytecode = TArrayView<const uint8>( VertexCode.GetData(), VertexCode.Num() );
	Desc.PixelShaderBytecode = TArrayView<const uint8>( Pixel.GetData(), Pixel.Num() );
	Desc.VertexStride = sizeof( FDrawVertex3D );
	Desc.InputElements = TArrayView<const FGraphicsInputElement>( Elements, 5 );
	Desc.BufferSlots = TArrayView<const FGraphicsBufferSlot>( BufferSlots, 2 );
	Desc.TextureSlots = bTextured ? TArrayView<const FGraphicsTextureSlot>( &TextureSlot, 1 ) : TArrayView<const FGraphicsTextureSlot>();
	Desc.NumRenderTargets = 1;
	Desc.RenderTargetFormats[0] = TargetFormat;
	Desc.DepthFormat = EGraphicsDepthFormat::D32Float;
	Desc.Topology = EGeometryTopology::Triangles;
	Desc.DepthCompare = State.Compare;
	Desc.Culling = State.Culling;
	Desc.SamplerAddress = State.AddressU;
	Desc.bDepthTest = State.bTestDepth;
	Desc.bDepthWrite = State.bTestDepth && State.bWriteDepth;
	Desc.bAlphaBlend = State.bAlphaBlend;
	Desc.Blend = State.Blend;
	Desc.CustomBlend = State.CustomBlend;
	Desc.bLinearSampler = State.bLinearFilter;
	const FGraphicsPipelineHandle Handle = Pipelines->CreateGraphicsPipeline( Desc );
	if ( !Handle.IsValid() )
	{
		LOG( LogDX12, Warning, "CDX12ForwardMeshRenderer: the forward pipeline cannot be created" );
	}
	PipelineCache.Add( Key, Handle );
	return Handle;
}

bool CDX12ForwardMeshRenderer::Draw( const FForwardMeshDraw& Draw, IRenderGeometry3D& Geometry )
{
	const FGeometryDrawState& State = Draw.State;
	if ( !Pipelines || !Buffers || !Textures || ( !Draw.bTransient && Draw.MeshKey == 0 ) || !Draw.Vertices || ( !Draw.Indices && !Draw.bTransient ) || Draw.NumVertices <= 0 || Draw.NumVertices > MaxVertices || Draw.NumIndices <= 0 || Draw.NumIndices % 3 != 0
		|| Draw.FirstIndex < 0 || Draw.IndexCount <= 0 || Draw.IndexCount > MaxIndices || Draw.IndexCount % 3 != 0 || Draw.FirstIndex % 3 != 0 || static_cast<int64>( Draw.FirstIndex ) + Draw.IndexCount > Draw.NumIndices )
	{
		return false;
	}
	if ( State.Mask.IsActive() || State.MaxAnisotropy > 1 || State.DepthBias != 0 || State.AddressU != State.AddressV || State.Topology != EGeometryTopology::Triangles
		|| ( State.bAlphaBlend && ( State.Blend == EGeometryBlend::Invert || State.Blend == EGeometryBlend::MultiplyAlpha ) ) )
	{
		return false; // この経路で表せない状態（呼び出し側が、CPU の経路で描く）。
	}
	if ( !IsFiniteMatrix( Draw.ClipMatrix ) || !IsFiniteMatrix( Draw.World ) || !IsFiniteMatrix( Draw.NormalMatrix ) || !NMath::IsFinite( Draw.AlphaScale ) )
	{
		return false;
	}
	for ( const float32 Value : Draw.Fog )
	{
		if ( !NMath::IsFinite( Value ) ) { return false; }
	}
	for ( const int32 Offset : { 7, 15 } )
	{
		const float32 Mode = Draw.Fog[Offset]; // 命令で定義された4つの方式だけ受け付ける。
		if ( Mode != 0.0f && Mode != 1.0f && Mode != 2.0f && Mode != 3.0f )
		{
			return false;
		}
	}
	for ( const float32 Value : Draw.UvTransform )
	{
		if ( !NMath::IsFinite( Value ) )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < FForwardMeshDraw::NumLightFloats; ++Index )
	{
		if ( !NMath::IsFinite( Draw.Lighting[Index] ) )
		{
			return false;
		}
	}
	if ( !Draw.Indices && ( Draw.FirstIndex != 0 || Draw.IndexCount != Draw.NumVertices || Draw.NumIndices != Draw.NumVertices ) )
	{
		return false; // 添字なしは頂点全体を順番に描く場合だけ。
	}
	for ( int32 Index = 0; Draw.Indices && Index < Draw.NumIndices; ++Index )
	{
		if ( Draw.Indices[Index] >= static_cast<uint32>( Draw.NumVertices ) )
		{
			return false; // 置く前に、全部の添字を確かめる。
		}
	}
	if ( !BuildShaders() )
	{
		return false;
	}
	FTextureResourceHandle Texture;
	EComputeTextureFormat TextureFormat = EComputeTextureFormat::RGBA8;
	const bool bTextured = Draw.Texture >= 0;
	if ( bTextured && ( !Textures->GetResource( Draw.Texture, Texture ) || !Textures->GetShaderFormat( Draw.Texture, TextureFormat ) ) )
	{
		return false;
	}
	++Clock;
	const FGraphicsPipelineHandle Pipeline = GetPipeline( Draw, bTextured, Geometry.GetColorFormat(), TextureFormat );
	if ( !Pipeline.IsValid() )
	{
		return false;
	}
	const FMesh* Mesh = Draw.bTransient ? nullptr : AcquireMesh( Draw );
	if ( !Draw.bTransient && !Mesh )
	{
		return false;
	}
	FObjectConstants Object;
	FMemory::Memcpy( Object.Clip, Draw.ClipMatrix, sizeof( Object.Clip ) );
	FMemory::Memcpy( Object.World, Draw.World, sizeof( Object.World ) );
	FMemory::Memcpy( Object.Normal, Draw.NormalMatrix, sizeof( Object.Normal ) );
	FMemory::Memcpy( Object.UvTransform, Draw.UvTransform, sizeof( Object.UvTransform ) );
	Object.Params[0] = Draw.bLit ? 1.0f : 0.0f;
	Object.Params[1] = Draw.AlphaScale;
	Object.Params[2] = Draw.bPixelLighting && Draw.bLit && Draw.Fog[7] == 0 && Draw.Fog[15] == 0 ? 1.0f : 0.0f;
	Object.Params[3] = bTextured ? 1.0f : 0.0f;
	FMemory::Memcpy( Object.Fog, Draw.Fog, sizeof( Object.Fog ) );
	FGraphicsConstantBytes Constants[2];
	Constants[0].Data = reinterpret_cast<const uint8*>( &Object );
	Constants[0].Size = sizeof( Object );
	Constants[1].Data = reinterpret_cast<const uint8*>( Draw.Lighting );
	Constants[1].Size = sizeof( Draw.Lighting );
	if ( Draw.bTransient )
	{
		FGraphicsPipelineTransientDraw Transient; // 借用値は既存のフレーム転送領域へコピーされる。
		Transient.Pipeline = Pipeline;
		Transient.Vertices = Draw.Vertices;
		Transient.VertexCount = Draw.NumVertices;
		Transient.Indices = Draw.Indices ? Draw.Indices + Draw.FirstIndex : nullptr;
		Transient.IndexCount = Draw.Indices ? Draw.IndexCount : 0;
		Transient.Constants = TArrayView<const FGraphicsConstantBytes>( Constants, 2 );
		Transient.Textures = bTextured ? TArrayView<const FTextureResourceHandle>( &Texture, 1 ) : TArrayView<const FTextureResourceHandle>();
		return Geometry.DrawGraphicsPipelineTransient( Transient );
	}
	FGraphicsPipelineDraw Command;
	Command.Pipeline = Pipeline;
	Command.VertexBuffer = Mesh->Vertices;
	Command.VertexOffset = 0;
	Command.VertexCount = static_cast<uint32>( Mesh->NumVertices );
	Command.IndexBuffer = Mesh->Indices;
	Command.IndexOffset = static_cast<uint64>( Draw.FirstIndex ) * sizeof( uint32 );
	Command.IndexCount = static_cast<uint32>( Draw.IndexCount );
	Command.IndexFormat = EGeometryIndexFormat::UInt32;
	Command.Constants = TArrayView<const FGraphicsConstantBytes>( Constants, 2 );
	Command.Textures = bTextured ? TArrayView<const FTextureResourceHandle>( &Texture, 1 ) : TArrayView<const FTextureResourceHandle>();
	const bool bDrawn = Geometry.DrawGraphicsPipeline( Command );
	if ( !bDrawn )
	{
		LOG( LogDX12, Warning, "CDX12ForwardMeshRenderer: the draw is rejected by the GPU path (mesh %llu)", Draw.MeshKey );
	}
	return bDrawn;
}
