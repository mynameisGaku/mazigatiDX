// SPDX-License-Identifier: MIT
#include "MazigatiEngineShaderSources.generated.h"
#include "DX12ShaderService.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment( lib, "d3dcompiler.lib" )

namespace
{
	// 頂点シェーダーの入力のセマンティックを、FDrawShaderVertex3D（88 バイト）の中の位置と型にする。知らない名前は false
	bool MapSemantic( const FString& Name, uint32 Index, EGraphicsVertexAttributeFormat& OutFormat, uint32& OutOffset )
	{
		struct FField
		{
			const char* Name;
			uint32 Index;
			EGraphicsVertexAttributeFormat Format;
			uint32 Offset;
		};
		static const FField Fields[] = {
			{ "POSITION", 0, EGraphicsVertexAttributeFormat::Float3, 0 },   { "POSITION", 1, EGraphicsVertexAttributeFormat::Float4, 12 },
			{ "NORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 28 },    { "TANGENT", 0, EGraphicsVertexAttributeFormat::Float3, 40 },
			{ "BINORMAL", 0, EGraphicsVertexAttributeFormat::Float3, 52 },  { "COLOR", 0, EGraphicsVertexAttributeFormat::UByte4Norm, 64 },
			{ "COLOR", 1, EGraphicsVertexAttributeFormat::UByte4Norm, 68 }, { "TEXCOORD", 0, EGraphicsVertexAttributeFormat::Float2, 72 },
			{ "TEXCOORD", 1, EGraphicsVertexAttributeFormat::Float2, 80 },
		};
		for ( const FField& Field : Fields )
		{
			if ( Field.Index == Index && Name.Equals( Field.Name, ESearchCase::IgnoreCase ) )
			{
				OutFormat = Field.Format;
				OutOffset = Field.Offset;
				return true;
			}
		}
		return false;
	}

	// 4 つの成分のうち、その形で読める成分の印（x = 1, y = 2, z = 4, w = 8）
	uint32 GetFormatMask( EGraphicsVertexAttributeFormat Format )
	{
		switch ( Format )
		{
		case EGraphicsVertexAttributeFormat::Float1: return 1;
		case EGraphicsVertexAttributeFormat::Float2: return 3;
		case EGraphicsVertexAttributeFormat::Float3: return 7;
		default: return 15;
		}
	}

	// 組み込みの頂点シェーダー（ShaderRect / ShaderQuad3D）。入力は、FDrawShaderVertex3D の全部。出力は、ピクセルシェーダーへ、色・画像の座標・Extra
	constexpr const char* BuiltInSource = NEngineShaderSources::DX12_BuiltIn;

	uint32 RoundUp( uint32 Value, uint32 Unit )
	{
		return ( Value + Unit - 1 ) / Unit * Unit;
	}
}

CDX12ShaderService::~CDX12ShaderService()
{
	Shutdown();
}

void CDX12ShaderService::Initialize( IGraphicsPipelineResources* InPipelines, IRenderBufferResources* InBuffers, CTextureAssetStore* InTextures )
{
	if ( Pipelines || !InPipelines || !InBuffers || !InTextures )
	{
		return;
	}
	Pipelines = InPipelines;
	Buffers = InBuffers;
	Textures = InTextures;
	MeshBuffers.Initialize( Buffers );
}

void CDX12ShaderService::Shutdown()
{
	MeshBuffers.Shutdown();
	if ( Pipelines )
	{
		for ( const auto& Pair : PipelineCache )
		{
			if ( Pair.Value.Handle.IsValid() )
			{
				Pipelines->ReleaseGraphicsPipeline( Pair.Value.Handle );
			}
		}
	}
	PipelineCache.Reset();
	if ( Textures && EmptyTexture >= 0 )
	{
		Textures->Delete( EmptyTexture );
	}
	EmptyTexture = -1;
	Shaders.Reset();
	BuiltInRect = FShaderRecord();
	BuiltInQuad = FShaderRecord();
	BuiltInLit = FShaderRecord();
	BuiltInShadowDepth = FShaderRecord();
	BuiltInShadowLit = FShaderRecord();
	BuiltInShadowLitTextured = FShaderRecord();
	BuiltInFogTextured = FShaderRecord();
	BuiltInImageFilter = FShaderRecord();
	BuiltInImageBlend = FShaderRecord();
	BuiltInPixelLitVertex = FShaderRecord();
	BuiltInPixelLit = FShaderRecord();
	BuiltInPixelLitTextured = FShaderRecord();
	BuiltInEffectMaterialVertex = FShaderRecord();
	BuiltInEffectMaterial = FShaderRecord();
	BuiltInShadowPreview = FShaderRecord();
	ShadowPasses.Reset();
	bBuiltInTried = false;
	Pipelines = nullptr;
	Buffers = nullptr;
	Textures = nullptr;
}

bool CDX12ShaderService::Reflect( EShaderStage Stage, const uint8* Code, int32 NumBytes, FShaderRecord& Record ) const
{
	if ( !Code || NumBytes < 32 || NumBytes > 16 * 1024 * 1024 )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection; // 成功するまで、出力を書き換えない。
	D3D12_SHADER_DESC Desc = {};
	if ( FAILED( D3DReflect( Code, static_cast<SIZE_T>( NumBytes ), IID_PPV_ARGS( &Reflection ) ) ) || FAILED( Reflection->GetDesc( &Desc ) ) )
	{
		return false;
	}
	const UINT Type = D3D12_SHVER_GET_TYPE( Desc.Version ); // 0 = ピクセル、1 = 頂点。
	if ( Type != ( Stage == EShaderStage::Vertex ? 1u : 0u ) || D3D12_SHVER_GET_MAJOR( Desc.Version ) != 5 )
	{
		return false;
	}
	FShaderRecord Result;
	Result.Stage = Stage;
	Result.Code.Append( Code, NumBytes );
	for ( UINT Index = 0; Index < Desc.InputParameters && Stage == EShaderStage::Vertex; ++Index )
	{
		D3D12_SIGNATURE_PARAMETER_DESC Param = {};
		if ( FAILED( Reflection->GetInputParameterDesc( Index, &Param ) ) )
		{
			return false;
		}
		if ( Param.SystemValueType != D3D_NAME_UNDEFINED )
		{
			continue; // SV_VertexID などは、頂点の中身を使わない。
		}
		FInput Input;
		Input.Semantic = FString( Param.SemanticName );
		Input.Index = Param.SemanticIndex;
		Input.Mask = Param.Mask;
		Result.Inputs.Add( Input );
	}
	for ( UINT Index = 0; Index < Desc.BoundResources; ++Index )
	{
		D3D12_SHADER_INPUT_BIND_DESC Bind = {};
		if ( FAILED( Reflection->GetResourceBindingDesc( Index, &Bind ) ) )
		{
			return false;
		}
		if ( Bind.Type == D3D_SIT_SAMPLER )
		{
			continue;
		}
		if ( Bind.Type == D3D_SIT_CBUFFER )
		{
			ID3D12ShaderReflectionConstantBuffer* Buffer = Reflection->GetConstantBufferByName( Bind.Name );
			D3D12_SHADER_BUFFER_DESC BufferDesc = {};
			if ( !Buffer || FAILED( Buffer->GetDesc( &BufferDesc ) ) )
			{
				return false;
			}
			FConstantInfo Info;
			Info.Binding = Bind.BindPoint;
			Info.Size = BufferDesc.Size;
			Result.Constants.Add( Info );
		}
		else if ( Bind.Type == D3D_SIT_TEXTURE && Bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2D )
		{
			Result.Textures.Add( Bind.BindPoint );
		}
		else
		{
			Result.bUnsupportedResource = true;   // 構造化バッファ・Texture3D など。
		}
	}
	for ( UINT Index = 0; Index < Desc.OutputParameters && Stage == EShaderStage::Pixel; ++Index )
	{
		D3D12_SIGNATURE_PARAMETER_DESC Param = {};
		if ( SUCCEEDED( Reflection->GetOutputParameterDesc( Index, &Param ) ) && Param.SystemValueType == D3D_NAME_TARGET )
		{
			++Result.NumOutputs;
		}
	}
	Record = MoveTemp( Result );
	return true;
}

int32 CDX12ShaderService::Make( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	FShaderRecord Record;
	if ( !Reflect( Stage, static_cast<const uint8*>( Code ), NumBytes, Record ) || NextShader == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	const int32 Handle = NextShader++;   // 番号は使い回さない。
	Shaders.Add( Handle, MoveTemp( Record ) );
	return Handle;
}

void CDX12ShaderService::Delete( int32 Shader )
{
	if ( !Shaders.Find( Shader ) )
	{
		return;
	}
	TArray<FString> Stale; // 走査中に表を変えないよう、先に鍵を集める。
	for ( const auto& Pair : PipelineCache )
	{
		if ( Pair.Value.VertexShader == Shader || Pair.Value.PixelShader == Shader )
		{
			Stale.Add( Pair.Key );
		}
	}
	for ( const FString& Key : Stale )
	{
		if ( const FPipelineEntry* Entry = PipelineCache.Find( Key ) )
		{
			if ( Pipelines && Entry->Handle.IsValid() )
			{
				Pipelines->ReleaseGraphicsPipeline( Entry->Handle );
			}
		}
		PipelineCache.Remove( Key );
	}
	Shaders.Remove( Shader );
}

bool CDX12ShaderService::Compile( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage )
{
	OutCode.Reset();
	OutMessage = FString();
	if ( !Source || NumBytes < 1 || !EntryPoint || EntryPoint[0] == '\0' || !ShaderModel )
	{
		OutMessage = "the source, the entry point and the shader model are required";
		return false;
	}
	if ( NCString::Compare( ShaderModel, "4_0" ) != 0 && NCString::Compare( ShaderModel, "4_1" ) != 0 && NCString::Compare( ShaderModel, "5_0" ) != 0 )
	{
		OutMessage = "the shader model must be 4_0, 4_1 or 5_0";
		return false;
	}
	// DX12 は Shader Model 5 だけを受け取る。4_0 / 4_1 のソースも、5_0 で同じようにコンパイルできる。
	const char* Target = Stage == EShaderStage::Pixel ? "ps_5_0" : "vs_5_0";
	Microsoft::WRL::ComPtr<ID3DBlob> Code, Errors;
	const HRESULT Result = D3DCompile( Source, static_cast<SIZE_T>( NumBytes ), SourceName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoint, Target, 0, 0, &Code, &Errors );
	if ( Errors )
	{
		OutMessage = FString( static_cast<const char*>( Errors->GetBufferPointer() ), static_cast<int32>( Errors->GetBufferSize() ) );
	}
	if ( FAILED( Result ) || !Code )
	{
		if ( OutMessage.IsEmpty() )
		{
			OutMessage = "the shader cannot be compiled";
		}
		return false;
	}
	OutCode.Append( static_cast<const uint8*>( Code->GetBufferPointer() ), static_cast<int32>( Code->GetBufferSize() ) );
	return true;
}

bool CDX12ShaderService::IsShaderValid( int32 Shader, EShaderStage Stage ) const
{
	const FShaderRecord* Record = Shaders.Find( Shader );
	return Record && Record->Stage == Stage;
}

void CDX12ShaderService::BuildBuiltIns()
{
	if ( bBuiltInTried )
	{
		return;
	}
	bBuiltInTried = true;
	const int32 Length = static_cast<int32>( NCString::Length( BuiltInSource ) );
	const auto Build = [&]( EShaderStage Stage, const char* Entry, FShaderRecord& Out )
	{
		TArray<uint8> Code;
		FString Message;
		if ( !Compile( Stage, BuiltInSource, Length, "BuiltInShader", Entry, "5_0", Code, Message ) || !Reflect( Stage, Code.GetData(), Code.Num(), Out ) )
		{
			LOG( LogDX12, Warning, "CDX12ShaderService: the built-in shader '%s' cannot be built: %s", Entry, *Message );
		}
	};
	Build( EShaderStage::Vertex, "RectMain", BuiltInRect );
	Build( EShaderStage::Vertex, "QuadMain", BuiltInQuad );
	Build( EShaderStage::Vertex, "LitMain", BuiltInLit );
	Build( EShaderStage::Pixel, "ShadowDepthMain", BuiltInShadowDepth );
	Build( EShaderStage::Pixel, "ShadowLitMain", BuiltInShadowLit );
	Build( EShaderStage::Pixel, "ShadowLitTexturedMain", BuiltInShadowLitTextured );
	Build( EShaderStage::Pixel, "FogTexturedMain", BuiltInFogTextured );
	Build( EShaderStage::Pixel, "ImageFilterMain", BuiltInImageFilter );
	Build( EShaderStage::Pixel, "ImageBlendMain", BuiltInImageBlend );
	Build( EShaderStage::Vertex, "PixelLitVS", BuiltInPixelLitVertex );
	Build( EShaderStage::Pixel, "PixelLitMain", BuiltInPixelLit );
	Build( EShaderStage::Pixel, "PixelLitTexturedMain", BuiltInPixelLitTextured );
	Build( EShaderStage::Vertex, "EffectMaterialVS", BuiltInEffectMaterialVertex );
	Build( EShaderStage::Pixel, "EffectMaterialPS", BuiltInEffectMaterial );
	Build( EShaderStage::Pixel, "ShadowPreviewMain", BuiltInShadowPreview );
}

const CDX12ShaderService::FShaderRecord* CDX12ShaderService::GetBuiltIn( EBuiltInVertexShader Kind )
{
	BuildBuiltIns();
	const FShaderRecord* Record = Kind == EBuiltInVertexShader::Rect ? &BuiltInRect : Kind == EBuiltInVertexShader::Quad3D ? &BuiltInQuad : Kind == EBuiltInVertexShader::ShadowLit ? &BuiltInLit : Kind == EBuiltInVertexShader::PixelLit ? &BuiltInPixelLitVertex : Kind == EBuiltInVertexShader::EffectMaterial ? &BuiltInEffectMaterialVertex : nullptr;
	return Record && !Record->Code.IsEmpty() ? Record : nullptr;
}

const CDX12ShaderService::FShaderRecord* CDX12ShaderService::GetBuiltInPixel( EBuiltInPixelShader Kind )
{
	BuildBuiltIns();
	const FShaderRecord* Record = Kind == EBuiltInPixelShader::ShadowDepth ? &BuiltInShadowDepth : Kind == EBuiltInPixelShader::ShadowLit ? &BuiltInShadowLit : Kind == EBuiltInPixelShader::ShadowLitTextured ? &BuiltInShadowLitTextured : Kind == EBuiltInPixelShader::FogTextured ? &BuiltInFogTextured : Kind == EBuiltInPixelShader::ImageFilter ? &BuiltInImageFilter : Kind == EBuiltInPixelShader::ImageBlend ? &BuiltInImageBlend : Kind == EBuiltInPixelShader::PixelLit ? &BuiltInPixelLit : Kind == EBuiltInPixelShader::PixelLitTextured ? &BuiltInPixelLitTextured : Kind == EBuiltInPixelShader::EffectMaterial ? &BuiltInEffectMaterial : Kind == EBuiltInPixelShader::ShadowPreview ? &BuiltInShadowPreview : nullptr;
	return Record && !Record->Code.IsEmpty() ? Record : nullptr;
}

void CDX12ShaderService::SetShadowPass( int32 ShadowMap, const FMatrix4x4& LightViewProjection, float32 AdjustDepth, int32 Width, int32 Height )
{
	FShadowPass Pass;
	Pass.LightViewProjection = LightViewProjection;
	Pass.AdjustDepth = AdjustDepth;
	Pass.Width = Width;
	Pass.Height = Height;
	ShadowPasses.Add( ShadowMap, Pass );
}

bool CDX12ShaderService::GetShadowPass( int32 ShadowMap, FMatrix4x4& OutLightViewProjection, float32& OutAdjustDepth, int32& OutWidth, int32& OutHeight ) const
{
	const FShadowPass* Pass = ShadowPasses.Find( ShadowMap );
	if ( !Pass )
	{
		return false;
	}
	OutLightViewProjection = Pass->LightViewProjection;
	OutAdjustDepth = Pass->AdjustDepth;
	OutWidth = Pass->Width;
	OutHeight = Pass->Height;
	return true;
}

bool CDX12ShaderService::GetEmptyTexture( FTextureResourceHandle& Out )
{
	if ( EmptyTexture < 0 || !Textures->GetResource( EmptyTexture, Out ) )
	{
		const FColor32 Clear( 0, 0, 0, 0 );   // 読むと 0 になる、1 x 1 の透明な黒
		EmptyTexture = Textures->Create( 1, 1, &Clear );
		if ( EmptyTexture < 0 || !Textures->GetResource( EmptyTexture, Out ) )
		{
			return false;
		}
	}
	return true;
}

FGraphicsPipelineHandle CDX12ShaderService::GetPipeline( const FShaderDrawRequest& Request, const FShaderRecord& Vertex, const FShaderRecord& Pixel, const TArray<FGraphicsBufferSlot>& BufferSlots, const TArray<FGraphicsTextureSlot>& TextureSlots )
{
	const FGeometryDrawState& State = Request.State;
	const int32 VertexId = Request.Builtin == EBuiltInVertexShader::None ? Request.VertexShader : -2 - static_cast<int32>( Request.Builtin );
	const int32 PixelId = Request.BuiltinPixel == EBuiltInPixelShader::None ? Request.PixelShader : -2 - static_cast<int32>( Request.BuiltinPixel );
	FString Formats; // 書き込み先と読む画像の形式（パイプラインの形を決める）。
	for ( int32 Target = 0; Target < Request.NumTargets; ++Target )
	{
		Formats += static_cast<char>( '0' + static_cast<int32>( Request.TargetFormats[Target] ) );
	}
	Formats += '/';
	for ( const FGraphicsTextureSlot& Slot : TextureSlots )
	{
		Formats += static_cast<char>( '0' + static_cast<int32>( Slot.Format ) );
	}
	const FString Key = Formats + FString::Printf( "|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", VertexId, PixelId, static_cast<int32>( Request.Topology ), Request.bUseDepth ? 1 : 0,
		State.bTestDepth ? 1 : 0, State.bWriteDepth ? 1 : 0, static_cast<int32>( State.Compare ), State.DepthBias, static_cast<int32>( State.Culling ), State.bAlphaBlend ? 1 : 0,
		State.bLinearFilter ? 1 : 0, static_cast<int32>( State.AddressU ), static_cast<int32>( State.AddressV ), State.bAlphaBlend ? static_cast<int32>( State.Blend ) : 0 ) + ( State.bAlphaBlend && State.Blend == EGeometryBlend::Custom ? FString::Printf( "|%u", State.CustomBlend.Pack() ) : FString() );
	if ( const FPipelineEntry* Found = PipelineCache.Find( Key ) )
	{
		return Found->Handle;   // 作れなかった組み合わせは、無効なまま覚えて、毎回作り直さない。
	}
	// 頂点シェーダーの入力を、頂点の中の位置へ割り当てる。
	TArray<FGraphicsInputElement> Elements;
	for ( const FInput& Input : Vertex.Inputs )
	{
		FGraphicsInputElement Element;
		if ( !MapSemantic( Input.Semantic, Input.Index, Element.Format, Element.AlignedByteOffset ) || ( Input.Mask & ~GetFormatMask( Element.Format ) ) != 0 )
		{
			LOG( LogDX12, Warning, "CDX12ShaderService: the vertex shader input %s%u cannot be read from the vertex (declare the inputs of the shader vertex in order)", *Input.Semantic, Input.Index );
			PipelineCache.Add( Key, FPipelineEntry() );
			return FGraphicsPipelineHandle();
		}
		Element.SemanticName = *Input.Semantic;
		Element.SemanticIndex = Input.Index;
		Elements.Add( Element );
	}
	FGraphicsPipelineDesc Desc;
	Desc.VertexShaderBytecode = TArrayView<const uint8>( Vertex.Code.GetData(), Vertex.Code.Num() );
	Desc.PixelShaderBytecode = TArrayView<const uint8>( Pixel.Code.GetData(), Pixel.Code.Num() );
	Desc.VertexStride = FShaderDrawRequest::VertexStride;
	Desc.InputElements = TArrayView<const FGraphicsInputElement>( Elements.GetData(), Elements.Num() );
	Desc.BufferSlots = TArrayView<const FGraphicsBufferSlot>( BufferSlots.GetData(), BufferSlots.Num() );
	Desc.TextureSlots = TArrayView<const FGraphicsTextureSlot>( TextureSlots.GetData(), TextureSlots.Num() );
	Desc.NumRenderTargets = static_cast<uint32>( Request.NumTargets );
	for ( int32 Target = 0; Target < Request.NumTargets; ++Target )
	{
		Desc.RenderTargetFormats[Target] = Request.TargetFormats[Target];
	}
	Desc.DepthFormat = Request.bUseDepth ? EGraphicsDepthFormat::D32Float : EGraphicsDepthFormat::None;
	Desc.Topology = Request.Topology;
	Desc.DepthCompare = State.Compare;
	Desc.Culling = State.Culling;
	Desc.SamplerAddress = State.AddressU;
	Desc.bDepthTest = Request.bUseDepth && State.bTestDepth;
	Desc.bDepthWrite = Request.bUseDepth && State.bTestDepth && State.bWriteDepth;
	Desc.bAlphaBlend = State.bAlphaBlend;
	Desc.Blend = State.Blend;
	Desc.CustomBlend = State.CustomBlend;
	Desc.bLinearSampler = State.bLinearFilter;
	FPipelineEntry Entry;
	Entry.VertexShader = Request.Builtin == EBuiltInVertexShader::None ? Request.VertexShader : -1;
	Entry.PixelShader = Request.BuiltinPixel == EBuiltInPixelShader::None ? Request.PixelShader : -1;
	Entry.Handle = Pipelines->CreateGraphicsPipeline( Desc );
	if ( !Entry.Handle.IsValid() )
	{
		LOG( LogDX12, Warning, "CDX12ShaderService: the pipeline of the shaders (vertex %d, pixel %d) cannot be created (the shaders must be Shader Model 5, the vertex inputs must be readable, and the outputs of the vertex shader must cover the inputs of the pixel shader)", VertexId, PixelId );
	}
	PipelineCache.Add( Key, Entry );
	return Entry.Handle;
}

bool CDX12ShaderService::DrawShader( const FShaderDrawRequest& Request, IRenderGeometry3D& Geometry )
{
	if ( !Pipelines || !Buffers || !Textures || !Request.Vertices || Request.NumVertices <= 0 || Request.NumVertices > 65536 || ( Request.Indices && ( Request.NumIndices <= 0 || Request.NumIndices > 196608 ) ) )
	{
		return false;
	}
	const FShaderRecord* Vertex = Request.Builtin == EBuiltInVertexShader::None ? Shaders.Find( Request.VertexShader ) : GetBuiltIn( Request.Builtin );
	const FShaderRecord* Pixel = Request.BuiltinPixel == EBuiltInPixelShader::None ? Shaders.Find( Request.PixelShader ) : GetBuiltInPixel( Request.BuiltinPixel );
	if ( !Vertex || !Pixel || Vertex->Stage != EShaderStage::Vertex || Pixel->Stage != EShaderStage::Pixel )
	{
		return false;
	}
	if ( Vertex->bUnsupportedResource || Pixel->bUnsupportedResource || !Vertex->Textures.IsEmpty() )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader resources", "Only constant buffers and Texture2D in the pixel shader are supported" );
		return false;
	}
	if ( Request.NumTargets < 1 || Request.NumTargets > 8 || Pixel->NumOutputs != Request.NumTargets )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader outputs", "The number of outputs of the pixel shader (SV_Target) must match the number of render targets" );
		return false;
	}
	// 置き場: 頂点・ピクセルの両方が読む定数バッファ（b 番号で 1 つにまとめる）と、ピクセルシェーダーが読む画像
	TArray<FGraphicsBufferSlot> BufferSlots;
	TArray<uint32> BufferSizes; // 読まれる大きさの最大。
	TArray<bool> BufferReadByVertex; // その番号を、頂点シェーダーが読むか（読まなければ、ピクセル側の中身を使う）。
	const auto AddConstant = [&]( const FConstantInfo& Info, bool bVertexStage )
	{
		for ( int32 Index = 0; Index < BufferSlots.Num(); ++Index )
		{
			if ( BufferSlots[Index].Binding == Info.Binding )
			{
				BufferSizes[Index] = NMath::Max( BufferSizes[Index], Info.Size );
				BufferReadByVertex[Index] = BufferReadByVertex[Index] || bVertexStage;
				return;
			}
		}
		FGraphicsBufferSlot Slot;
		Slot.Binding = Info.Binding;
		Slot.Layout = EGraphicsBufferLayout::Constant;
		BufferSlots.Add( Slot );
		BufferSizes.Add( Info.Size );
		BufferReadByVertex.Add( bVertexStage );
	};
	for ( const FConstantInfo& Info : Vertex->Constants )
	{
		AddConstant( Info, true );
	}
	for ( const FConstantInfo& Info : Pixel->Constants )
	{
		AddConstant( Info, false );
	}
	TArray<FGraphicsTextureSlot> TextureSlots;
	for ( const uint32 Binding : Pixel->Textures )
	{
		FGraphicsTextureSlot Slot;
		Slot.Binding = Binding;
		Slot.Format = EComputeTextureFormat::RGBA8; // 段に置いた画像の形式（置いていなければ、空の画像のRGBA8）。
		if ( Binding < static_cast<uint32>( FShaderDrawRequest::NumTextureStages ) && Request.Textures[Binding] >= 0 )
		{
			Textures->GetShaderFormat( Request.Textures[Binding], Slot.Format );
		}
		TextureSlots.Add( Slot );
	}
	const FGraphicsPipelineHandle Pipeline = GetPipeline( Request, *Vertex, *Pixel, BufferSlots, TextureSlots );
	if ( !Pipeline.IsValid() )
	{
		return false;
	}
	// 画像: 段に置いてあるものか、無ければ空の画像
	TArray<FTextureResourceHandle> SlotTextures;
	for ( const FGraphicsTextureSlot& Slot : TextureSlots )
	{
		FTextureResourceHandle Resource;
		const int32 Asset = Slot.Binding < static_cast<uint32>( FShaderDrawRequest::NumTextureStages ) ? Request.Textures[Slot.Binding] : -1;
		if ( Asset < 0 || !Textures->GetResource( Asset, Resource ) )
		{
			if ( !GetEmptyTexture( Resource ) )
			{
				return false;
			}
		}
		SlotTextures.Add( Resource );
	}
	// 定数: 置いてある中身を 256 の倍数へ切り上げ、余りは 0（転送領域へ載せる側が揃えるので、読まれる大きさ以上の中身を渡す）。
	TArray<TArray<uint8>> ConstantBytes;
	TArray<FGraphicsConstantBytes> Constants;
	for ( int32 Index = 0; Index < BufferSlots.Num(); ++Index )
	{
		// 置いてある中身。その番号を、頂点シェーダーが読むなら頂点側、読まないならピクセル側（別の段に置いた古い中身を、読んでしまわないように）。
		// 両方が読む番号で、中身が違うときは、頂点側が使われる。
		const int32 WantedStage = static_cast<int32>( BufferReadByVertex[Index] ? EShaderStage::Vertex : EShaderStage::Pixel );
		const FShaderConstantData* Found = nullptr;
		for ( const FShaderConstantData& Data : Request.Constants )
		{
			if ( static_cast<uint32>( Data.Slot ) == BufferSlots[Index].Binding && ( !Found || ( Data.Stage == WantedStage && Found->Stage != WantedStage ) ) )
			{
				Found = &Data;
			}
		}
		const uint32 Size = NMath::Max( 1u, NMath::Max( BufferSizes[Index], Found ? static_cast<uint32>( Found->Bytes.Num() ) : 0u ) );
		if ( Size > 65536 )
		{
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNum( static_cast<int32>( Size ), 0 );
		if ( Found )
		{
			for ( int32 Byte = 0; Byte < Found->Bytes.Num(); ++Byte )
			{
				Bytes[Byte] = Found->Bytes[Byte];
			}
		}
		ConstantBytes.Add( MoveTemp( Bytes ) );
	}
	for ( const TArray<uint8>& Bytes : ConstantBytes )
	{
		FGraphicsConstantBytes Constant;
		Constant.Data = Bytes.GetData();
		Constant.Size = static_cast<uint32>( Bytes.Num() );
		Constants.Add( Constant );
	}
	static const bool bUseMeshCache = !NCommandLine::HasFlag( "no-shader-mesh-cache" ); // 同じ描画を従来の転送経路と比較する診断用指定。
	if ( bUseMeshCache && Request.NumVertices >= 32 && Request.Topology == EGeometryTopology::Triangles )
	{
		FGraphicsPipelineDraw Shared; // パイプラインと個体定数は毎回選び、形状だけ共有する。
		const TArrayView<const uint8> VertexBytes( Request.Vertices, Request.NumVertices * FShaderDrawRequest::VertexStride ); // 検査済み個数からの有限な範囲。
		const TArrayView<const uint8> IndexBytes( reinterpret_cast<const uint8*>( Request.Indices ), Request.Indices ? Request.NumIndices * static_cast<int32>( sizeof( uint32 ) ) : 0 ); // 添字なしも別形状として扱う。
		if ( MeshBuffers.Acquire( VertexBytes, IndexBytes, Shared.VertexBuffer, Shared.IndexBuffer ) )
		{
			Shared.Pipeline = Pipeline;
			Shared.VertexCount = static_cast<uint32>( Request.NumVertices );
			Shared.IndexCount = Request.Indices ? static_cast<uint32>( Request.NumIndices ) : 0u;
			Shared.IndexFormat = EGeometryIndexFormat::UInt32;
			Shared.Constants = TArrayView<const FGraphicsConstantBytes>( Constants.GetData(), Constants.Num() );
			Shared.Textures = TArrayView<const FTextureResourceHandle>( SlotTextures.GetData(), SlotTextures.Num() );
			if ( Geometry.DrawGraphicsPipeline( Shared ) ) { return true; }
		}
	}
	FGraphicsPipelineTransientDraw Draw; // 初回・登録失敗・共有描画の拒否では、何も描く前に従来の経路へ戻る。
	Draw.Pipeline = Pipeline;
	Draw.Vertices = Request.Vertices;
	Draw.VertexCount = static_cast<uint32>( Request.NumVertices );
	Draw.Indices = Request.Indices;
	Draw.IndexCount = Request.Indices ? static_cast<uint32>( Request.NumIndices ) : 0u;
	Draw.Constants = TArrayView<const FGraphicsConstantBytes>( Constants.GetData(), Constants.Num() );
	Draw.Textures = TArrayView<const FTextureResourceHandle>( SlotTextures.GetData(), SlotTextures.Num() );
	const bool bDrawn = Geometry.DrawGraphicsPipelineTransient( Draw );
	if ( !bDrawn )
	{
		NBackendSupport::ReportUnsupported( "DX12", "Shader draw (rejected by the GPU path)", "DrawGraphicsPipelineTransient rejected the draw (check the pass, formats, constants and textures)" );
	}
	return bDrawn;
}
