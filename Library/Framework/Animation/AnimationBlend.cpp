// SPDX-License-Identifier: MIT
#include "Library/Framework/Animation/AnimationBlend.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	// 指定した骨が、対象骨自身か祖先かを、循環で止まらないように調べる。
	bool IsAncestor( TArrayView<const int32> Parents, int32 Ancestor, int32 Frame )
	{
		for ( int32 Steps = 0; Frame >= 0 && Steps < Parents.Num(); ++Steps )
		{
			if ( Frame == Ancestor )
			{
				return true;
			}
			Frame = Parents[Frame];
		}
		return false;
	}

	// NaN、無限大、負の重みは休止層として扱う。
	float64 PositiveFiniteWeight( float32 Weight )
	{
		return NMath::IsFinite( Weight ) && Weight > 0.0f ? static_cast<float64>( Weight ) : 0.0;
	}
}

bool NAnimationBlend::IsValidHierarchy( TArrayView<const int32> Parents )
{
	for ( int32 Frame = 0; Frame < Parents.Num(); ++Frame )
	{
		const int32 Parent = Parents[Frame];
		if ( Parent < -1 || Parent >= Parents.Num() || Parent == Frame )
		{
			return false;
		}
	}
	TArray<uint8> State;
	State.SetNum( Parents.Num(), 0 );
	for ( int32 Frame = 0; Frame < Parents.Num(); ++Frame )
	{
		int32 Current = Frame;
		while ( Current >= 0 && State[Current] == 0 )
		{
			State[Current] = 1;
			Current = Parents[Current];
		}
		if ( Current >= 0 && State[Current] == 1 )
		{
			return false;
		}
		Current = Frame;
		while ( Current >= 0 && State[Current] == 1 )
		{
			State[Current] = 2;
			Current = Parents[Current];
		}
	}
	return true;
}

FAnimationBlendResult NAnimationBlend::Compute( TArrayView<const FAnimationBlendLayer> Layers, TArrayView<const int32> Parents, TArrayView<const uint8> PreviouslyAffectedFrames )
{
	FAnimationBlendResult Result;
	Result.WholeRates.SetNum( Layers.Num(), 0.0f );

	float64 WholeSum = 0.0;
	for ( int32 Index = 0; Index < Layers.Num(); ++Index )
	{
		if ( Layers[Index].PartFrame < 0 )
		{
			WholeSum += PositiveFiniteWeight( Layers[Index].Weight );
		}
	}
	if ( WholeSum > 0.0 )
	{
		for ( int32 Index = 0; Index < Layers.Num(); ++Index )
		{
			if ( Layers[Index].PartFrame < 0 )
			{
				Result.WholeRates[Index] = static_cast<float32>( PositiveFiniteWeight( Layers[Index].Weight ) / WholeSum );
			}
		}
	}

	if ( !IsValidHierarchy( Parents ) )
	{
		return Result;
	}
	for ( int32 Frame = 0; Frame < Parents.Num(); ++Frame )
	{
		bool bAffected = Frame < PreviouslyAffectedFrames.Num() && PreviouslyAffectedFrames[Frame] != 0;
		float64 PartSum = 0.0;
		for ( const FAnimationBlendLayer& Layer : Layers )
		{
			if ( Layer.PartFrame >= 0 && IsAncestor( Parents, Layer.PartFrame, Frame ) )
			{
				bAffected = true;
				PartSum += PositiveFiniteWeight( Layer.Weight );
			}
		}
		if ( !bAffected )
		{
			continue;
		}

		const float64 Share = PartSum < 1.0 ? PartSum : 1.0;
		for ( int32 Index = 0; Index < Layers.Num(); ++Index )
		{
			const FAnimationBlendLayer& Layer = Layers[Index];
			if ( Layer.PartFrame < 0 )
			{
				Result.FrameRates.Add( FAnimationFrameRate { Index, Frame, static_cast<float32>( Result.WholeRates[Index] * ( 1.0 - Share ) ) } );
			}
			else
			{
				const bool bAffectsFrame = Layer.PartFrame >= 0 && IsAncestor( Parents, Layer.PartFrame, Frame );
				const float64 Rate = bAffectsFrame && PartSum > 0.0 ? PositiveFiniteWeight( Layer.Weight ) / PartSum * Share : 0.0;
				Result.FrameRates.Add( FAnimationFrameRate { Index, Frame, static_cast<float32>( Rate ) } );
			}
		}
	}
	return Result;
}
