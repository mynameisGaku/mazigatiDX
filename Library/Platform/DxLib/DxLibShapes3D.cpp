// SPDX-License-Identifier: MIT
#include "DxLibShapes3D.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibVertex.h"

namespace
{
	unsigned int ToDxColor( const FDrawRgb& Color )
	{
		return GetColor( Color.R, Color.G, Color.B );
	}

	VECTOR ToVector( const float32 ( &Xyz )[3] )
	{
		return VGet( Xyz[0], Xyz[1], Xyz[2] );
	}

	template<size_t Count>
	bool AreFinite( const float32 ( &Values )[Count] )
	{
		for ( size_t Index = 0; Index < Count; ++Index )
		{
			if ( !NMath::IsFinite( Values[Index] ) )
			{
				return false;
			}
		}
		return true;
	}

	// 頂点の並びのつなぎ方（DX_PRIMTYPE_*）
	int ToDxPrimitive( EDrawPrimitive Type )
	{
		switch ( Type )
		{
		case EDrawPrimitive::Points:
			return DX_PRIMTYPE_POINTLIST;
		case EDrawPrimitive::Lines:
			return DX_PRIMTYPE_LINELIST;
		case EDrawPrimitive::LineStrip:
			return DX_PRIMTYPE_LINESTRIP;
		case EDrawPrimitive::Triangles:
			return DX_PRIMTYPE_TRIANGLELIST;
		case EDrawPrimitive::TriangleStrip:
			return DX_PRIMTYPE_TRIANGLESTRIP;
		case EDrawPrimitive::TriangleFan:
			return DX_PRIMTYPE_TRIANGLEFAN;
		}
		return DX_PRIMTYPE_TRIANGLELIST;
	}

	// 頂点の並びを、点・線・三角形として描く。命令の大きさ・数・添字が合わない、数値が有限でないときは、何も描かない（DxLib に、範囲の外を読ませない）
	void DrawPrimitive( const FDrawCommandView& Command )
	{
		if ( !Command.CanRead<FDrawPrimitive3DData>() )
		{
			return;
		}
		const FDrawPrimitive3DData Data = Command.Read<FDrawPrimitive3DData>();
		const EDrawPrimitive Type = static_cast<EDrawPrimitive>( Data.Type );
		const bool bIndexed = Data.NumIndices > 0;
		// 範囲の外のつなぎ方と、0 以下の頂点の数は、使えない数として弾かれる。添字の数が負のときと、頂点の数が負のときは、次の大きさの確認が弾く（GetCommandWords が 0 になる）
		if ( !NDrawPrimitive::IsValidCount( Type, bIndexed ? Data.NumIndices : Data.NumVertices ) )
		{
			return;
		}
		// 命令の大きさ（ヘッダーの 1 ワードも含む）が、頂点と添字の数にぴったり合うか
		if ( static_cast<size_t>( Command.NumDataWords ) + 1 != FDrawPrimitive3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) )
		{
			return;
		}

		constexpr size_t DataWords = sizeof( FDrawPrimitive3DData ) / 4;
		constexpr size_t VertexWords = sizeof( FDrawVertex3D ) / 4;
		TArray<VERTEX3D> Vertices;
		Vertices.SetNum( Data.NumVertices );
		for ( int32 Index = 0; Index < Data.NumVertices; ++Index )
		{
			FDrawVertex3D Source;
			FMemory::Memcpy( &Source, Command.Data + DataWords + static_cast<size_t>( Index ) * VertexWords, sizeof( Source ) );
			if ( !NDxLibVertex::ToDxVertex( Source, Vertices[Index] ) )
			{
				return;
			}
		}
		// 画像なしは、DX_NONE_GRAPH（-1 ではない。-1 だと何も描かれない）
		const int Graph = Data.Texture >= 0 ? Data.Texture : DX_NONE_GRAPH;
		if ( !bIndexed )
		{
			DrawPrimitive3D( Vertices.GetData(), Data.NumVertices, ToDxPrimitive( Type ), Graph, TRUE );
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
		DrawPrimitiveIndexed3D( Vertices.GetData(), Data.NumVertices, Indices.GetData(), Data.NumIndices, ToDxPrimitive( Type ), Graph, TRUE );
	}

