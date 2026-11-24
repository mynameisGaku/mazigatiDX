// SPDX-License-Identifier: MIT
#include "AsyncDrawGate.h"

namespace
{
	constexpr int32 MaxVerified = 16384; // 確かめ済みの数の上限。超えたら全部捨てて、確かめ直す。
	constexpr uint32 MaxConstantBytes = 65536; // 実行先が受け付ける定数 1 つの大きさ。
	constexpr uint32 MaxVertexStride = 4096; // 頂点 1 つの大きさの上限（複写する量を抑える）。

	// 値を混ぜて、組み合わせを表す 64 ビットを作る。
	uint64 Mix( uint64 Hash, uint64 Value )
	{
		Hash ^= Value + 0x9E3779B97F4A7C15ull + ( Hash << 6 ) + ( Hash >> 2 );
		return Hash * 0xFF51AFD7ED558CCDull;
	}

	// 定数 1 つを、転送領域へ載せる量（256 の境界へ揃え、揃えの余裕を足す）。
	uint64 ConstantUploadBytes( uint32 Size )
	{
		return ( static_cast<uint64>( Size ) + 255ull ) / 256ull * 256ull + 256ull;
	}
}

void CAsyncDrawGate::BeginFrame()
{
	PinnedBuffers.Reset();
	PinnedTextures.Reset();
	PinnedPipelines.Reset();
	PinnedBytes = 0;
	UploadBytes = 0;
	NumDraws = 0;
	SetTarget( 0, TArrayView<const FTextureResourceHandle>() );
}

void CAsyncDrawGate::SetTarget( uint64 InTargetKey, TArrayView<const FTextureResourceHandle> InWrittenTextures )
{
	TargetKey = InTargetKey;
	WrittenTextures.Reset();
	for ( const FTextureResourceHandle &Texture : InWrittenTextures )
	{
		if ( Texture.IsValid() )
		{
			WrittenTextures.Add( Texture.Value );
		}
	}
}

void CAsyncDrawGate::InvalidateVerified()
{
	Verified.Reset();
}

bool CAsyncDrawGate::IsWritten( uint64 Texture ) const
{
	return WrittenTextures.Contains( Texture );
}

CAsyncDrawGate::FUse CAsyncDrawGate::Inspect( const FGraphicsPipelineDraw &Draw ) const
{
	FUse Use;
	const bool bIndexed = Draw.IndexBuffer.IsValid();
	Use.bValid = Draw.Pipeline.IsValid() && Draw.VertexBuffer.IsValid() && Draw.PinnedBytes > 0 && Draw.VertexCount > 0 && Draw.VertexCount <= NGraphicsPipelineLimits::MaxRegisteredVertices
		&& ( bIndexed ? Draw.IndexCount > 0 && Draw.IndexCount <= NGraphicsPipelineLimits::MaxRegisteredIndices : Draw.IndexCount == 0 )
		&& ( Draw.Buffers.Num() == 0 || Draw.Buffers.GetData() ) && ( Draw.Textures.Num() == 0 || Draw.Textures.GetData() ) && ( Draw.Constants.Num() == 0 || Draw.Constants.GetData() );
	if ( !Use.bValid )
	{
		return Use;
	}
	uint64 Key = Mix( Mix( 1, TargetKey ), Draw.Pipeline.Value );
	Key = Mix( Mix( Mix( Key, Draw.VertexBuffer.Value ), Draw.VertexOffset ), Draw.VertexCount );
	Key = Mix( Mix( Mix( Mix( Key, Draw.IndexBuffer.Value ), Draw.IndexOffset ), Draw.IndexCount ), static_cast<uint64>( Draw.IndexFormat ) );
	Use.PipelineValue = Draw.Pipeline.Value;
	Use.Buffers.Add( Draw.VertexBuffer.Value );
	if ( bIndexed )
	{
		Use.Buffers.Add( Draw.IndexBuffer.Value );
	}
	Key = Mix( Key, static_cast<uint64>( Draw.Buffers.Num() ) );
	for ( const FBufferResourceHandle &Buffer : Draw.Buffers )
	{
		Key = Mix( Key, Buffer.Value );
		Use.Buffers.Add( Buffer.Value );
	}
	Key = Mix( Key, static_cast<uint64>( Draw.Textures.Num() ) );
	for ( const FTextureResourceHandle &Texture : Draw.Textures )
	{
		Key = Mix( Key, Texture.Value );
		Use.Textures.Add( Texture.Value );
		Use.bTouchesWrittenTexture = Use.bTouchesWrittenTexture || IsWritten( Texture.Value );
	}
	Key = Mix( Key, static_cast<uint64>( Draw.Constants.Num() ) );
	for ( const FGraphicsConstantBytes &Constant : Draw.Constants )
	{
		Use.bValid = Use.bValid && Constant.Data && Constant.Size > 0 && Constant.Size <= MaxConstantBytes;
		Key = Mix( Key, Constant.Size );
		Use.UploadBytes += ConstantUploadBytes( Constant.Size );
	}
	Use.Key = Key;
	Use.PinnedBytes = Draw.PinnedBytes;
	return Use;
}

