// SPDX-License-Identifier: MIT
#include "WavFile.h"

#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	uint16 ReadLe16( const uint8* Data )
	{
		return static_cast<uint16>( Data[0] | ( Data[1] << 8 ) );
	}

	uint32 ReadLe32( const uint8* Data )
	{
		return static_cast<uint32>( Data[0] ) | ( static_cast<uint32>( Data[1] ) << 8 ) | ( static_cast<uint32>( Data[2] ) << 16 ) | ( static_cast<uint32>( Data[3] ) << 24 );
	}

	void WriteLe16( TArray<uint8>& Out, uint16 Value )
	{
		Out.Add( static_cast<uint8>( Value & 0xFF ) );
		Out.Add( static_cast<uint8>( ( Value >> 8 ) & 0xFF ) );
	}

	void WriteLe32( TArray<uint8>& Out, uint32 Value )
	{
		WriteLe16( Out, static_cast<uint16>( Value & 0xFFFF ) );
		WriteLe16( Out, static_cast<uint16>( ( Value >> 16 ) & 0xFFFF ) );
	}

	void WriteTag( TArray<uint8>& Out, const char* Tag )
	{
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			Out.Add( static_cast<uint8>( Tag[Index] ) );
		}
	}

	constexpr uint16 FormatPcm = 1;
	constexpr uint16 FormatFloat = 3;
	constexpr uint16 FormatExtensible = 0xFFFE;
}

bool NWavFile::Encode( const FPcmBuffer& Pcm, TArray<uint8>& OutBytes )
{
	OutBytes.Reset();
	if ( !Pcm.IsValid() )
	{
		return false;
	}
	const uint32 DataBytes = static_cast<uint32>( Pcm.Samples.Num() ) * 2u;
	const uint32 BlockAlign = static_cast<uint32>( Pcm.Channels ) * 2u;
	OutBytes.Reserve( static_cast<int32>( 44u + DataBytes ) );
	WriteTag( OutBytes, "RIFF" );
	WriteLe32( OutBytes, 36u + DataBytes );
	WriteTag( OutBytes, "WAVE" );
	WriteTag( OutBytes, "fmt " );
	WriteLe32( OutBytes, 16u );
	WriteLe16( OutBytes, FormatPcm );
	WriteLe16( OutBytes, static_cast<uint16>( Pcm.Channels ) );
	WriteLe32( OutBytes, static_cast<uint32>( Pcm.SampleRate ) );
	WriteLe32( OutBytes, static_cast<uint32>( Pcm.SampleRate ) * BlockAlign );
	WriteLe16( OutBytes, static_cast<uint16>( BlockAlign ) );
	WriteLe16( OutBytes, 16 );
	WriteTag( OutBytes, "data" );
	WriteLe32( OutBytes, DataBytes );
	for ( const float32 Sample : Pcm.Samples )
	{
		// 数でない値は無音にする。範囲の外は切る
		const float32 Clamped = Sample == Sample ? NMath::Clamp( Sample, -1.0f, 1.0f ) : 0.0f;
		const int32 Value = static_cast<int32>( NMath::RoundToInt( Clamped * 32767.0f ) );
		WriteLe16( OutBytes, static_cast<uint16>( static_cast<int16>( Value ) ) );
	}
	return true;
}

bool NWavFile::Decode( const uint8* Data, usize Size, FPcmBuffer& OutPcm )
{
	if ( Data == nullptr || Size < 12 || FMemory::Memcmp( Data, "RIFF", 4 ) != 0 || FMemory::Memcmp( Data + 8, "WAVE", 4 ) != 0 )
	{
		return false;
	}
	uint16 Format = 0;
	int32 Channels = 0;
	int32 SampleRate = 0;
	int32 Bits = 0;
	bool bHasFormat = false;
	usize Position = 12;
	while ( Position + 8 <= Size )
	{
		const uint32 ChunkSize = ReadLe32( Data + Position + 4 );
		const uint8* Chunk = Data + Position + 8;
		const usize Available = Size - ( Position + 8 );
		if ( FMemory::Memcmp( Data + Position, "fmt ", 4 ) == 0 && ChunkSize >= 16 && Available >= 16 )
		{
			Format = ReadLe16( Chunk );
			Channels = ReadLe16( Chunk + 2 );
			SampleRate = static_cast<int32>( ReadLe32( Chunk + 4 ) );
			Bits = ReadLe16( Chunk + 14 );
			// 拡張形式は、本当の形式が後ろ（サブ形式の先頭 2 バイト）にある
			if ( Format == FormatExtensible && ChunkSize >= 40 && Available >= 40 )
			{
				Format = ReadLe16( Chunk + 24 );
			}
			bHasFormat = true;
		}
		else if ( FMemory::Memcmp( Data + Position, "data", 4 ) == 0 )
		{
			if ( !bHasFormat || Channels < 1 || Channels > 8 || SampleRate <= 0 )
			{
				return false;
			}
			const bool bInteger = Format == FormatPcm && ( Bits == 8 || Bits == 16 || Bits == 24 || Bits == 32 );
			const bool bFloat = Format == FormatFloat && Bits == 32;
			if ( !bInteger && !bFloat )
			{
				return false;
			}
			const usize BytesPerSample = static_cast<usize>( Bits / 8 );
			// データの大きさが実際より大きく書かれている（壊れた・途中で切れた）ファイルは、あるぶんだけ読む
			const usize DataBytes = static_cast<usize>( ChunkSize ) < Available ? static_cast<usize>( ChunkSize ) : Available;
			const usize FrameBytes = BytesPerSample * static_cast<usize>( Channels );
			const usize Frames = DataBytes / FrameBytes;
			if ( Frames == 0 )
			{
				return false;
			}
			FPcmBuffer Result;
			Result.SampleRate = SampleRate;
			Result.Channels = Channels;
			Result.Samples.Reserve( static_cast<int32>( Frames * static_cast<usize>( Channels ) ) );
			for ( usize Index = 0; Index < Frames * static_cast<usize>( Channels ); ++Index )
			{
				const uint8* Sample = Chunk + Index * BytesPerSample;
				float32 Value = 0.0f;
				if ( bFloat )
				{
					const uint32 Raw = ReadLe32( Sample );
					FMemory::Memcpy( &Value, &Raw, 4 );
				}
				else if ( Bits == 8 )
				{
					Value = ( static_cast<float32>( Sample[0] ) - 128.0f ) / 128.0f;
				}
				else if ( Bits == 16 )
				{
					Value = static_cast<float32>( static_cast<int16>( ReadLe16( Sample ) ) ) / 32768.0f;
				}
				else if ( Bits == 24 )
				{
					int32 Raw = static_cast<int32>( Sample[0] ) | ( static_cast<int32>( Sample[1] ) << 8 ) | ( static_cast<int32>( Sample[2] ) << 16 );
					Raw = ( Raw & 0x800000 ) != 0 ? Raw - 0x1000000 : Raw;
					Value = static_cast<float32>( Raw ) / 8388608.0f;
				}
				else
				{
					Value = static_cast<float32>( static_cast<int32>( ReadLe32( Sample ) ) ) / 2147483648.0f;
				}
				Result.Samples.Add( Value );
			}
			OutPcm = MoveTemp( Result );
			return true;
		}
		Position += 8 + static_cast<usize>( ChunkSize ) + ( ChunkSize & 1u );
	}
	return false;
}
