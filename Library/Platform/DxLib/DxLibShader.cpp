// SPDX-License-Identifier: MIT
#include "DxLibShader.h"

#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Math/Math.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	int ToDxPrimitive( EDrawPrimitive Type )
	{
		switch ( Type )
		{
		case EDrawPrimitive::Points: return DX_PRIMTYPE_POINTLIST;
		case EDrawPrimitive::Lines: return DX_PRIMTYPE_LINELIST;
		case EDrawPrimitive::LineStrip: return DX_PRIMTYPE_LINESTRIP;
		case EDrawPrimitive::Triangles: return DX_PRIMTYPE_TRIANGLELIST;
		case EDrawPrimitive::TriangleStrip: return DX_PRIMTYPE_TRIANGLESTRIP;
		case EDrawPrimitive::TriangleFan: return DX_PRIMTYPE_TRIANGLEFAN;
		}
		return DX_PRIMTYPE_TRIANGLELIST;
	}

	// 空の段に置く、1 x 1 の透明な黒い画像。DxLib は、SetUseTextureToShader( 段, -1 ) で外しても、直前の画像を段に残す（実測: 同じフレームの中でも、次の実行でも読める）ので、
	// 外すときは、これを置く（読むと 0 で、画像を置かなかった段と同じ結果になる）。DxLib_End まで持つ。InitGraph などで消えていたら、作り直す
	int GetEmptyTexture()
	{
		static int Texture = -1;
		int Width = 0;
		int Height = 0;
		if ( Texture >= 0 && GetGraphSize( Texture, &Width, &Height ) == 0 )
		{
			return Texture;
		}
		const int Soft = MakeARGB8ColorSoftImage( 1, 1 );
		if ( Soft < 0 )
		{
			return -1;
		}
		DrawPixelSoftImage( Soft, 0, 0, 0, 0, 0, 0 );
		Texture = CreateGraphFromSoftImage( Soft );
		DeleteSoftImage( Soft );
		return Texture;
	}

	bool IsStage( int32 Stage )
	{
		return Stage == static_cast<int32>( EShaderStage::Vertex ) || Stage == static_cast<int32>( EShaderStage::Pixel );
	}

	int ToDxShaderType( int32 Stage )
	{
		return Stage == static_cast<int32>( EShaderStage::Pixel ) ? DX_SHADERTYPE_PIXEL : DX_SHADERTYPE_VERTEX;
	}

	bool IsFinite3( const float32 ( &Values )[3] )
	{
		return NMath::IsFinite( Values[0] ) && NMath::IsFinite( Values[1] ) && NMath::IsFinite( Values[2] );
	}

	// 命令の頂点（R, G, B, A の順）を、DxLib の頂点にする。数値が有限でなければ false
	bool ToDxVertex( const FDrawShaderVertex3D& Source, VERTEX3DSHADER& Out )
	{
		if ( !IsFinite3( Source.Position ) || !IsFinite3( Source.Normal ) || !IsFinite3( Source.Tangent ) || !IsFinite3( Source.Binormal )
			|| !NMath::IsFinite( Source.SubPosition[0] ) || !NMath::IsFinite( Source.SubPosition[1] ) || !NMath::IsFinite( Source.SubPosition[2] ) || !NMath::IsFinite( Source.SubPosition[3] )
			|| !NMath::IsFinite( Source.U ) || !NMath::IsFinite( Source.V ) || !NMath::IsFinite( Source.SubU ) || !NMath::IsFinite( Source.SubV ) )
		{
			return false;
		}
		FMemory::Memzero( &Out, sizeof( Out ) );
		Out.pos = VGet( Source.Position[0], Source.Position[1], Source.Position[2] );
		Out.spos = FLOAT4 { Source.SubPosition[0], Source.SubPosition[1], Source.SubPosition[2], Source.SubPosition[3] };
		Out.norm = VGet( Source.Normal[0], Source.Normal[1], Source.Normal[2] );
		Out.tan = VGet( Source.Tangent[0], Source.Tangent[1], Source.Tangent[2] );
		Out.binorm = VGet( Source.Binormal[0], Source.Binormal[1], Source.Binormal[2] );
		Out.dif = GetColorU8( Source.Diffuse[0], Source.Diffuse[1], Source.Diffuse[2], Source.Diffuse[3] );
		Out.spc = GetColorU8( Source.Specular[0], Source.Specular[1], Source.Specular[2], Source.Specular[3] );
		Out.u = Source.U;
		Out.v = Source.V;
		Out.su = Source.SubU;
		Out.sv = Source.SubV;
		return true;
	}

	// ShaderRect の組み込みの頂点シェーダー（クリップ座標をそのまま渡し、頂点の色と画像の座標をピクセルシェーダーへ渡す。vs_4_0。HLSL は下のとおり。fxc でコンパイルしたバイト列）。
	// 実行のときのコンパイルに頼らず、d3dcompiler_47.dll が無い環境でも使えるよう、バイト列を埋め込んでいる
	//   struct VI { float3 Position : POSITION; float4 SubPosition : POSITION1; float3 Normal : NORMAL; float3 Tangent : TANGENT; float3 Binormal : BINORMAL;
	//               float4 Diffuse : COLOR0; float4 Specular : COLOR1; float2 Uv : TEXCOORD0; float2 SubUv : TEXCOORD1; };
	//   struct VO { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 Uv : TEXCOORD0; };
	//   VO main( VI i ) { VO o; o.Position = float4( i.Position, 1 ); o.Color = i.Diffuse; o.Uv = i.Uv; return o; }
	const unsigned char RectVertexShaderCode[] =
	{
		0x44, 0x58, 0x42, 0x43, 0x83, 0xB1, 0xD2, 0x91, 0x60, 0x30, 0x87, 0xE5, 0x1F, 0x9B, 0xA2, 0x40,
		0xC1, 0xD2, 0x10, 0x33, 0x01, 0x00, 0x00, 0x00, 0x38, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
		0x34, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x98, 0x01, 0x00, 0x00, 0x0C, 0x02, 0x00, 0x00,
		0xBC, 0x02, 0x00, 0x00, 0x52, 0x44, 0x45, 0x46, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x04, 0xFE, 0xFF,
		0x00, 0x81, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66,
		0x74, 0x20, 0x28, 0x52, 0x29, 0x20, 0x48, 0x4C, 0x53, 0x4C, 0x20, 0x53, 0x68, 0x61, 0x64, 0x65,
		0x72, 0x20, 0x43, 0x6F, 0x6D, 0x70, 0x69, 0x6C, 0x65, 0x72, 0x20, 0x31, 0x30, 0x2E, 0x31, 0x00,
		0x49, 0x53, 0x47, 0x4E, 0x10, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
		0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
		0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
		0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00,
		0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00,
		0x07, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x50, 0x4F, 0x53, 0x49, 0x54, 0x49, 0x4F, 0x4E,
		0x00, 0x4E, 0x4F, 0x52, 0x4D, 0x41, 0x4C, 0x00, 0x54, 0x41, 0x4E, 0x47, 0x45, 0x4E, 0x54, 0x00,
		0x42, 0x49, 0x4E, 0x4F, 0x52, 0x4D, 0x41, 0x4C, 0x00, 0x43, 0x4F, 0x4C, 0x4F, 0x52, 0x00, 0x54,
		0x45, 0x58, 0x43, 0x4F, 0x4F, 0x52, 0x44, 0x00, 0x4F, 0x53, 0x47, 0x4E, 0x6C, 0x00, 0x00, 0x00,
		0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
		0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x0C, 0x00, 0x00,
		0x53, 0x56, 0x5F, 0x50, 0x4F, 0x53, 0x49, 0x54, 0x49, 0x4F, 0x4E, 0x00, 0x43, 0x4F, 0x4C, 0x4F,
		0x52, 0x00, 0x54, 0x45, 0x58, 0x43, 0x4F, 0x4F, 0x52, 0x44, 0x00, 0xAB, 0x53, 0x48, 0x44, 0x52,
		0xA8, 0x00, 0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x03,
		0x72, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x03, 0xF2, 0x10, 0x10, 0x00,
		0x05, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x03, 0x32, 0x10, 0x10, 0x00, 0x07, 0x00, 0x00, 0x00,
		0x67, 0x00, 0x00, 0x04, 0xF2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x65, 0x00, 0x00, 0x03, 0xF2, 0x20, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x03,
		0x32, 0x20, 0x10, 0x00, 0x02, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x05, 0x72, 0x20, 0x10, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x46, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x05,
		0x82, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,
		0x36, 0x00, 0x00, 0x05, 0xF2, 0x20, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x46, 0x1E, 0x10, 0x00,
		0x05, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x05, 0x32, 0x20, 0x10, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x46, 0x10, 0x10, 0x00, 0x07, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x01, 0x53, 0x54, 0x41, 0x54,
		0x74, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	// 組み込みの頂点シェーダーの DxLib のハンドル。DxLib_End まで持つ。InitShader などで消えていたら、作り直す（SetUseVertexShader が断るかで確かめる）
	int GetRectVertexShader( int UserVertexShader )
	{
		static int Shader = -1;
		if ( Shader >= 0 && SetUseVertexShader( Shader ) == 0 )
		{
			return Shader;
		}
		Shader = LoadVertexShaderFromMem( RectVertexShaderCode, static_cast<int>( sizeof( RectVertexShaderCode ) ) );
		if ( Shader >= 0 && SetUseVertexShader( Shader ) == 0 )
		{
			return Shader;
		}
		// 組み込みのシェーダーを使えないので、利用者の頂点シェーダーに戻す
		SetUseVertexShader( UserVertexShader );
		return -1;
	}
}

