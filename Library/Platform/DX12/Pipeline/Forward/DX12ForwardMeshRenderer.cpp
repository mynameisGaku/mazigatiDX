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

	bool CompileEntry( const char* Entry, const char* Target, TArray<uint8>& Out, bool bSkinned = false )
	{
		const char* Source = NEngineShaderSources::DX12_ForwardMesh; // 資源管理とシェーダーの式を分離する。
		const D3D_SHADER_MACRO Skinning[] = { { "SKINNED", "1" }, { nullptr, nullptr } }; // GPU スキニング（骨の行列で頂点を変形する）の頂点シェーダー。
		Microsoft::WRL::ComPtr<ID3DBlob> Code, Errors;
		const HRESULT Result = D3DCompile( Source, NCString::Length( Source ), "ForwardMesh", bSkinned ? Skinning : nullptr, nullptr, Entry, Target, 0, 0, &Code, &Errors );
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
		float32 LightViewProjection[16]; // 影を受けるときの、光のクリップ座標への行列。
		float32 ShadowParams[4]; // 影の絵の幅・高さ・補正の深さ・影を受けるスロットの印。
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
	bLastPipelineValid = false;
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
	ShadowPixelCode.Reset();
	ShadowTexturedPixelCode.Reset();
	DepthVertexCode.Reset();
	DepthPixelCode.Reset();
	SkinnedVertexCode.Reset();
	SkinnedDepthVertexCode.Reset();
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
		bShadersReady = CompileEntry( "ForwardVS", "vs_5_0", VertexCode ) && CompileEntry( "ForwardPS", "ps_5_0", PixelCode ) && CompileEntry( "ForwardTexturedPS", "ps_5_0", TexturedPixelCode )
			&& CompileEntry( "ForwardShadowPS", "ps_5_0", ShadowPixelCode ) && CompileEntry( "ForwardShadowTexturedPS", "ps_5_0", ShadowTexturedPixelCode )
			&& CompileEntry( "ForwardDepthVS", "vs_5_0", DepthVertexCode ) && CompileEntry( "ForwardDepthPS", "ps_5_0", DepthPixelCode )
			&& CompileEntry( "ForwardVS", "vs_5_0", SkinnedVertexCode, true ) && CompileEntry( "ForwardDepthVS", "vs_5_0", SkinnedDepthVertexCode, true );
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
		if ( Mesh.Skin.IsValid() )
		{
			Buffers->ReleaseBuffer( Mesh.Skin );
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
		if ( Found->Version == Draw.MeshVersion && Found->NumVertices == Draw.NumVertices && Found->NumIndices == Draw.NumIndices && Found->Skin.IsValid() == ( Draw.SkinVertices != nullptr ) && Found->NumBones == Draw.NumBones )
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
	if ( Draw.SkinVertices )
	{
		for ( int32 Index = 0; Index < Draw.NumVertices; ++Index ) // 置く前に、全部の骨の番号が、骨の行列の中にあることを確かめる。
		{
			for ( int32 Slot = 0; Slot < 4; ++Slot )
			{
				if ( Draw.SkinVertices[Index].Bones[Slot] >= Draw.NumBones || !NMath::IsFinite( Draw.SkinVertices[Index].Weights[Slot] ) )
				{
					ReleaseMesh( Mesh );
					return nullptr;
				}
			}
		}
		Mesh.Skin = Buffers->CreateBuffer( TArrayView<const uint8>( reinterpret_cast<const uint8*>( Draw.SkinVertices ), static_cast<int32>( static_cast<usize>( Draw.NumVertices ) * sizeof( FStaticModelSkinVertex ) ) ), ERenderBufferShaderAccess::Read );
		Mesh.NumBones = Draw.NumBones;
	}
	if ( !Mesh.Vertices.IsValid() || !Mesh.Indices.IsValid() || ( Draw.SkinVertices && !Mesh.Skin.IsValid() ) )
	{
		ReleaseMesh( Mesh );
		return nullptr;
	}
	Mesh.Version = Draw.MeshVersion;
	Mesh.NumVertices = Draw.NumVertices;
	Mesh.NumIndices = Draw.NumIndices;
	Mesh.Bytes = static_cast<uint64>( Draw.NumVertices ) * sizeof( FDrawVertex3D ) + static_cast<uint64>( Draw.NumIndices ) * sizeof( uint32 );
	if ( Draw.SkinVertices )
	{
		Mesh.Bytes += static_cast<uint64>( Draw.NumVertices ) * sizeof( FStaticModelSkinVertex );
	}
	Mesh.LastUsed = Clock;
	TotalBytes += Mesh.Bytes;
	Meshes.Add( Draw.MeshKey, Mesh );
	Purge( Draw.MeshKey );
	return Meshes.Find( Draw.MeshKey );
}

FGraphicsPipelineHandle CDX12ForwardMeshRenderer::GetPipeline( const FForwardMeshDraw& Draw, bool bTextured, bool bReceive, ERenderTargetColorFormat TargetFormat, EComputeTextureFormat TextureFormat )
{
	const FGeometryDrawState& State = Draw.State;
	const FGeometryCustomBlend& Custom = State.CustomBlend;
	const bool bCustom = State.bAlphaBlend && State.Blend == EGeometryBlend::Custom; // 自分で決めた係数のときだけ、係数も鍵に入れる。
	const int32 Inputs[NumPipelineInputs] = { Draw.bDepthOnly ? 2 : ( bReceive ? 1 : 0 ), bTextured ? 1 : 0, static_cast<int32>( TargetFormat ), static_cast<int32>( TextureFormat ), State.bTestDepth ? 1 : 0, State.bWriteDepth ? 1 : 0,
		static_cast<int32>( State.Compare ), static_cast<int32>( State.Culling ), State.bAlphaBlend ? 1 : 0, State.bAlphaBlend ? static_cast<int32>( State.Blend ) : 0, static_cast<int32>( State.AddressU ), State.bLinearFilter ? 1 : 0,
		bCustom ? static_cast<int32>( Custom.SrcColor ) : 0, bCustom ? static_cast<int32>( Custom.DestColor ) : 0, bCustom ? static_cast<int32>( Custom.ColorOp ) : 0, bCustom ? static_cast<int32>( Custom.SrcAlpha ) : 0, bCustom ? static_cast<int32>( Custom.DestAlpha ) : 0,
		bCustom ? static_cast<int32>( Custom.AlphaOp ) : 0, Draw.SkinVertices ? 1 : 0 };
	// 直前の描画と、入力がすべて同じなら、鍵の文字列を作らずに、前回の答えを返す（同じ状態の描画が、続くことが多い）。
	if ( bLastPipelineValid )
	{
		bool bSame = true;
		for ( int32 Index = 0; Index < NumPipelineInputs && bSame; ++Index )
		{
			bSame = LastPipelineInputs[Index] == Inputs[Index];
		}
		if ( bSame )
		{
			return LastPipelineHandle;
		}
	}
	const auto Remember = [&]( FGraphicsPipelineHandle Handle )
	{
		for ( int32 Index = 0; Index < NumPipelineInputs; ++Index )
		{
			LastPipelineInputs[Index] = Inputs[Index];
		}
		LastPipelineHandle = Handle;
		bLastPipelineValid = true;
		return Handle;
	};
	const FString Key = FString::Printf( "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d,%d,%d,%d,%d,%d|%d", Inputs[0], Inputs[1], Inputs[2], Inputs[3], Inputs[4], Inputs[5], Inputs[6], Inputs[7], Inputs[8], Inputs[9], Inputs[10], Inputs[11], Inputs[12], Inputs[13], Inputs[14], Inputs[15], Inputs[16], Inputs[17], Inputs[18] );
	if ( const FGraphicsPipelineHandle* Found = PipelineCache.Find( Key ) )
	{
		return Remember( *Found ); // 作れなかった組み合わせは、無効なまま覚えて、毎回作り直さない。
	}
	// 頂点の並びは FDrawVertex3D（48 バイト）。
	const FGraphicsInputElement Elements[5] = {
		{ "POSITION", 0, EGraphicsVertexAttributeFormat::Float3, 0 },
		{ "NORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 12 },
		{ "COLOR", 0, EGraphicsVertexAttributeFormat::UByte4Norm, 24 },
		{ "COLOR", 1, EGraphicsVertexAttributeFormat::UByte4Norm, 28 },
		{ "TEXCOORD", 0, EGraphicsVertexAttributeFormat::Float2, 32 },
	};
	FGraphicsBufferSlot BufferSlots[4];
	BufferSlots[0].Binding = 0;
	BufferSlots[0].Layout = EGraphicsBufferLayout::Constant;
	BufferSlots[1].Binding = 6;
	BufferSlots[1].Layout = EGraphicsBufferLayout::Constant;
	int32 NumBufferSlots = Draw.bDepthOnly ? 1 : 2; // 深度だけの描画は、個体の定数（b0）だけを使う（宣言した枠は、すべて使われている必要がある）。
	const bool bSkinned = Draw.SkinVertices != nullptr;
	if ( bSkinned ) // GPU スキニング: 骨の行列（定数 b1。個体の定数の続き）と、頂点ごとの骨と重み（構造化バッファ t2）。
	{
		BufferSlots[NumBufferSlots].Binding = 1;
		BufferSlots[NumBufferSlots].Layout = EGraphicsBufferLayout::Constant;
		++NumBufferSlots;
		BufferSlots[NumBufferSlots].Binding = 2;
		BufferSlots[NumBufferSlots].Layout = EGraphicsBufferLayout::Structured;
		BufferSlots[NumBufferSlots].ElementStride = sizeof( FStaticModelSkinVertex );
		++NumBufferSlots;
	}
	FGraphicsTextureSlot TextureSlots[2]; // 画像（t0）と、影の絵（t1）。ある分だけ、この順に並べる。
	int32 NumTextureSlots = 0;
	if ( bTextured )
	{
		TextureSlots[NumTextureSlots].Binding = 0;
		TextureSlots[NumTextureSlots].Format = TextureFormat;
		++NumTextureSlots;
	}
	if ( bReceive )
	{
		TextureSlots[NumTextureSlots].Binding = 1;
		TextureSlots[NumTextureSlots].Format = EComputeTextureFormat::R32Float;
		++NumTextureSlots;
	}
	const TArray<uint8>& Pixel = Draw.bDepthOnly ? DepthPixelCode : bReceive ? ( bTextured ? ShadowTexturedPixelCode : ShadowPixelCode ) : ( bTextured ? TexturedPixelCode : PixelCode );
	const TArray<uint8>& Vertex = Draw.bDepthOnly ? ( bSkinned ? SkinnedDepthVertexCode : DepthVertexCode ) : ( bSkinned ? SkinnedVertexCode : VertexCode );
	FGraphicsPipelineDesc Desc;
	Desc.VertexShaderBytecode = TArrayView<const uint8>( Vertex.GetData(), Vertex.Num() );
	Desc.PixelShaderBytecode = TArrayView<const uint8>( Pixel.GetData(), Pixel.Num() );
	Desc.VertexStride = sizeof( FDrawVertex3D );
	Desc.InputElements = TArrayView<const FGraphicsInputElement>( Elements, 5 );
	Desc.BufferSlots = TArrayView<const FGraphicsBufferSlot>( BufferSlots, NumBufferSlots );
	Desc.TextureSlots = NumTextureSlots > 0 ? TArrayView<const FGraphicsTextureSlot>( TextureSlots, NumTextureSlots ) : TArrayView<const FGraphicsTextureSlot>();
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
	return Remember( Handle );
}

bool CDX12ForwardMeshRenderer::Draw( const FForwardMeshDraw& Draw, IRenderGeometry3D& Geometry )
{
	const FGeometryDrawState& State = Draw.State;
	const int32 VertexLimit = static_cast<int32>( Draw.bTransient ? NGraphicsPipelineLimits::MaxTransientVertices : NGraphicsPipelineLimits::MaxRegisteredVertices ); // 登録しない描画は、小さく抑える。
	const int32 IndexLimit = static_cast<int32>( Draw.bTransient ? NGraphicsPipelineLimits::MaxTransientIndices : NGraphicsPipelineLimits::MaxRegisteredIndices );
	if ( !Pipelines || !Buffers || !Textures || ( !Draw.bTransient && Draw.MeshKey == 0 ) || !Draw.Vertices || ( !Draw.Indices && !Draw.bTransient ) || Draw.NumVertices <= 0 || Draw.NumVertices > VertexLimit || Draw.NumIndices <= 0 || Draw.NumIndices % 3 != 0
		|| Draw.FirstIndex < 0 || Draw.IndexCount <= 0 || Draw.IndexCount > IndexLimit || Draw.IndexCount % 3 != 0 || Draw.FirstIndex % 3 != 0 || static_cast<int64>( Draw.FirstIndex ) + Draw.IndexCount > Draw.NumIndices )
	{
		return false;
	}
	if ( State.Mask.IsActive() || State.MaxAnisotropy > 1 || State.DepthBias != 0 || State.AddressU != State.AddressV || State.Topology != EGeometryTopology::Triangles
		|| ( State.bAlphaBlend && ( State.Blend == EGeometryBlend::Invert || State.Blend == EGeometryBlend::MultiplyAlpha ) ) )
	{
		return false; // この経路で表せない状態（呼び出し側が、CPU の経路で描く）。
	}
	const bool bSkinnedDraw = Draw.SkinVertices != nullptr;
	if ( bSkinnedDraw && ( Draw.bTransient || !Draw.BoneMatrices || Draw.NumBones <= 0 || Draw.NumBones > MaxStaticModelSkinBones ) )
	{
		return false; // GPU スキニングは、登録するメッシュだけ（骨の行列が要る）。
	}
	for ( int32 Index = 0; bSkinnedDraw && Index < Draw.NumBones * 12; ++Index )
	{
		if ( !NMath::IsFinite( Draw.BoneMatrices[Index] ) )
		{
			return false;
		}
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
	const bool bDepth = Draw.bDepthOnly; // 影の絵へ、光から見た深度だけを書く。
	if ( bDepth && Geometry.GetColorFormat() != ERenderTargetColorFormat::R32Float )
	{
		return false; // 影の絵（R32Float）以外へは、深度を書けない（呼び出し側が、CPU の経路で描く）。
	}
	const bool bReceive = !bDepth && Draw.ShadowMap >= 0 && Draw.bLit; // 影を受ける（光を計算する描画だけ）。
	if ( bReceive )
	{
		if ( !IsFiniteMatrix( Draw.LightViewProjection ) || !NMath::IsFinite( Draw.ShadowParams[2] ) || !( Draw.ShadowParams[0] >= 1.0f ) || !( Draw.ShadowParams[1] >= 1.0f ) || Draw.ShadowMask == 0 )
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
	FTextureResourceHandle SlotTextures[2]; // 画像（あれば）→ 影の絵（あれば）の順。パイプラインの画像の枠と同じ並び。
	int32 NumSlotTextures = 0;
	EComputeTextureFormat TextureFormat = EComputeTextureFormat::RGBA8;
	const bool bTextured = !bDepth && Draw.Texture >= 0;
	if ( bTextured )
	{
		if ( !Textures->GetResource( Draw.Texture, SlotTextures[NumSlotTextures] ) || !Textures->GetShaderFormat( Draw.Texture, TextureFormat ) )
		{
			return false;
		}
		++NumSlotTextures;
	}
	if ( bReceive )
	{
		EComputeTextureFormat ShadowFormat = EComputeTextureFormat::RGBA8;
		if ( !Textures->GetResource( Draw.ShadowMap, SlotTextures[NumSlotTextures] ) || !Textures->GetShaderFormat( Draw.ShadowMap, ShadowFormat ) || ShadowFormat != EComputeTextureFormat::R32Float )
		{
			return false;
		}
		++NumSlotTextures;
	}
	++Clock;
	const FGraphicsPipelineHandle Pipeline = GetPipeline( Draw, bTextured, bReceive, Geometry.GetColorFormat(), TextureFormat );
	if ( !Pipeline.IsValid() )
	{
		return false;
	}
	const FMesh* Mesh = Draw.bTransient ? nullptr : AcquireMesh( Draw );
	if ( !Draw.bTransient && !Mesh )
	{
		return false;
	}
	if ( bSkinnedDraw && ( !Mesh->Skin.IsValid() || Mesh->NumBones != Draw.NumBones ) )
	{
		return false; // 置いてあるメッシュの骨の数と、いまの骨の行列の数が違う（骨の番号の範囲を保証できない）。
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
	FMemory::Memcpy( Object.LightViewProjection, Draw.LightViewProjection, sizeof( Object.LightViewProjection ) );
	Object.ShadowParams[0] = bReceive ? Draw.ShadowParams[0] : 0.0f;
	Object.ShadowParams[1] = bReceive ? Draw.ShadowParams[1] : 0.0f;
	Object.ShadowParams[2] = bReceive ? Draw.ShadowParams[2] : 0.0f;
	Object.ShadowParams[3] = bReceive ? static_cast<float32>( Draw.ShadowMask ) : 0.0f;
	FGraphicsConstantBytes Constants[3]; // 個体（b0）→ 光（b6。深度だけの描画は無し）→ 骨の行列（b1。GPU スキニングだけ）の順。パイプラインの枠と同じ並び。
	int32 NumConstants = 0;
	Constants[NumConstants].Data = reinterpret_cast<const uint8*>( &Object );
	Constants[NumConstants].Size = sizeof( Object );
	++NumConstants;
	if ( !bDepth )
	{
		Constants[NumConstants].Data = reinterpret_cast<const uint8*>( Draw.Lighting );
		Constants[NumConstants].Size = sizeof( Draw.Lighting );
		++NumConstants;
	}
	if ( bSkinnedDraw )
	{
		constexpr int32 PaletteFloats = 12 * MaxStaticModelSkinBones; // シェーダーの g_Bones の大きさ（定数は、宣言した大きさ以上で渡す）。
		if ( PaletteScratch.Num() != PaletteFloats )
		{
			PaletteScratch.SetNum( PaletteFloats, 0.0f );
		}
		FMemory::Memcpy( PaletteScratch.GetData(), Draw.BoneMatrices, sizeof( float32 ) * 12 * Draw.NumBones );
		FMemory::Memzero( PaletteScratch.GetData() + 12 * Draw.NumBones, sizeof( float32 ) * ( PaletteFloats - 12 * Draw.NumBones ) );
		Constants[NumConstants].Data = reinterpret_cast<const uint8*>( PaletteScratch.GetData() );
		Constants[NumConstants].Size = sizeof( float32 ) * PaletteFloats;
		++NumConstants;
	}
	if ( Draw.bTransient )
	{
		FGraphicsPipelineTransientDraw Transient; // 借用値は既存のフレーム転送領域へコピーされる。
		Transient.Pipeline = Pipeline;
		Transient.Vertices = Draw.Vertices;
		Transient.VertexCount = Draw.NumVertices;
		Transient.VertexStride = static_cast<uint32>( sizeof( FDrawVertex3D ) );
		Transient.Indices = Draw.Indices ? Draw.Indices + Draw.FirstIndex : nullptr;
		Transient.IndexCount = Draw.Indices ? Draw.IndexCount : 0;
		Transient.Constants = TArrayView<const FGraphicsConstantBytes>( Constants, NumConstants );
		Transient.Textures = TArrayView<const FTextureResourceHandle>( SlotTextures, NumSlotTextures );
		return Geometry.QueueGraphicsPipelineTransient( Transient );
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
	Command.Constants = TArrayView<const FGraphicsConstantBytes>( Constants, NumConstants );
	const FBufferResourceHandle SkinBuffers[1] = { Mesh->Skin }; // 構造化バッファ（t2）。GPU スキニングだけ。
	Command.Buffers = TArrayView<const FBufferResourceHandle>( SkinBuffers, bSkinnedDraw ? 1 : 0 );
	Command.Textures = TArrayView<const FTextureResourceHandle>( SlotTextures, NumSlotTextures );
	Command.PinnedBytes = Mesh->Bytes; // 結果を待たずに積むときの、保持量の見積もり。
	const bool bDrawn = Geometry.QueueGraphicsPipeline( Command );
	if ( !bDrawn )
	{
		LOG( LogDX12, Warning, "CDX12ForwardMeshRenderer: the draw is rejected by the GPU path (mesh %llu)", Draw.MeshKey );
	}
	return bDrawn;
}
