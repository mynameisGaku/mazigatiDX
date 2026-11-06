// SPDX-License-Identifier: MIT
#include "DrawModelFramePoseData.h"

#include "Library/Core/Collection/Set.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"

static_assert( sizeof( FDrawModelFrameOverrideData ) == 68 );
static_assert( alignof( FDrawModelFrameOverrideData ) == 4 );
static_assert( bIsTriviallyCopyable<FDrawModelFrameOverrideData> );
static_assert( sizeof( FDrawModelFramePoseData ) == 64 );
static_assert( sizeof( FDrawModelFramePoseData ) % 4 == 0 );
static_assert( bIsTriviallyCopyable<FDrawModelFramePoseData> );

namespace
{
	// 12値のaffine行列がすべて有限かを調べる。
	bool HasFiniteAffineValues( const float32 ( &Values )[12] )
	{
		// 12個の保存行列値を検査する添字。
		for ( int32 Index = 0; Index < 12; ++Index )
		{
			if ( !NMath::IsFinite( Values[Index] ) )
			{
				return false;
			}
		}
		return true;
	}

	// 4行4列値が有限かつ最後の列が厳密なaffine値かを調べる。
	bool IsCanonicalAffineValues( const float32 ( &Values )[16] )
	{
		// 行列要素の有限性を検査する添字。
		for ( int32 Index = 0; Index < 16; ++Index )
		{
			if ( !NMath::IsFinite( Values[Index] ) )
			{
				return false;
			}
		}
		return Values[3] == 0.0f && Values[7] == 0.0f && Values[11] == 0.0f && Values[15] == 1.0f;
	}

	// command payloadから、指定indexのpose byte offsetを得る。
	const uint32* GetOverrideWords( const FDrawCommandView& Command, int32 Index )
	{
		// 固定headerからpose末尾までのword数。
		const int32 HeaderWords = static_cast<int32>( sizeof( FDrawModelFramePoseData ) / 4 );
		// 1個のpose recordが占めるword数。
		const int32 OverrideWords = static_cast<int32>( sizeof( FDrawModelFrameOverrideData ) / 4 );
		return Command.Data + HeaderWords + Index * OverrideWords;
	}
}

bool NDrawModelFramePose::IsValidHeader( const FDrawModelFramePoseData& Data )
{
	if ( Data.Handle < 0 || Data.AnimIndex < -1 || !NMath::IsFinite( Data.AnimTime ) || Data.AnimTime < 0.0f || Data.NumOverrides < 0 || Data.NumOverrides > MaxOverrides || !HasFiniteAffineValues( Data.M ) )
	{
		return false;
	}
	return true;
}

bool NDrawModelFramePose::IsValidOverrides( const FDrawModelFrameOverrideData* Overrides, int32 NumOverrides )
{
	if ( NumOverrides < 0 || NumOverrides > MaxOverrides || ( NumOverrides > 0 && Overrides == nullptr ) )
	{
		return false;
	}
	TSet<int32> Frames; // 重複したbone番号を検出する集合。
	Frames.Reserve( NumOverrides );
	// 入力poseを1件ずつ検査する配列位置。
	for ( int32 Index = 0; Index < NumOverrides; ++Index )
	{
		const FDrawModelFrameOverrideData& Override = Overrides[Index]; // 検査中のpose値。
		if ( Override.Frame < 0 || Frames.Contains( Override.Frame ) || !IsCanonicalAffineValues( Override.M ) )
		{
			return false;
		}
		Frames.Add( Override.Frame );
	}
	return true;
}

bool NDrawModelFramePose::IsValidCommand( const FDrawCommandView& Command, FDrawModelFramePoseData& OutData )
{
	if ( Command.Type != EDrawCommand::ModelFramePose3D || Command.Flags != 0 || Command.Data == nullptr || Command.NumDataWords < static_cast<int32>( sizeof( FDrawModelFramePoseData ) / 4 ) )
	{
		return false;
	}
	FDrawModelFramePoseData Data{}; // commandから読む固定header。
	FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
	// 固定headerが占めるword数。
	const int32 HeaderWords = static_cast<int32>( sizeof( FDrawModelFramePoseData ) / 4 );
	// 1個のpose recordが占めるword数。
	const int32 OverrideWords = static_cast<int32>( sizeof( FDrawModelFrameOverrideData ) / 4 );
	if ( !IsValidHeader( Data ) || Command.NumDataWords != HeaderWords + Data.NumOverrides * OverrideWords )
	{
		return false;
	}
	TSet<int32> Frames; // payload内のframe重複を検出する集合。
	Frames.Reserve( Data.NumOverrides );
	// command末尾のpose recordを順に検査する位置。
	for ( int32 Index = 0; Index < Data.NumOverrides; ++Index )
	{
		FDrawModelFrameOverrideData Override; // aligned localへ読むpose値。
		FMemory::Memcpy( &Override, GetOverrideWords( Command, Index ), sizeof( Override ) );
		if ( Override.Frame < 0 || Frames.Contains( Override.Frame ) || !IsCanonicalAffineValues( Override.M ) )
		{
			return false;
		}
		Frames.Add( Override.Frame );
	}
	OutData = Data;
	return true;
}

bool NDrawModelFramePose::ReadOverride( const FDrawCommandView& Command, int32 Index, FDrawModelFrameOverrideData& OutOverride )
{
	if ( Command.Type != EDrawCommand::ModelFramePose3D || Command.Flags != 0 || Command.Data == nullptr || Index < 0 || Index >= MaxOverrides || Command.NumDataWords < static_cast<int32>( sizeof( FDrawModelFramePoseData ) / 4 ) )
	{
		return false;
	}
	FDrawModelFramePoseData Data{}; // record数を読む固定header。
	FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
	const int32 HeaderWords = static_cast<int32>( sizeof( FDrawModelFramePoseData ) / 4 ); // 固定headerのword数。
	const int32 OverrideWords = static_cast<int32>( sizeof( FDrawModelFrameOverrideData ) / 4 ); // 1pose recordのword数。
	if ( !IsValidHeader( Data ) || Command.NumDataWords != HeaderWords + Data.NumOverrides * OverrideWords || Index >= Data.NumOverrides )
	{
		return false;
	}
	FDrawModelFrameOverrideData Override; // aligned localへ複写するpose値。
	FMemory::Memcpy( &Override, GetOverrideWords( Command, Index ), sizeof( Override ) );
	if ( Override.Frame < 0 || !IsCanonicalAffineValues( Override.M ) )
	{
		return false;
	}
	OutOverride = Override;
	return true;
}