	// 頂点バッファ（と、あればインデックスバッファ）の一部を描く。使う範囲・数が合わない壊れた命令は、描かない（DxLib は、添字なしの範囲を確かめないので、バッファの外を読ませない）
	void DrawPrimitiveBuffer( const FDrawCommandView& Command )
	{
		if ( !Command.CanRead<FDrawPrimitiveBuffer3DData>() )
		{
			return;
		}
		const FDrawPrimitiveBuffer3DData Data = Command.Read<FDrawPrimitiveBuffer3DData>();
		const EDrawPrimitive Type = static_cast<EDrawPrimitive>( Data.Type );
		const bool bIndexed = Data.IndexBuffer >= 0;
		// 使う範囲（添字ありなら添字の範囲、なしなら頂点の範囲）が、バッファの大きさに収まるか（足し算は、溢れないよう 64 ビットで）。範囲の外のつなぎ方は、使えない数として弾かれる
		const int32 NumBuffer = bIndexed ? Data.NumBufferIndices : Data.NumBufferVertices;
		if ( Data.Start < 0 || Data.Num < 1 || static_cast<int64>( Data.Start ) + Data.Num > NumBuffer || !NDrawPrimitive::IsValidCount( Type, Data.Num ) )
		{
			return;
		}
		// 画像なしは、DX_NONE_GRAPH（-1 ではない。-1 だと何も描かれない）
		const int Graph = Data.Texture >= 0 ? Data.Texture : DX_NONE_GRAPH;
		if ( !bIndexed )
		{
			DrawPrimitive3D_UseVertexBuffer2( Data.VertexBuffer, ToDxPrimitive( Type ), Data.Start, Data.Num, Graph, TRUE );
			return;
		}
		// 頂点の範囲は、バッファ全体を渡す（実測: 範囲は絵に影響しない。0 個だけ失敗する）
		DrawPrimitiveIndexed3D_UseVertexBuffer2( Data.VertexBuffer, Data.IndexBuffer, ToDxPrimitive( Type ), Data.BaseVertex, 0, Data.NumBufferVertices, Data.Start, Data.Num, Graph, TRUE );
	}
}