void FDxLibShader::SetEmptyTexture( int32 Stage )
{
	const int Empty = GetEmptyTexture();
	SetUseTextureToShader( Stage, Empty >= 0 ? Empty : -1 );
}

bool FDxLibShader::IsStateCommand( EDrawCommand Type )
{
	return Type == EDrawCommand::UseShader || Type == EDrawCommand::ShaderConstants || Type == EDrawCommand::ShaderTexture;
}

bool FDxLibShader::IsBlockedInPass( EDrawCommand Type )
{
	return Type == EDrawCommand::PrimitiveShader3D || Type == EDrawCommand::ShaderRect;
}

bool FDxLibShader::Apply( const FDrawCommandView& Command )
{
	switch ( Command.Type )
	{
	case EDrawCommand::UseShader:
		UseShader( Command );
		return true;
	case EDrawCommand::ShaderConstants:
		SetConstants( Command );
		return true;
	case EDrawCommand::ShaderTexture:
		SetTexture( Command );
		return true;
	case EDrawCommand::PrimitiveShader3D:
		DrawPrimitive3D( Command );
		return true;
	case EDrawCommand::ShaderRect:
		DrawRect( Command );
		return true;
	default:
		return false;
	}
}

void FDxLibShader::UseShader( const FDrawCommandView& Command )
{
	if ( !Command.CanRead<FDrawUseShaderData>() )
	{
		return;
	}
	const FDrawUseShaderData Data = Command.Read<FDrawUseShaderData>();
	if ( !IsStage( Data.Stage ) )
	{
		return;
	}
	const bool bVertex = Data.Stage == static_cast<int32>( EShaderStage::Vertex );
	// DxLib が断る（解放済み・種類の違うシェーダー）なら、そのシェーダーは使わない（前のシェーダーで描いてしまわないように、外す）
	int Handle = Data.Handle;
	if ( Handle >= 0 )
	{
		const int Result = bVertex ? SetUseVertexShader( Handle ) : SetUsePixelShader( Handle );
		if ( Result != 0 )
		{
			Handle = -1;
		}
	}
	if ( Handle < 0 )
	{
		if ( bVertex )
		{
			SetUseVertexShader( -1 );
		}
		else
		{
			SetUsePixelShader( -1 );
		}
	}
	( bVertex ? VertexShader : PixelShader ) = Handle;
}

