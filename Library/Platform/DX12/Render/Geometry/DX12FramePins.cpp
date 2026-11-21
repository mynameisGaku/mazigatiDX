// SPDX-License-Identifier: MIT
#include "DX12FramePins.h"

void CDX12FramePins::Reset()
{
	Buffers.Reset();
	Textures.Reset();
	Pipelines.Reset();
	BufferBytes = 0;
}

bool CDX12FramePins::CanCommitBuffers( TArrayView<const FDX12BufferBinding> Candidates, int32 &OutNewCount, uint64 &OutAddedBytes ) const
{
	OutNewCount = 0;
	OutAddedBytes = 0;
	for ( int32 i = 0; i < Candidates.Num(); ++i )
	{
		ID3D12Resource *Resource = Candidates[i].Resource.Get(); // 重複した資源は参照数と保持量を増やさない。
		bool bSeen = false;
		for ( const FDX12BufferBinding &Existing : Buffers )
		{
			if ( Existing.Resource.Get() == Resource )
			{
				bSeen = true;
				break;
			}
		}
		for ( int32 Earlier = 0; !bSeen && Earlier < i; ++Earlier )
		{
			if ( Candidates[Earlier].Resource.Get() == Resource )
			{
				bSeen = true;
			}
		}
		if ( !bSeen )
		{
			++OutNewCount;
			if ( Candidates[i].Size > MaxBufferBytes - BufferBytes - OutAddedBytes )
			{
				return false;
			}
			OutAddedBytes += Candidates[i].Size;
		}
	}
	return Buffers.Num() + OutNewCount <= MaxBuffers;
}

bool CDX12FramePins::CommitBuffers( TArrayView<const FDX12BufferBinding> Candidates )
{
	int32 NewCount = 0;
	uint64 AddedBytes = 0;
	if ( !CanCommitBuffers( Candidates, NewCount, AddedBytes ) )
	{
		return false;
	}
	for ( const FDX12BufferBinding &Candidate : Candidates )
	{
		bool bPinned = false;
		for ( const FDX12BufferBinding &Existing : Buffers )
		{
			if ( Existing.Resource.Get() == Candidate.Resource.Get() )
			{
				bPinned = true;
				break;
			}
		}
		if ( !bPinned )
		{
			Buffers.Add( Candidate );
		}
	}
	BufferBytes += AddedBytes;
	return true;
}

bool CDX12FramePins::PinTexture( const FDX12TextureBinding &Texture )
{
	if ( !Texture.Resource )
	{
		return false;
	}
	for ( const FDX12TextureBinding &Existing : Textures )
	{
		if ( Existing.Resource.Get() == Texture.Resource.Get() )
		{
			return true;
		}
	}
	if ( Textures.Num() >= MaxTextures )
	{
		return false;
	}
	Textures.Add( Texture );
	return true;
}

bool CDX12FramePins::CommitGraphicsAssets( TArrayView<const FDX12TextureBinding> NewTextures, const FDX12GraphicsPipelineBinding &Pipeline )
{
	int32 NewTextureCount = 0; // 未保持の新規画像実体数。
	for ( int32 i = 0; i < NewTextures.Num(); ++i )
	{
		ID3D12Resource *Resource = NewTextures[i].Resource.Get();
		bool bSeen = false;
		for ( const FDX12TextureBinding &Existing : Textures )
		{
			if ( Existing.Resource.Get() == Resource )
			{
				bSeen = true;
				break;
			}
		}
		for ( int32 Earlier = 0; !bSeen && Earlier < i; ++Earlier )
		{
			if ( NewTextures[Earlier].Resource.Get() == Resource )
			{
				bSeen = true;
			}
		}
		if ( !bSeen )
		{
			++NewTextureCount;
		}
	}
	bool bPipelinePinned = false; // 同じPSOの再描画では保持数を増やさない。
	for ( const FDX12GraphicsPipelineBinding &Existing : Pipelines )
	{
		if ( Existing.Pipeline.Get() == Pipeline.Pipeline.Get() )
		{
			bPipelinePinned = true;
			break;
		}
	}
	if ( Textures.Num() + NewTextureCount > MaxTextures || Pipelines.Num() + ( bPipelinePinned ? 0 : 1 ) > MaxPipelines )
	{
		return false;
	}
	for ( const FDX12TextureBinding &Candidate : NewTextures )
	{
		bool bPinned = false;
		for ( const FDX12TextureBinding &Existing : Textures )
		{
			if ( Existing.Resource.Get() == Candidate.Resource.Get() )
			{
				bPinned = true;
				break;
			}
		}
		if ( !bPinned )
		{
			Textures.Add( Candidate );
		}
	}
	if ( !bPipelinePinned )
	{
		Pipelines.Add( Pipeline );
	}
	return true;
}

void CDX12FramePins::MoveTo( TArray<FDX12BufferBinding> &OutBuffers )
{
	for ( FDX12BufferBinding &Pin : Buffers )
	{
		OutBuffers.Add( MoveTemp( Pin ) );
	}
	Buffers.Reset();
	BufferBytes = 0;
}

void CDX12FramePins::MoveTo( TArray<FDX12TextureBinding> &OutTextures, TArray<FDX12GraphicsPipelineBinding> &OutPipelines )
{
	for ( FDX12TextureBinding &Pin : Textures )
	{
		OutTextures.Add( MoveTemp( Pin ) );
	}
	Textures.Reset();
	for ( FDX12GraphicsPipelineBinding &Pin : Pipelines )
	{
		OutPipelines.Add( MoveTemp( Pin ) );
	}
	Pipelines.Reset();
}