namespace NDxLibShapes3D
{
	bool Execute( const FDrawCommandView& Command )
	{
		const int FlipX = Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE;
		const int FlipY = Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE;
		switch ( Command.Type )
		{
		case EDrawCommand::Pixel3D:
		{
			if ( Command.CanRead<FDrawPixel3DData>() )
			{
				const FDrawPixel3DData Data = Command.Read<FDrawPixel3DData>();
				if ( AreFinite( Data.Pos ) )
				{
					DrawPixel3D( ToVector( Data.Pos ), ToDxColor( Data.Color ) );
				}
			}
			return true;
		}
		case EDrawCommand::Cone3D:
		{
			if ( Command.CanRead<FDrawCone3DData>() )
			{
				const FDrawCone3DData Data = Command.Read<FDrawCone3DData>();
				if ( AreFinite( Data.Top ) && AreFinite( Data.Bottom ) && NMath::IsFinite( Data.Radius ) )
				{
					DrawCone3D( ToVector( Data.Top ), ToVector( Data.Bottom ), Data.Radius, Data.Div, ToDxColor( Data.Diffuse ), ToDxColor( Data.Specular ), Command.Has( FDrawFlag::Fill ) ? TRUE : FALSE );
				}
			}
			return true;
		}
		case EDrawCommand::BillboardPivot3D:
		{
			if ( Command.CanRead<FDrawBillboardPivot3DData>() )
			{
				const FDrawBillboardPivot3DData Data = Command.Read<FDrawBillboardPivot3DData>();
				if ( Data.Handle >= 0 && AreFinite( Data.Pos ) && NMath::IsFinite( Data.Size ) && NMath::IsFinite( Data.PivotX ) && NMath::IsFinite( Data.PivotY ) && NMath::IsFinite( Data.Angle ) )
				{
					// DxLib の基準点の縦は、画像の下から数える。フレームワークは上から数える
					DrawBillboard3D( ToVector( Data.Pos ), Data.PivotX, 1.0f - Data.PivotY, Data.Size, Data.Angle, Data.Handle, TRUE, FlipX, FlipY );
				}
			}
			return true;
		}
		case EDrawCommand::BillboardQuad3D:
		{
			if ( Command.CanRead<FDrawBillboardQuad3DData>() )
			{
				const FDrawBillboardQuad3DData Data = Command.Read<FDrawBillboardQuad3DData>();
				if ( Data.Handle >= 0 && AreFinite( Data.Pos ) && AreFinite( Data.Corner ) )
				{
					DrawModiBillboard3D( ToVector( Data.Pos ), Data.Corner[0], Data.Corner[1], Data.Corner[2], Data.Corner[3], Data.Corner[4], Data.Corner[5], Data.Corner[6], Data.Corner[7], Data.Handle, TRUE );
				}
			}
			return true;
		}
		case EDrawCommand::Sprite3D:
		{
			if ( Command.CanRead<FDrawSprite3DData>() )
			{
				const FDrawSprite3DData Data = Command.Read<FDrawSprite3DData>();
				if ( Data.Handle >= 0 && AreFinite( Data.Pos ) && NMath::IsFinite( Data.Scale ) && NMath::IsFinite( Data.Angle ) )
				{
					DrawRotaGraph3D( Data.Pos[0], Data.Pos[1], Data.Pos[2], Data.Scale, Data.Angle, Data.Handle, TRUE, FlipX, FlipY );
				}
			}
			return true;
		}
		case EDrawCommand::SpriteStretch3D:
		{
			if ( Command.CanRead<FDrawSpriteStretch3DData>() )
			{
				const FDrawSpriteStretch3DData Data = Command.Read<FDrawSpriteStretch3DData>();
				if ( Data.Handle >= 0 && AreFinite( Data.Pos ) && NMath::IsFinite( Data.ScaleX ) && NMath::IsFinite( Data.ScaleY ) )
				{
					DrawExtendGraph3D( Data.Pos[0], Data.Pos[1], Data.Pos[2], Data.ScaleX, Data.ScaleY, Data.Handle, TRUE );
				}
			}
			return true;
		}
		case EDrawCommand::SpritePivot3D:
		{
			if ( Command.CanRead<FDrawSpritePivot3DData>() )
			{
				const FDrawSpritePivot3DData Data = Command.Read<FDrawSpritePivot3DData>();
				int Width = 0;
				int Height = 0;
				if ( Data.Handle >= 0 && AreFinite( Data.Pos ) && NMath::IsFinite( Data.PivotX ) && NMath::IsFinite( Data.PivotY ) && NMath::IsFinite( Data.ScaleX ) && NMath::IsFinite( Data.ScaleY ) && NMath::IsFinite( Data.Angle )
					&& GetGraphSize( Data.Handle, &Width, &Height ) == 0 )
				{
					// DxLib の基準点の縦は、画像の下から数える。フレームワークは上から数える
					DrawRota2Graph3D( Data.Pos[0], Data.Pos[1], Data.Pos[2], Data.PivotX, static_cast<float>( Height ) - Data.PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, Data.Handle, TRUE, FlipX, FlipY );
				}
			}
			return true;
		}
		case EDrawCommand::Primitive3D:
		{
			DrawPrimitive( Command );
			return true;
		}
		case EDrawCommand::PrimitiveBuffer3D:
		{
			DrawPrimitiveBuffer( Command );
			return true;
		}
		default:
			return false;
		}
	}
}