void FDxLibShader::SetConstants( const FDrawCommandView& Command )
{
	if ( !Command.CanRead<FDrawShaderConstantsData>() )
	{
		return;
	}
	const FDrawShaderConstantsData Data = Command.Read<FDrawShaderConstantsData>();
	constexpr int32 HeaderWords = static_cast<int32>( sizeof( FDrawShaderConstantsData ) / 4 );
	// 壊れた命令（使えないスロット・大きさ。命令の大きさが、中身にぴったり合わない）は、実行しない
	if ( !IsStage( Data.Stage ) || Data.Slot < NDrawShader::FirstUserSlot || Data.Slot > NDrawShader::LastUserSlot
		|| Data.NumBytes < 16 || Data.NumBytes > NDrawShader::MaxConstantBytes || Data.NumBytes % 16 != 0 || Command.NumDataWords != HeaderWords + Data.NumBytes / 4 )
	{
		return;
	}

	FConstantBuffer* Found = nullptr;
	for ( FConstantBuffer& Existing : ConstantBuffers )
	{
		if ( Existing.Stage == Data.Stage && Existing.Slot == Data.Slot )
		{
			Found = &Existing;
			break;
		}
	}
	// 大きさが足りなければ、作り直す（定数バッファの大きさは、作ったあと変えられない）
	if ( Found != nullptr && Found->NumBytes < Data.NumBytes )
	{
		DeleteShaderConstantBuffer( Found->Buffer );
		ConstantBuffers.RemoveAt( static_cast<int32>( Found - ConstantBuffers.GetData() ) );
		Found = nullptr;
	}
	if ( Found == nullptr )
	{
		const int Buffer = CreateShaderConstantBuffer( Data.NumBytes );
		if ( Buffer < 0 )
		{
			return;
		}
		FConstantBuffer Created;
		Created.Stage = Data.Stage;
		Created.Slot = Data.Slot;
		Created.Buffer = Buffer;
		Created.NumBytes = Data.NumBytes;
		const int32 Index = ConstantBuffers.Add( Created );
		Found = &ConstantBuffers[Index];
	}
	void* const Memory = GetBufferShaderConstantBuffer( Found->Buffer );
	if ( Memory == nullptr )
	{
		return;
	}
	// 大きさが余るときは、残りを 0 にする（前の中身が残らないように）
	FMemory::Memzero( Memory, static_cast<size_t>( Found->NumBytes ) );
	FMemory::Memcpy( Memory, Command.Data + HeaderWords, static_cast<size_t>( Data.NumBytes ) );
	UpdateShaderConstantBuffer( Found->Buffer );
	SetShaderConstantBuffer( Found->Buffer, ToDxShaderType( Data.Stage ), Data.Slot );
}

