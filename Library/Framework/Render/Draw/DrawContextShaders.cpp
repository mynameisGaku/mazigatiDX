#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

bool FDrawContextShaders::PushUseShader( EShaderStage Stage, FShaderHandle Shader )
{
	if ( !Shader.IsValid() || Shader.Stage != Stage )
	{
		return false;
	}
	FDrawUseShaderData Data;
	Data.Stage = static_cast<int32>( Stage );
	Data.Handle = Shader.Value;
	Push( EDrawCommand::UseShader, 0, Data );
	return true;
}

bool FDrawContextShaders::UseVertexShader( FShaderHandle Shader )
{
	return PushUseShader( EShaderStage::Vertex, Shader );
}

bool FDrawContextShaders::UsePixelShader( FShaderHandle Shader )
{
	return PushUseShader( EShaderStage::Pixel, Shader );
}

void FDrawContextShaders::StopUsingVertexShader()
{
	FDrawUseShaderData Data;
	Data.Stage = static_cast<int32>( EShaderStage::Vertex );
	Data.Handle = -1;
	Push( EDrawCommand::UseShader, 0, Data );
}

void FDrawContextShaders::StopUsingPixelShader()
{
	FDrawUseShaderData Data;
	Data.Stage = static_cast<int32>( EShaderStage::Pixel );
	Data.Handle = -1;
	Push( EDrawCommand::UseShader, 0, Data );
}

bool FDrawContextShaders::ShaderConstants( EShaderStage Stage, int32 Slot, const void* Data, int32 NumBytes )
{
	if ( ( Stage != EShaderStage::Vertex && Stage != EShaderStage::Pixel ) || Slot < NDrawShader::FirstUserSlot || Slot > NDrawShader::LastUserSlot
		|| Data == nullptr || NumBytes < 16 || NumBytes > NDrawShader::MaxConstantBytes || NumBytes % 16 != 0 )
	{
		return false;
	}
	constexpr int32 HeaderWords = static_cast<int32>( sizeof( FDrawShaderConstantsData ) / 4 );
	const int32 CommandWords = 1 + HeaderWords + NumBytes / 4;
	TArray<uint32>& Words = Buffer->Words;
	if ( Words.Num() > TNumericLimits<int32>::Max() - CommandWords )
	{
		return false;
	}
	FDrawShaderConstantsData Header;
	Header.Stage = static_cast<int32>( Stage );
	Header.Slot = Slot;
	Header.NumBytes = NumBytes;
	const int32 Begin = Words.AddZeroed( CommandWords );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::ShaderConstants, 0, CommandWords );
	FMemory::Memcpy( Out + 1, &Header, sizeof( Header ) );
	FMemory::Memcpy( Out + 1 + HeaderWords, Data, static_cast<size_t>( NumBytes ) );
	return true;
}

bool FDrawContextShaders::ShaderTexture( int32 Stage, FImageHandle Image )
{
	if ( Stage < 0 || Stage >= NDrawShader::NumTextureStages || !Image.IsValid() )
	{
		return false;
	}
	FDrawShaderTextureData Data;
	Data.Stage = Stage;
	Data.Handle = Image.Value;
	Push( EDrawCommand::ShaderTexture, 0, Data );
	return true;
}

void FDrawContextShaders::StopUsingShaderTexture( int32 Stage )
{
	if ( Stage < 0 || Stage >= NDrawShader::NumTextureStages )
	{
		return;
	}
	FDrawShaderTextureData Data;
	Data.Stage = Stage;
	Data.Handle = -1;
	Push( EDrawCommand::ShaderTexture, 0, Data );
}

bool FDrawContextShaders::ShaderOutputTarget( int32 Index, FImageHandle Target )
{
	if ( Index < 1 || Index >= NDrawShader::NumOutputTargets || !Target.IsValid() )
	{
		return false;
	}
	FDrawShaderOutputTargetData Data;
	Data.Index = Index;
	Data.Handle = Target.Value;
	Push( EDrawCommand::ShaderOutputTarget, 0, Data );
	return true;
}

void FDrawContextShaders::StopUsingShaderOutputTarget( int32 Index )
{
	if ( Index < 1 || Index >= NDrawShader::NumOutputTargets )
	{
		return;
	}
	FDrawShaderOutputTargetData Data;
	Data.Index = Index;
	Data.Handle = -1;
	Push( EDrawCommand::ShaderOutputTarget, 0, Data );
}

bool FDrawContextShaders::PrimitiveShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type )
{
	return PushPrimitiveShader3D( Vertices, NumVertices, nullptr, 0, Type );
}

