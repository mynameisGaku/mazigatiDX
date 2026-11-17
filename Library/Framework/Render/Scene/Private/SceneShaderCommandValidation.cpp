// SPDX-License-Identifier: MIT
#include "SceneShaderCommandValidation.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"

bool NSceneShaderCommand::IsState( const FDrawCommandView& Command )
{
	if ( !Command.Data || Command.Flags != 0 ) { return false; }
	if ( Command.Type == EDrawCommand::UseShader && Command.NumDataWords == sizeof( FDrawUseShaderData ) / 4 )
	{
		const FDrawUseShaderData Data = Command.Read<FDrawUseShaderData>(); // 検査済み固定部。
		return Data.Stage >= 0 && Data.Stage <= 1 && Data.Handle >= -1;
	}
	if ( Command.Type == EDrawCommand::ShaderTexture && Command.NumDataWords == sizeof( FDrawShaderTextureData ) / 4 )
	{
		const FDrawShaderTextureData Data = Command.Read<FDrawShaderTextureData>(); // 画像の段と借用番号。
		return Data.Stage >= 0 && Data.Stage < NDrawShader::NumTextureStages && Data.Handle >= -1;
	}
	if ( Command.Type == EDrawCommand::ShaderConstants && Command.CanRead<FDrawShaderConstantsData>() )
	{
		const FDrawShaderConstantsData Data = Command.Read<FDrawShaderConstantsData>(); // バイト数を先に検査する固定部。
		return Data.Stage >= 0 && Data.Stage <= 1 && Data.Slot >= NDrawShader::FirstUserSlot && Data.Slot <= NDrawShader::LastUserSlot && Data.NumBytes >= 16 && Data.NumBytes <= NDrawShader::MaxConstantBytes && Data.NumBytes % 16 == 0 && Command.NumDataWords == static_cast<int32>( sizeof( Data ) / 4 ) + Data.NumBytes / 4;
	}
	return false;
}

bool NSceneShaderCommand::IsDraw( const FDrawCommandView& Command )
{
	if ( Command.Type != EDrawCommand::PrimitiveShader3D || !Command.Data || Command.Flags != 0 || !Command.CanRead<FDrawPrimitiveShader3DData>() ) { return false; }
	const FDrawPrimitiveShader3DData Data = Command.Read<FDrawPrimitiveShader3DData>(); // 頂点・添字を読む前の個数。
	constexpr int32 VertexWords = sizeof( FDrawShaderVertex3D ) / 4; // 既存の88バイト頂点形式。
	constexpr int32 HeaderWords = sizeof( FDrawPrimitiveShader3DData ) / 4; // 命令内の固定設定。
	if ( Data.NumVertices < 1 || Data.NumVertices > NDrawCommand::MaxWords / VertexWords || Data.NumIndices < 0 || Data.NumIndices > NDrawCommand::MaxWords * 2 || Data.Reserved != 0 ) { return false; }
	if ( !NDrawPrimitive::IsValidCount( static_cast<EDrawPrimitive>( Data.Type ), Data.NumIndices > 0 ? Data.NumIndices : Data.NumVertices ) || FDrawPrimitiveShader3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) != static_cast<size_t>( Command.NumDataWords ) + 1 ) { return false; }
	for ( int32 Index = 0; Index < Data.NumVertices; ++Index )
	{
		FDrawShaderVertex3D Vertex; // 整列されていない入力も所有値へコピーして検査する。
		FMemory::Memcpy( &Vertex, Command.Data + HeaderWords + Index * VertexWords, sizeof( Vertex ) );
		float32 Values[20]; // 色以外の全浮動小数点値。
		FMemory::Memcpy( Values, &Vertex, sizeof( float32 ) * 16 );
		Values[16] = Vertex.U;
		Values[17] = Vertex.V;
		Values[18] = Vertex.SubU;
		Values[19] = Vertex.SubV;
		for ( float32 Value : Values ) { if ( !NMath::IsFinite( Value ) ) { return false; } }
	}
	const uint8* Indices = reinterpret_cast<const uint8*>( Command.Data + HeaderWords + Data.NumVertices * VertexWords ); // 全体長が一致した後だけ参照する添字列。
	for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
	{
		uint16 Value = 0; // 一つの添字の所有コピー。
		FMemory::Memcpy( &Value, Indices + Index * sizeof( uint16 ), sizeof( Value ) );
		if ( Value >= Data.NumVertices ) { return false; }
	}
	return true;
}
