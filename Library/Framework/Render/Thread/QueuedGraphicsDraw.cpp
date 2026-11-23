// SPDX-License-Identifier: MIT
#include "QueuedGraphicsDraw.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/TypeTraits.h"

// 複写を入れる入れ物の、数と置き場所（すべて、領域の先頭からのバイト数）。
struct FQueuedDrawHeader
{
	bool bTransient = false; // 一時データの描画か。
	bool bIndexed = false; // 一時データに添字があるか。
	FGraphicsPipelineHandle Pipeline; // 描画するパイプライン。
	FBufferResourceHandle VertexBuffer; // 共有の頂点バッファ。
	FBufferResourceHandle IndexBuffer; // 共有の添字バッファ（添字なしなら無効）。
	uint64 VertexOffset = 0; // 頂点バッファ内の位置。
	uint64 IndexOffset = 0; // 添字バッファ内の位置。
	uint32 VertexCount = 0; // 頂点の数。
	uint32 IndexCount = 0; // 添字の数。
	EGeometryIndexFormat IndexFormat = EGeometryIndexFormat::UInt16; // 共有の添字の幅。
	uint32 NumBuffers = 0; // 共有のバッファスロットの数。
	uint32 NumTextures = 0; // 画像スロットの数。
	uint32 NumConstants = 0; // 定数の数。
	uint32 BuffersOffset = 0; // 共有のバッファスロット（FBufferResourceHandle）の位置。
	uint32 TexturesOffset = 0; // 画像スロット（FTextureResourceHandle）の位置。
	uint32 ConstantSizesOffset = 0; // 定数ごとの大きさ（uint32）の位置。
	uint32 ConstantBytesOffset = 0; // 定数の中身を、順に並べたものの位置。
	uint32 VerticesOffset = 0; // 一時データの頂点の位置。
	uint32 IndicesOffset = 0; // 一時データの添字（uint32）の位置。
};

// 複写を入れる入れ物。上の数と置き場所と、それに従って全部の配列を並べた 1 つの領域。使い終わったら、置き場へ返して使い回す。
struct FQueuedDrawStorage
{
	FQueuedDrawHeader Header; // 数と置き場所。
	TArray<uint8> Bytes; // 全部の配列を並べた領域。
	FQueuedDrawStorage *NextFree = nullptr; // 置き場の、次の空いた入れ物。
};

namespace
{
	static_assert( bIsTriviallyCopyable<FBufferResourceHandle> && bIsTriviallyCopyable<FTextureResourceHandle>, "handles are copied as bytes" );

	constexpr uint64 SectionAlignment = 16; // 配列ごとの置き場所の、そろえる幅。
	constexpr uint64 MaxStorageBytes = 0x40000000; // 1 つの描画の複写の、上限（1 GB）。これを超えるものは、複写せずに断る。
	constexpr int32 RoundCapacity = 4096; // 領域の大きさを、この幅の倍数へ切り上げて確保する（少しずつ違う大きさで、何度も確保し直さない）。
	constexpr int32 MaxPooledBytes = 64 * 1024; // 置き場へ返すときに、領域をこれより大きく持っていたら、手放す（置き場に、大きな領域を溜めない）。
	constexpr int32 MaxPooledStorages = 1024; // 置き場に取っておく入れ物の数の上限。
	constexpr int32 MaxInlineConstants = 8; // 実行のとき、定数の見え方を、スタックへ置く数（これを超えたら、確保する）。

	FORCEINLINE uint64 AlignUp( uint64 Value )
	{
		return ( Value + SectionAlignment - 1 ) & ~( SectionAlignment - 1 );
	}

	TAtomic<bool> bPoolAlive{ false }; // 置き場が、まだ使えるか（プロセスの終わりに、置き場が先に消えても、あとから返された入れ物は、そのまま解放する）。

	// 使い終わった入れ物の置き場。確保する側（ゲームスレッド）と、返す側（描画スレッド）が、別のスレッド。
	class FQueuedDrawStoragePool
	{
	  public:
		FQueuedDrawStoragePool();
		~FQueuedDrawStoragePool();
		// 空の入れ物を 1 つ渡す（置き場に無ければ、新しく作る）。
		FQueuedDrawStorage *Acquire();
		// 入れ物を返す（置き場がいっぱいなら、解放する）。
		void Release( FQueuedDrawStorage *Storage );

	  private:
		FMutex Mutex; // 下の 2 つを守る。
		FQueuedDrawStorage *Head = nullptr; // 空いた入れ物の列の先頭。
		int32 Count = 0; // 空いた入れ物の数。
	};