bool FDrawContextShaders::PrimitiveIndexedShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type )
{
	return Indices != nullptr && PushPrimitiveShader3D( Vertices, NumVertices, Indices, NumIndices, Type );
}

bool FDrawContextShaders::PushPrimitiveShader3D( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type )
{
	if ( Vertices == nullptr )
	{
		return false;
	}
	const bool bIndexed = Indices != nullptr;
	if ( !bIndexed && NumIndices != 0 )
	{
		return false;
	}
	// つなぎ方に合う数か（インデックスがあればインデックスの数、なければ頂点の数）。範囲の外のつなぎ方と、0 以下の数は、使えない数なので、ここで弾かれる
	if ( !NDrawPrimitive::IsValidCount( Type, bIndexed ? NumIndices : NumVertices ) )
	{
		return false;
	}
	// 1 つの命令に収まる大きさか
	const size_t CommandWords = FDrawPrimitiveShader3DData::GetCommandWords( NumVertices, NumIndices );
	if ( CommandWords == 0 || CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return false;
	}
	// 一部の頂点だけ捨てると、形が変わってしまうので、全部を先に確かめる
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		if ( !Vertices[Index].IsValid() )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < NumIndices; ++Index )
	{
		if ( Indices[Index] >= NumVertices )
		{
			return false;
		}
	}

	constexpr size_t DataWords = sizeof( FDrawPrimitiveShader3DData ) / 4;
	constexpr size_t VertexWords = sizeof( FDrawShaderVertex3D ) / 4;
	FDrawPrimitiveShader3DData Data;
	Data.Type = static_cast<int32>( Type );
	Data.NumVertices = NumVertices;
	Data.NumIndices = NumIndices;
	TArray<uint32>& Words = Buffer->Words;
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::PrimitiveShader3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		const FDrawShaderVertex3D Vertex = Vertices[Index].ToDrawData();
		FMemory::Memcpy( Out + 1 + DataWords + static_cast<size_t>( Index ) * VertexWords, &Vertex, sizeof( Vertex ) );
	}
	if ( bIndexed )
	{
		// 2 個で 1 ワード。奇数個の最後の半分は、AddZeroed の 0 のまま
		FMemory::Memcpy( Out + 1 + DataWords + static_cast<size_t>( NumVertices ) * VertexWords, Indices, static_cast<size_t>( NumIndices ) * sizeof( uint16 ) );
	}
	return true;
}

bool FDrawContextShaders::ShaderRect( float32 X, float32 Y, float32 Width, float32 Height, const FColor32& Color )
{
	return ShaderRect( X, Y, Width, Height, 0.0f, 0.0f, 1.0f, 1.0f, Color );
}

bool FDrawContextShaders::ShaderRect( float32 X, float32 Y, float32 Width, float32 Height, float32 U0, float32 V0, float32 U1, float32 V1, const FColor32& Color )
{
	if ( !AllDrawable( X, Y, Width, Height ) || Width <= 0.0f || Height <= 0.0f || !NMath::IsFinite( U0 ) || !NMath::IsFinite( V0 ) || !NMath::IsFinite( U1 ) || !NMath::IsFinite( V1 ) )
	{
		return false;
	}
	FDrawShaderRectData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Width = Width;
	Data.Height = Height;
	Data.U0 = U0;
	Data.V0 = V0;
	Data.U1 = U1;
	Data.V1 = V1;
	Data.Color[0] = Color.r;
	Data.Color[1] = Color.g;
	Data.Color[2] = Color.b;
	Data.Color[3] = Color.a;
	Push( EDrawCommand::ShaderRect, 0, Data );
	return true;
}

bool FDrawContextShaders::ShaderQuad3D( const FDrawQuad3DData& Data, const FVector4& Extra )
{
	const float32 Numbers[] = { Data.Pos[0], Data.Pos[1], Data.Pos[2], Data.Right[0], Data.Right[1], Data.Right[2], Data.Up[0], Data.Up[1], Data.Up[2], Data.Width, Data.Height, Data.PivotX, Data.PivotY, Data.Angle, Data.U0, Data.V0, Data.U1, Data.V1,
								Extra.x, Extra.y, Extra.z, Extra.w };
	for ( const float32 Number : Numbers )
	{
		if ( !NMath::IsFinite( Number ) )
		{
			return false;
		}
	}
	if ( Data.Width <= 0.0f || Data.Height <= 0.0f )
	{
		return false;
	}
	FDrawShaderQuad3DData Command;
	Command.Quad = Data;
	Command.Extra[0] = Extra.x;
	Command.Extra[1] = Extra.y;
	Command.Extra[2] = Extra.z;
	Command.Extra[3] = Extra.w;
	Push( EDrawCommand::ShaderQuad3D, 0, Command );
	return true;
}
