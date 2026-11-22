// SPDX-License-Identifier: MIT
#include "QueuedGraphicsDraw.h"
#include "Library/Core/Memory/Memory.h"

namespace
{
	// 定数の中身を、大きさの列と一緒に複写する。
	void CopyConstants( TArrayView<const FGraphicsConstantBytes> Constants, TArray<uint32> &OutSizes, TArray<uint8> &OutBytes )
	{
		for ( const FGraphicsConstantBytes &Constant : Constants )
		{
			OutSizes.Add( Constant.Size );
			if ( Constant.Size > 0 )
			{
				OutBytes.Append( Constant.Data, static_cast<int32>( Constant.Size ) );
			}
		}
	}
}

FQueuedGraphicsDraw FQueuedGraphicsDraw::Copy( const FGraphicsPipelineDraw &Draw )
{
	FQueuedGraphicsDraw Result;
	Result.Pipeline = Draw.Pipeline;
	Result.VertexBuffer = Draw.VertexBuffer;
	Result.IndexBuffer = Draw.IndexBuffer;
	Result.VertexOffset = Draw.VertexOffset;
	Result.IndexOffset = Draw.IndexOffset;
	Result.VertexCount = Draw.VertexCount;
	Result.IndexCount = Draw.IndexCount;
	Result.IndexFormat = Draw.IndexFormat;
	if ( Draw.Buffers.Num() > 0 )
	{
		Result.Buffers.Append( Draw.Buffers.GetData(), Draw.Buffers.Num() );
	}
	if ( Draw.Textures.Num() > 0 )
	{
		Result.Textures.Append( Draw.Textures.GetData(), Draw.Textures.Num() );
	}
	CopyConstants( Draw.Constants, Result.ConstantSizes, Result.ConstantBytes );
	return Result;
}

FQueuedGraphicsDraw FQueuedGraphicsDraw::Copy( const FGraphicsPipelineTransientDraw &Draw )
{
	FQueuedGraphicsDraw Result;
	Result.bTransient = true;
	Result.Pipeline = Draw.Pipeline;
	Result.VertexCount = Draw.VertexCount;
	Result.IndexCount = Draw.IndexCount;
	Result.bIndexed = Draw.Indices != nullptr;
	Result.Vertices.Append( static_cast<const uint8 *>( Draw.Vertices ), static_cast<int32>( static_cast<uint64>( Draw.VertexCount ) * Draw.VertexStride ) );
	if ( Result.bIndexed )
	{
		Result.Indices.Append( Draw.Indices, static_cast<int32>( Draw.IndexCount ) );
	}
	if ( Draw.Textures.Num() > 0 )
	{
		Result.Textures.Append( Draw.Textures.GetData(), Draw.Textures.Num() );
	}
	CopyConstants( Draw.Constants, Result.ConstantSizes, Result.ConstantBytes );
	return Result;
}

void FQueuedGraphicsDraw::MakeConstantViews( TArray<FGraphicsConstantBytes> &OutViews ) const
{
	const uint8 *Cursor = ConstantBytes.GetData();
	for ( const uint32 Size : ConstantSizes )
	{
		FGraphicsConstantBytes View;
		View.Data = Cursor;
		View.Size = Size;
		OutViews.Add( View );
		Cursor += Size;
	}
}

bool FQueuedGraphicsDraw::Execute( IRenderGeometry3D &Geometry ) const
{
	TArray<FGraphicsConstantBytes> Constants;
	MakeConstantViews( Constants );
	const TArrayView<const FGraphicsConstantBytes> ConstantView( Constants.GetData(), Constants.Num() );
	const TArrayView<const FTextureResourceHandle> TextureView( Textures.GetData(), Textures.Num() );
	if ( bTransient )
	{
		FGraphicsPipelineTransientDraw Draw;
		Draw.Pipeline = Pipeline;
		Draw.Vertices = Vertices.GetData();
		Draw.VertexCount = VertexCount;
		Draw.Indices = bIndexed ? Indices.GetData() : nullptr;
		Draw.IndexCount = IndexCount;
		Draw.Constants = ConstantView;
		Draw.Textures = TextureView;
		return Geometry.DrawGraphicsPipelineTransient( Draw );
	}
	FGraphicsPipelineDraw Draw;
	Draw.Pipeline = Pipeline;
	Draw.VertexBuffer = VertexBuffer;
	Draw.VertexOffset = VertexOffset;
	Draw.VertexCount = VertexCount;
	Draw.IndexBuffer = IndexBuffer;
	Draw.IndexOffset = IndexOffset;
	Draw.IndexCount = IndexCount;
	Draw.IndexFormat = IndexFormat;
	Draw.Buffers = TArrayView<const FBufferResourceHandle>( Buffers.GetData(), Buffers.Num() );
	Draw.Textures = TextureView;
	Draw.Constants = ConstantView;
	return Geometry.DrawGraphicsPipeline( Draw );
}