	FQueuedDrawStoragePool::FQueuedDrawStoragePool()
	{
		bPoolAlive.Store( true, EMemoryOrder::Release );
	}

	FQueuedDrawStoragePool::~FQueuedDrawStoragePool()
	{
		FQueuedDrawStorage *Chain = nullptr;
		{
			FScopedLock Lock( Mutex );
			bPoolAlive.Store( false, EMemoryOrder::Release );
			Chain = Head;
			Head = nullptr;
			Count = 0;
		}
		while ( Chain )
		{
			FQueuedDrawStorage *Next = Chain->NextFree;
			delete Chain;
			Chain = Next;
		}
	}

	FQueuedDrawStorage *FQueuedDrawStoragePool::Acquire()
	{
		{
			FScopedLock Lock( Mutex );
			if ( Head )
			{
				FQueuedDrawStorage *Storage = Head;
				Head = Storage->NextFree;
				Storage->NextFree = nullptr;
				--Count;
				return Storage;
			}
		}
		return new FQueuedDrawStorage();
	}

	void FQueuedDrawStoragePool::Release( FQueuedDrawStorage *Storage )
	{
		if ( !Storage )
		{
			return;
		}
		Storage->Header = FQueuedDrawHeader();
		if ( Storage->Bytes.Max() > MaxPooledBytes )
		{
			Storage->Bytes.Empty();
		}
		else
		{
			Storage->Bytes.Reset();
		}
		{
			FScopedLock Lock( Mutex );
			if ( bPoolAlive.Load( EMemoryOrder::Acquire ) && Count < MaxPooledStorages )
			{
				Storage->NextFree = Head;
				Head = Storage;
				++Count;
				return;
			}
		}
		delete Storage;
	}

	FQueuedDrawStoragePool &GetPool()
	{
		static FQueuedDrawStoragePool Pool;
		return Pool;
	}

	// 入れ物を 1 つ用意して、全部の配列を並べる位置を決め、その大きさの領域を確保する。大きすぎるときは、nullptr。
	FQueuedDrawStorage *Prepare( uint32 NumBuffers, uint32 NumTextures, TArrayView<const FGraphicsConstantBytes> Constants, uint64 VertexBytes, uint32 NumIndices )
	{
		uint64 ConstantBytes = 0; // 定数の中身の合計。
		for ( const FGraphicsConstantBytes &Constant : Constants )
		{
			ConstantBytes += Constant.Size;
		}
		FQueuedDrawHeader Header;
		Header.NumBuffers = NumBuffers;
		Header.NumTextures = NumTextures;
		Header.NumConstants = static_cast<uint32>( Constants.Num() );
		uint64 Offset = 0; // 次の配列を置く位置。
		const auto Place = [&Offset]( uint64 Bytes )
		{
			const uint64 Start = Offset;
			Offset = AlignUp( Offset + Bytes );
			return Start;
		};
		if ( VertexBytes > MaxStorageBytes || ConstantBytes > MaxStorageBytes )
		{
			return nullptr; // 足し合わせる前に断る（足し算が、桁あふれしない大きさに収める）。
		}
		Header.BuffersOffset = static_cast<uint32>( Place( static_cast<uint64>( NumBuffers ) * sizeof( FBufferResourceHandle ) ) );
		Header.TexturesOffset = static_cast<uint32>( Place( static_cast<uint64>( NumTextures ) * sizeof( FTextureResourceHandle ) ) );
		Header.ConstantSizesOffset = static_cast<uint32>( Place( static_cast<uint64>( Header.NumConstants ) * sizeof( uint32 ) ) );
		Header.ConstantBytesOffset = static_cast<uint32>( Place( ConstantBytes ) );
		Header.VerticesOffset = static_cast<uint32>( Place( VertexBytes ) );
		Header.IndicesOffset = static_cast<uint32>( Place( static_cast<uint64>( NumIndices ) * sizeof( uint32 ) ) );
		if ( Offset > MaxStorageBytes )
		{
			return nullptr;
		}
		const uint64 Total = Offset > 0 ? Offset : SectionAlignment; // 空でも、先頭のアドレスは持つ。
		FQueuedDrawStorage *Storage = GetPool().Acquire();
		Storage->Header = Header;
		if ( static_cast<uint64>( Storage->Bytes.Max() ) < Total )
		{
			const uint64 Capacity = ( Total + RoundCapacity - 1 ) / RoundCapacity * RoundCapacity;
			Storage->Bytes.Empty(); // 古い中身は、使わないので、写さずに、解放してから確保する。
			Storage->Bytes.Reserve( static_cast<int32>( Capacity ) );
		}
		Storage->Bytes.AddUninitialized( static_cast<int32>( Total ) );
		return Storage;
	}