void FDxLibShader::SetTexture( const FDrawCommandView& Command )
{
	if ( !Command.CanRead<FDrawShaderTextureData>() )
	{
		return;
	}
	const FDrawShaderTextureData Data = Command.Read<FDrawShaderTextureData>();
	if ( Data.Stage < 0 || Data.Stage >= NDrawShader::NumTextureStages )
	{
		return;
	}
	// DxLib が断る（解放済みの画像）なら、その段は空にする。空にするときは、-1 でなく、空の画像を置く（-1 では、直前の画像が残る）
	if ( Data.Handle >= 0 && SetUseTextureToShader( Data.Stage, Data.Handle ) == 0 )
	{
		bTextureUsed[Data.Stage] = true;
		return;
	}
	SetEmptyTexture( Data.Stage );
	bTextureUsed[Data.Stage] = true;
}

void FDxLibShader::DrawPrimitive3D( const FDrawCommandView& Command )
{
	if ( !Command.CanRead<FDrawPrimitiveShader3DData>() )
	{
		return;
	}
	const FDrawPrimitiveShader3DData Data = Command.Read<FDrawPrimitiveShader3DData>();
	const EDrawPrimitive Type = static_cast<EDrawPrimitive>( Data.Type );
	const bool bIndexed = Data.NumIndices > 0;
	// 範囲の外のつなぎ方と、0 以下の数は、使えない数として弾かれる（DxLib は、負のポリゴン数で落ちる）
	if ( !NDrawPrimitive::IsValidCount( Type, bIndexed ? Data.NumIndices : Data.NumVertices ) )
	{
		return;
	}
	// 命令の大きさ（ヘッダーの 1 ワードも含む）が、頂点と添字の数にぴったり合うか
	if ( static_cast<size_t>( Command.NumDataWords ) + 1 != FDrawPrimitiveShader3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) )
	{
		return;
	}
	// シェーダーが決まっていないと、DxLib は何も描かない（頂点シェーダーとピクセルシェーダーの両方）。渡さない
	if ( !CanDraw() )
	{
		return;
	}

	constexpr size_t DataWords = sizeof( FDrawPrimitiveShader3DData ) / 4;
	constexpr size_t VertexWords = sizeof( FDrawShaderVertex3D ) / 4;
	TArray<VERTEX3DSHADER> Vertices;
	Vertices.SetNum( Data.NumVertices );
	for ( int32 Index = 0; Index < Data.NumVertices; ++Index )
	{
		FDrawShaderVertex3D Source;
		FMemory::Memcpy( &Source, Command.Data + DataWords + static_cast<size_t>( Index ) * VertexWords, sizeof( Source ) );
		if ( !ToDxVertex( Source, Vertices[Index] ) )
		{
			return;
		}
	}
	if ( !bIndexed )
	{
		DrawPrimitive3DToShader( Vertices.GetData(), Data.NumVertices, ToDxPrimitive( Type ) );
		return;
	}

	TArray<unsigned short> Indices;
	Indices.SetNum( Data.NumIndices );
	FMemory::Memcpy( Indices.GetData(), Command.Data + DataWords + static_cast<size_t>( Data.NumVertices ) * VertexWords, static_cast<size_t>( Data.NumIndices ) * sizeof( unsigned short ) );
	for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
	{
		if ( Indices[Index] >= Data.NumVertices )
		{
			return;
		}
	}
	DrawPrimitiveIndexed3DToShader( Vertices.GetData(), Data.NumVertices, Indices.GetData(), Data.NumIndices, ToDxPrimitive( Type ) );
}

