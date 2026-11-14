// SPDX-License-Identifier: MIT
#include "ModelRootMotionTracker.h"

#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Animation/ModelAnimationSampler.h"
#include "Library/Framework/Animation/RootMotion.h"
#include "Library/Math/MathUtils.h"

void FModelRootMotionTracker::ClearPending()
{
	Pending = FInterval();
}

void FModelRootMotionTracker::Invalidate( bool bRequireRestart )
{
	ClearPending();
	if ( bRequireRestart )
	{
		bRequiresRestart = true;
	}
}

bool FModelRootMotionTracker::IsPlaybackInstanceMismatched( const TSharedPtr<CModelInstance>& Instance ) const
{
	return bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) );
}

void FModelRootMotionTracker::CheckInstance( const TSharedPtr<CModelInstance>& Instance )
{
	if ( IsPlaybackInstanceMismatched( Instance ) )
	{
		bInstanceMismatch = true;
		Invalidate( true );
	}
	if ( Frame >= 0 && ( !ConfiguredInstance.IsValid() || !ConfiguredInstance.HasSameObject( Instance.Get() ) ) )
	{
		bInstanceMismatch = true;
		Invalidate( true );
	}
}

bool FModelRootMotionTracker::IsIdentityValid( const TSharedPtr<CModelInstance>& Instance ) const
{
	if ( Frame < 0 || !Instance.IsValid() || !ConfiguredInstance.IsValid() || !ConfiguredInstance.HasSameObject( Instance.Get() ) || bRequiresRestart || bClipReplacementRequiresClear || bInstanceMismatch )
	{
		return false;
	}
	if ( IsPlaybackInstanceMismatched( Instance ) )
	{
		return false;
	}
	if ( Pending.bValid && ( !Pending.Instance.IsValid() || !Pending.Instance.HasSameObject( Instance.Get() ) ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// 骨の選択
//-----------------------------------------------------------------------------
bool FModelRootMotionTracker::SelectBone( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond, const FString& BoneName )
{
	CheckInstance( Instance );
	if ( Playback.bPlaying && Playback.UnitsPerSecondAtStart != CurrentUnitsPerSecond )
	{
		Invalidate( true );
	}
	if ( !Instance.IsValid() || !Instance->GetSource().IsValid() || !Instance->GetSource()->IsReady() || !Instance->GetSource()->HasMesh() || bInstanceMismatch || bRequiresRestart || bClipReplacementRequiresClear )
	{
		return false;
	}
	const int32 Found = Instance->GetSource()->GetBoneNames().Find( BoneName );   // 骨の名前から、番号を求める
	if ( Found < 0 )
	{
		return false;
	}
	Frame = Found;
	ConfiguredInstance = Instance;
	ClearPending();
	return true;
}

void FModelRootMotionTracker::ClearBone()
{
	Frame = INDEX_NONE;
	ConfiguredInstance.Reset();
	ClearPending();
	Sampler.Reset();
	SamplerTarget.Reset();
	SamplerSource.Reset();
	SamplerAnimIndex = INDEX_NONE;
}

//-----------------------------------------------------------------------------
// アニメーターからの知らせ
//-----------------------------------------------------------------------------
void FModelRootMotionTracker::NotifyClipReplaced()
{
	bClipReplacementRequiresClear = true;
	Invalidate( true );
}

void FModelRootMotionTracker::NotifyClipsCleared()
{
	ClearPending();
	bRequiresRestart = bInstanceMismatch;
	bClipReplacementRequiresClear = false;
}

void FModelRootMotionTracker::NotifyLayerAttached( const TSharedPtr<CModelInstance>& Instance )
{
	if ( !bHasPlaybackInstance )
	{
		PlaybackInstance = Instance;
		bHasPlaybackInstance = true;
	}
	else if ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) )
	{
		bInstanceMismatch = true;
		Invalidate( true );
	}
}

void FModelRootMotionTracker::NotifyPlayStarted()
{
	ClearPending();
	if ( !bClipReplacementRequiresClear && !bInstanceMismatch )
	{
		bRequiresRestart = false;
	}
}

void FModelRootMotionTracker::Validate( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond )
{
	CheckInstance( Instance );
	if ( Playback.bPlaying && Playback.UnitsPerSecondAtStart != CurrentUnitsPerSecond )
	{
		Invalidate( true );
	}
	if ( Pending.bValid && Pending.Segment.UnitsPerSecond != CurrentUnitsPerSecond )
	{
		Invalidate( true );
	}
}

//-----------------------------------------------------------------------------
// 区間
//-----------------------------------------------------------------------------
bool FModelRootMotionTracker::CanAccumulate() const
{
	return Frame >= 0 && !bRequiresRestart && !bClipReplacementRequiresClear && !bInstanceMismatch;
}

void FModelRootMotionTracker::RejectAccumulation()
{
	Invalidate( true );
}

void FModelRootMotionTracker::Accumulate( const FModelRootMotionSegment& Segment, const TSharedPtr<CModelInstance>& Instance, float64 From, float64 To )
{
	if ( From == To )
	{
		return;
	}
	if ( Pending.bValid )
	{
		// 前の区間の終わりから続いていて、条件がすべて同じときだけ、つなぐ
		const FModelRootMotionSegment& Previous = Pending.Segment;
		const bool bSameInterval = Pending.To == From && Previous.ClipSlot == Segment.ClipSlot && Previous.Source.Get() == Segment.Source.Get() && Previous.AnimIndex == Segment.AnimIndex && Previous.LengthUnits == Segment.LengthUnits && Previous.LengthSeconds == Segment.LengthSeconds && Previous.UnitsPerSecond == Segment.UnitsPerSecond && Previous.Loop == Segment.Loop && Pending.Frame == Frame && Pending.Instance.IsValid() && Pending.Instance.HasSameObject( Instance.Get() );
		if ( !bSameInterval )
		{
			Invalidate( true );
			return;
		}
		Pending.To = To;
		return;
	}
	Pending.bValid = true;
	Pending.From = From;
	Pending.To = To;
	Pending.Segment = Segment;
	Pending.Frame = Frame;
	Pending.Instance = Instance;
}

bool FModelRootMotionTracker::TryExtract( const TSharedPtr<CModelInstance>& Instance, const FModelRootMotionPlayback& Playback, float32 CurrentUnitsPerSecond, FMatrix4x4& OutDelta )
{
	Validate( Instance, Playback, CurrentUnitsPerSecond );
	if ( !Pending.bValid || !IsIdentityValid( Instance ) )
	{
		return false;
	}
	if ( !Playback.bPlaying || !Playback.bAttachedToInstance || Playback.UnitsPerSecondAtStart != Pending.Segment.UnitsPerSecond )
	{
		bInstanceMismatch = IsPlaybackInstanceMismatched( Instance );
		Invalidate( true );
		return false;
	}
	const FInterval Interval = Pending;   // 失敗しても、区間を保つための写し
	const TSharedPtr<CModel>& Target = Instance->GetSource();   // 姿勢を読む、対象のメッシュ
	if ( !Target.IsValid() )
	{
		return false;
	}
	if ( Interval.From != Interval.To && ( !Sampler.IsValid() || SamplerTarget.Get() != Target.Get() || SamplerSource.Get() != Interval.Segment.Source.Get() || SamplerAnimIndex != Interval.Segment.AnimIndex ) )
	{
		TSharedPtr<CModelAnimationSampler> NewSampler = CModelAnimationSampler::Create( Target, Interval.Segment.Source, Interval.Segment.AnimIndex );   // 要るときにだけ作る
		if ( !NewSampler.IsValid() )
		{
			return false;
		}
		Sampler = MoveTemp( NewSampler );
		SamplerTarget = Target;
		SamplerSource = Interval.Segment.Source;
		SamplerAnimIndex = Interval.Segment.AnimIndex;
	}
	FMatrix4x4 Delta;   // 成功が決まるまで、呼び出し側の出力に触らない
	const TCallable<bool( float32, FMatrix4x4& )> Sample = [this, &Interval]( float32 SampleSeconds, FMatrix4x4& OutPose ) {
			if ( !Sampler.IsValid() || !NMath::IsFinite( SampleSeconds ) )
			{
				return false;
			}
			const float64 NativeTime64 = static_cast<float64>( SampleSeconds ) * static_cast<float64>( Interval.Segment.UnitsPerSecond );   // 再生のときと同じ時間の単位へ
			if ( !NMath::IsFinite( NativeTime64 ) )
			{
				return false;
			}
			const float32 NativeTime = static_cast<float32>( NMath::Max( 0.0, NMath::Min( NativeTime64, static_cast<float64>( Interval.Segment.LengthUnits ) ) ) );   // 丸めたあとも、クリップの範囲に収める
			return Sampler->SampleFrameLocalMatrix( Interval.Frame, NativeTime, OutPose );
		};
	const bool bExtracted = NRootMotion::ExtractDelta( Interval.From, Interval.To, Interval.Segment.LengthSeconds, Interval.Segment.Loop, Sample, Delta );
	if ( !bExtracted )
	{
		return false;
	}
	OutDelta = Delta;
	return true;
}