	// 画像スロットと定数を、入れ物へ複写する（共通の部分）。
	void CopyTexturesAndConstants( FQueuedDrawStorage &Storage, TArrayView<const FTextureResourceHandle> Textures, TArrayView<const FGraphicsConstantBytes> Constants )
	{
		const FQueuedDrawHeader &Header = Storage.Header;
		uint8 *Base = Storage.Bytes.GetData();
		if ( Textures.Num() > 0 )
		{
			FMemory::Memcpy( Base + Header.TexturesOffset, Textures.GetData(), static_cast<usize>( Textures.Num() ) * sizeof( FTextureResourceHandle ) );
		}
		uint32 *Sizes = reinterpret_cast<uint32 *>( Base + Header.ConstantSizesOffset );
		uint8 *Cursor = Base + Header.ConstantBytesOffset;
		for ( int32 Index = 0; Index < Constants.Num(); ++Index )
		{
			const FGraphicsConstantBytes &Constant = Constants[Index];
			Sizes[Index] = Constant.Size;
			if ( Constant.Size > 0 )
			{
				FMemory::Memcpy( Cursor, Constant.Data, Constant.Size );
				Cursor += Constant.Size;
			}
		}
	}
}

FQueuedGraphicsDraw::FQueuedGraphicsDraw( FQueuedDrawStorage *InStorage ) noexcept
	: Storage( InStorage )
{
}

FQueuedGraphicsDraw::FQueuedGraphicsDraw( const FQueuedGraphicsDraw &Other )
{
	if ( Other.Storage )
	{
		Storage = GetPool().Acquire();
		Storage->Header = Other.Storage->Header;
		Storage->Bytes.Reset();
		Storage->Bytes.Append( Other.Storage->Bytes.GetData(), Other.Storage->Bytes.Num() );
	}
}

FQueuedGraphicsDraw::FQueuedGraphicsDraw( FQueuedGraphicsDraw &&Other ) noexcept
	: Storage( Other.Storage )
{
	Other.Storage = nullptr;
}

FQueuedGraphicsDraw &FQueuedGraphicsDraw::operator=( const FQueuedGraphicsDraw &Other )
{
	if ( this != &Other )
	{
		FQueuedGraphicsDraw Copied( Other );
		*this = static_cast<FQueuedGraphicsDraw &&>( Copied );
	}
	return *this;
}

FQueuedGraphicsDraw &FQueuedGraphicsDraw::operator=( FQueuedGraphicsDraw &&Other ) noexcept
{
	if ( this != &Other )
	{
		Release();
		Storage = Other.Storage;
		Other.Storage = nullptr;
	}
	return *this;
}

FQueuedGraphicsDraw::~FQueuedGraphicsDraw()
{
	Release();
}

void FQueuedGraphicsDraw::Release()
{
	if ( Storage )
	{
		GetPool().Release( Storage );
		Storage = nullptr;
	}
}

FQueuedGraphicsDraw FQueuedGraphicsDraw::Copy( const FGraphicsPipelineDraw &Draw )
{
	FQueuedDrawStorage *Storage = Prepare( static_cast<uint32>( Draw.Buffers.Num() ), static_cast<uint32>( Draw.Textures.Num() ), Draw.Constants, 0, 0 );
	if ( !Storage )
	{
		return FQueuedGraphicsDraw();
	}
	FQueuedDrawHeader &Header = Storage->Header;
	Header.Pipeline = Draw.Pipeline;
	Header.VertexBuffer = Draw.VertexBuffer;
	Header.IndexBuffer = Draw.IndexBuffer;
	Header.VertexOffset = Draw.VertexOffset;
	Header.IndexOffset = Draw.IndexOffset;
	Header.VertexCount = Draw.VertexCount;
	Header.IndexCount = Draw.IndexCount;
	Header.IndexFormat = Draw.IndexFormat;
	if ( Draw.Buffers.Num() > 0 )
	{
		FMemory::Memcpy( Storage->Bytes.GetData() + Header.BuffersOffset, Draw.Buffers.GetData(), static_cast<usize>( Draw.Buffers.Num() ) * sizeof( FBufferResourceHandle ) );
	}
	CopyTexturesAndConstants( *Storage, Draw.Textures, Draw.Constants );
	return FQueuedGraphicsDraw( Storage );
}