CAsyncDrawGate::FUse CAsyncDrawGate::InspectTransient( const FGraphicsPipelineTransientDraw &Draw ) const
{
	FUse Use;
	const bool bIndexed = Draw.Indices != nullptr;
	Use.bValid = Draw.Pipeline.IsValid() && Draw.Vertices && Draw.VertexStride > 0 && Draw.VertexStride <= MaxVertexStride && Draw.VertexCount > 0 && Draw.VertexCount <= NGraphicsPipelineLimits::MaxTransientVertices
		&& ( bIndexed ? Draw.IndexCount > 0 && Draw.IndexCount <= NGraphicsPipelineLimits::MaxTransientIndices : Draw.IndexCount == 0 )
		&& ( Draw.Textures.Num() == 0 || Draw.Textures.GetData() ) && ( Draw.Constants.Num() == 0 || Draw.Constants.GetData() );
	if ( !Use.bValid )
	{
		return Use;
	}
	// 頂点数・添字数は、6 で割った余りだけを含める（点・線・三角形のどの組でも、余りが同じなら、割り切れるかは同じ）。
	uint64 Key = Mix( Mix( 2, TargetKey ), Draw.Pipeline.Value );
	Key = Mix( Mix( Mix( Key, Draw.VertexStride ), Draw.VertexCount % 6 ), bIndexed ? 1 + Draw.IndexCount % 6 : 0 );
	Use.PipelineValue = Draw.Pipeline.Value;
	Key = Mix( Key, static_cast<uint64>( Draw.Textures.Num() ) );
	for ( const FTextureResourceHandle &Texture : Draw.Textures )
	{
		Key = Mix( Key, Texture.Value );
		Use.Textures.Add( Texture.Value );
		Use.bTouchesWrittenTexture = Use.bTouchesWrittenTexture || IsWritten( Texture.Value );
	}
	Key = Mix( Key, static_cast<uint64>( Draw.Constants.Num() ) );
	Use.UploadBytes = static_cast<uint64>( Draw.VertexCount ) * Draw.VertexStride + ( bIndexed ? static_cast<uint64>( Draw.IndexCount ) * 4ull : 0ull );
	for ( const FGraphicsConstantBytes &Constant : Draw.Constants )
	{
		Use.bValid = Use.bValid && Constant.Data && Constant.Size > 0 && Constant.Size <= MaxConstantBytes;
		Key = Mix( Key, Constant.Size );
		Use.UploadBytes += ConstantUploadBytes( Constant.Size );
	}
	Use.Key = Key;
	return Use;
}

bool CAsyncDrawGate::FitsBudget( const FUse &Use ) const
{
	if ( NumDraws >= MaxDraws || UploadBytes + Use.UploadBytes > MaxUploadBytes )
	{
		return false;
	}
	int32 NewBuffers = 0;
	for ( const uint64 Buffer : Use.Buffers )
	{
		NewBuffers += PinnedBuffers.Contains( Buffer ) ? 0 : 1;
	}
	if ( NewBuffers > 0 && ( PinnedBuffers.Num() + NewBuffers > MaxPinnedBuffers || Use.PinnedBytes > MaxPinnedBytes || PinnedBytes > MaxPinnedBytes - Use.PinnedBytes ) )
	{
		return false;
	}
	int32 NewTextures = 0;
	for ( const uint64 Texture : Use.Textures )
	{
		NewTextures += PinnedTextures.Contains( Texture ) ? 0 : 1;
	}
	return PinnedTextures.Num() + NewTextures <= MaxPinnedTextures && PinnedPipelines.Num() + ( PinnedPipelines.Contains( Use.PipelineValue ) ? 0 : 1 ) <= MaxPinnedPipelines;
}

void CAsyncDrawGate::Count( const FUse &Use )
{
	++NumDraws;
	UploadBytes += Use.UploadBytes;
	bool bNewBuffer = false;
	for ( const uint64 Buffer : Use.Buffers )
	{
		bNewBuffer = PinnedBuffers.Add( Buffer ) || bNewBuffer;
	}
	if ( bNewBuffer )
	{
		PinnedBytes += Use.PinnedBytes; // 組の一部だけが新しくても、全体を足す（多めに数える側へ倒す）。
	}
	for ( const uint64 Texture : Use.Textures )
	{
		PinnedTextures.Add( Texture );
	}
	PinnedPipelines.Add( Use.PipelineValue );
}

void CAsyncDrawGate::Remember( const FUse &Use )
{
	if ( Verified.Num() >= MaxVerified )
	{
		Verified.Reset();
	}
	Verified.Add( Use.Key );
}

CAsyncDrawGate::EAdmission CAsyncDrawGate::Admit( const FGraphicsPipelineDraw &Draw )
{
	const FUse Use = Inspect( Draw );
	if ( !Use.bValid || Use.bTouchesWrittenTexture || !Verified.Contains( Use.Key ) || !FitsBudget( Use ) )
	{
		return EAdmission::Verify;
	}
	Count( Use );
	return EAdmission::Queue;
}

CAsyncDrawGate::EAdmission CAsyncDrawGate::AdmitTransient( const FGraphicsPipelineTransientDraw &Draw )
{
	const FUse Use = InspectTransient( Draw );
	if ( !Use.bValid || Use.bTouchesWrittenTexture || !Verified.Contains( Use.Key ) || !FitsBudget( Use ) )
	{
		return EAdmission::Verify;
	}
	Count( Use );
	return EAdmission::Queue;
}

void CAsyncDrawGate::NoteExecuted( const FGraphicsPipelineDraw &Draw, bool bAccepted )
{
	const FUse Use = Inspect( Draw );
	if ( !bAccepted || !Use.bValid )
	{
		return;
	}
	Count( Use );
	if ( !Use.bTouchesWrittenTexture )
	{
		Remember( Use );
	}
}

void CAsyncDrawGate::NoteExecutedTransient( const FGraphicsPipelineTransientDraw &Draw, bool bAccepted )
{
	const FUse Use = InspectTransient( Draw );
	if ( !bAccepted || !Use.bValid )
	{
		return;
	}
	Count( Use );
	if ( !Use.bTouchesWrittenTexture )
	{
		Remember( Use );
	}
}