void FDxLibShader::DrawRect( const FDrawCommandView& Command )
{
	if ( !Command.CanRead<FDrawShaderRectData>() )
	{
		return;
	}
	const FDrawShaderRectData Data = Command.Read<FDrawShaderRectData>();
	if ( !NMath::IsFinite( Data.X ) || !NMath::IsFinite( Data.Y ) || !NMath::IsFinite( Data.Width ) || !NMath::IsFinite( Data.Height )
		|| !NMath::IsFinite( Data.U0 ) || !NMath::IsFinite( Data.V0 ) || !NMath::IsFinite( Data.U1 ) || !NMath::IsFinite( Data.V1 ) || Data.Width <= 0.0f || Data.Height <= 0.0f )
	{
		return;
	}
	// ピクセルシェーダーが決まっていないと、DxLib は何も描かない（頂点シェーダーは、組み込みのものを使うので、決まっていなくてよい）
	if ( PixelShader < 0 )
	{
		return;
	}
	// いまの描画先の大きさ。描画先が画像（MakeScreen で作った描画先）なら、その画像の大きさ。画面（バックバッファ）なら、画面の大きさ
	int ScreenWidth = 0;
	int ScreenHeight = 0;
	if ( GetGraphSize( GetDrawScreen(), &ScreenWidth, &ScreenHeight ) != 0 )
	{
		GetDrawScreenSize( &ScreenWidth, &ScreenHeight );
	}
	if ( ScreenWidth < 1 || ScreenHeight < 1 )
	{
		return;
	}
	const int RectVertex = GetRectVertexShader( VertexShader );
	if ( RectVertex < 0 )
	{
		return;
	}
	// 画素の座標を、クリップ座標（x は -1〜1、y は上が +1）に直す。左上が (0, 0)。奥行きは 0（いちばん手前）
	const float Left = Data.X / static_cast<float>( ScreenWidth ) * 2.0f - 1.0f;
	const float Right = ( Data.X + Data.Width ) / static_cast<float>( ScreenWidth ) * 2.0f - 1.0f;
	const float Top = 1.0f - Data.Y / static_cast<float>( ScreenHeight ) * 2.0f;
	const float Bottom = 1.0f - ( Data.Y + Data.Height ) / static_cast<float>( ScreenHeight ) * 2.0f;
	const float PositionsX[4] = { Left, Right, Left, Right };
	const float PositionsY[4] = { Top, Top, Bottom, Bottom };
	const float Us[4] = { Data.U0, Data.U1, Data.U0, Data.U1 };
	const float Vs[4] = { Data.V0, Data.V0, Data.V1, Data.V1 };
	VERTEX3DSHADER Vertices[4];
	for ( int Index = 0; Index < 4; ++Index )
	{
		FMemory::Memzero( &Vertices[Index], sizeof( VERTEX3DSHADER ) );
		Vertices[Index].pos = VGet( PositionsX[Index], PositionsY[Index], 0.0f );
		Vertices[Index].dif = GetColorU8( Data.Color[0], Data.Color[1], Data.Color[2], Data.Color[3] );
		Vertices[Index].u = Us[Index];
		Vertices[Index].v = Vs[Index];
	}
	DrawPrimitive3DToShader( Vertices, 4, DX_PRIMTYPE_TRIANGLESTRIP );
	// 利用者の頂点シェーダーに戻す
	SetUseVertexShader( VertexShader );
}

void FDxLibShader::Restore()
{
	if ( VertexShader >= 0 )
	{
		SetUseVertexShader( -1 );
		VertexShader = -1;
	}
	if ( PixelShader >= 0 )
	{
		SetUsePixelShader( -1 );
		PixelShader = -1;
	}
	for ( int32 Stage = 0; Stage < NDrawShader::NumTextureStages; ++Stage )
	{
		if ( bTextureUsed[Stage] )
		{
			SetEmptyTexture( Stage );
			bTextureUsed[Stage] = false;
		}
	}
	// 定数バッファは、消す前に中身を 0 にして反映する（DxLib は、消したあとも、最後に反映した中身を、スロットに残す（実測）。次のオブジェクトに、この中身が漏れないように）
	for ( const FConstantBuffer& Existing : ConstantBuffers )
	{
		void* const Memory = GetBufferShaderConstantBuffer( Existing.Buffer );
		if ( Memory != nullptr )
		{
			FMemory::Memzero( Memory, static_cast<size_t>( Existing.NumBytes ) );
			UpdateShaderConstantBuffer( Existing.Buffer );
		}
		DeleteShaderConstantBuffer( Existing.Buffer );
	}
	ConstantBuffers.Reset();
}