FQueuedGraphicsDraw FQueuedGraphicsDraw::Copy( const FGraphicsPipelineTransientDraw &Draw )
{
	if ( !Draw.Vertices || Draw.VertexStride == 0 )
	{
		return FQueuedGraphicsDraw(); // 頂点の大きさが分からないものは、複写できない（実行は、拒否される）。
	}
	const bool bIndexed = Draw.Indices != nullptr;
	FQueuedDrawStorage *Storage = Prepare( 0, static_cast<uint32>( Draw.Textures.Num() ), Draw.Constants, static_cast<uint64>( Draw.VertexCount ) * Draw.VertexStride, bIndexed ? Draw.IndexCount : 0u );
	if ( !Storage )
	{
		return FQueuedGraphicsDraw();
	}
	FQueuedDrawHeader &Header = Storage->Header;
	Header.bTransient = true;
	Header.bIndexed = bIndexed;
	Header.Pipeline = Draw.Pipeline;
	Header.VertexCount = Draw.VertexCount;
	Header.IndexCount = Draw.IndexCount;
	uint8 *Base = Storage->Bytes.GetData();
	FMemory::Memcpy( Base + Header.VerticesOffset, Draw.Vertices, static_cast<usize>( Draw.VertexCount ) * Draw.VertexStride );
	if ( bIndexed && Draw.IndexCount > 0 )
	{
		FMemory::Memcpy( Base + Header.IndicesOffset, Draw.Indices, static_cast<usize>( Draw.IndexCount ) * sizeof( uint32 ) );
	}
	CopyTexturesAndConstants( *Storage, Draw.Textures, Draw.Constants );
	return FQueuedGraphicsDraw( Storage );
}

bool FQueuedGraphicsDraw::IsTransient() const
{
	return Storage && Storage->Header.bTransient;
}

FGraphicsPipelineHandle FQueuedGraphicsDraw::GetPipeline() const
{
	return Storage ? Storage->Header.Pipeline : FGraphicsPipelineHandle();
}

bool FQueuedGraphicsDraw::Execute( IRenderGeometry3D &Geometry ) const
{
	if ( !Storage )
	{
		return false;
	}
	const FQueuedDrawHeader &Header = Storage->Header;
	const uint8 *Base = Storage->Bytes.GetData();
	// 定数の見え方（入れ物の中の位置）を作る。
	FGraphicsConstantBytes InlineViews[MaxInlineConstants];
	TArray<FGraphicsConstantBytes> ManyViews;
	FGraphicsConstantBytes *Views = InlineViews;
	if ( Header.NumConstants > static_cast<uint32>( MaxInlineConstants ) )
	{
		ManyViews.SetNum( static_cast<int32>( Header.NumConstants ) );
		Views = ManyViews.GetData();
	}
	const uint32 *Sizes = reinterpret_cast<const uint32 *>( Base + Header.ConstantSizesOffset );
	const uint8 *Cursor = Base + Header.ConstantBytesOffset;
	for ( uint32 Index = 0; Index < Header.NumConstants; ++Index )
	{
		Views[Index].Data = Cursor;
		Views[Index].Size = Sizes[Index];
		Cursor += Sizes[Index];
	}
	const TArrayView<const FGraphicsConstantBytes> ConstantView( Views, static_cast<int32>( Header.NumConstants ) );
	const TArrayView<const FTextureResourceHandle> TextureView( reinterpret_cast<const FTextureResourceHandle *>( Base + Header.TexturesOffset ), static_cast<int32>( Header.NumTextures ) );
	if ( Header.bTransient )
	{
		FGraphicsPipelineTransientDraw Draw;
		Draw.Pipeline = Header.Pipeline;
		Draw.Vertices = Base + Header.VerticesOffset;
		Draw.VertexCount = Header.VertexCount;
		Draw.Indices = Header.bIndexed ? reinterpret_cast<const uint32 *>( Base + Header.IndicesOffset ) : nullptr;
		Draw.IndexCount = Header.IndexCount;
		Draw.Constants = ConstantView;
		Draw.Textures = TextureView;
		return Geometry.DrawGraphicsPipelineTransient( Draw );
	}
	FGraphicsPipelineDraw Draw;
	Draw.Pipeline = Header.Pipeline;
	Draw.VertexBuffer = Header.VertexBuffer;
	Draw.VertexOffset = Header.VertexOffset;
	Draw.VertexCount = Header.VertexCount;
	Draw.IndexBuffer = Header.IndexBuffer;
	Draw.IndexOffset = Header.IndexOffset;
	Draw.IndexCount = Header.IndexCount;
	Draw.IndexFormat = Header.IndexFormat;
	Draw.Buffers = TArrayView<const FBufferResourceHandle>( reinterpret_cast<const FBufferResourceHandle *>( Base + Header.BuffersOffset ), static_cast<int32>( Header.NumBuffers ) );
	Draw.Textures = TextureView;
	Draw.Constants = ConstantView;
	return Geometry.DrawGraphicsPipeline( Draw );
}
