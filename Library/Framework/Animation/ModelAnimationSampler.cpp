// SPDX-License-Identifier: MIT
#include "ModelAnimationSampler.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Model.h"

namespace
{
	// backend出力が有限な親基準の行列であることを確認する。
	bool IsFiniteAffineMatrix( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		constexpr float32 Tolerance = 1.0e-4f;
		return NMath::Abs( Matrix.m[0][3] ) <= Tolerance && NMath::Abs( Matrix.m[1][3] ) <= Tolerance && NMath::Abs( Matrix.m[2][3] ) <= Tolerance && NMath::Abs( Matrix.m[3][3] - 1.0f ) <= Tolerance;
	}
}

TSharedPtr<CModelAnimationSampler> CModelAnimationSampler::Create( const TSharedPtr<CModel>& TargetModel, const TSharedPtr<CModel>& AnimationSource, int32 AnimIndex )
{
	GAME_THREAD_ONLY( nullptr );
	if ( !NAssetService::IsMainThread() || !TargetModel.IsValid() || !AnimationSource.IsValid() || !TargetModel->IsReady() || !AnimationSource->IsReady() || !TargetModel->HasMesh() )
	{
		return nullptr;
	}
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr )
	{
		return nullptr;
	}
	const TArray<FModelClipInfo>& Clips = AnimationSource->GetClips();
	const FModelClipInfo* Clip = Clips.FindByPredicate( [AnimIndex]( const FModelClipInfo& Entry ) { return Entry.AnimIndex == AnimIndex; } );
	const int32 FrameCount = TargetModel->GetBoneNames().Num();
	if ( Clip == nullptr || !NMath::IsFinite( Clip->Length ) || Clip->Length <= 0.0f || FrameCount <= 0 )
	{
		return nullptr;
	}
	TSharedPtr<CModelInstance> Instance = TargetModel->CreateInstance();
	if ( !Instance.IsValid() )
	{
		return nullptr;
	}
	const int32 AttachIndex = Instance->AttachAnim( AnimationSource, AnimIndex, false );
	if ( AttachIndex < 0 )
	{
		return nullptr;
	}
	FMatrix4x4 Probe;
	if ( !Backend->SampleModelAnimFrameLocalMatrix( Instance->GetHandle(), AttachIndex, 0, 0.0f, Probe ) || !IsFiniteAffineMatrix( Probe ) )
	{
		return nullptr;
	}
	TSharedPtr<CModelAnimationSampler> Sampler = MakeShared<CModelAnimationSampler>();
	Sampler->Instance = MoveTemp( Instance );
	Sampler->Backend = Backend;
	Sampler->AttachIndex = AttachIndex;
	Sampler->FrameCount = FrameCount;
	Sampler->ClipLength = Clip->Length;
	return Sampler;
}

bool CModelAnimationSampler::SampleFrameLocalMatrix( int32 Frame, float32 Time, FMatrix4x4& OutMatrix ) const
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() || !Instance.IsValid() || Frame < 0 || Frame >= FrameCount || !NMath::IsFinite( Time ) || Time < 0.0f || Time > ClipLength )
	{
		return false;
	}
	if ( Backend == nullptr || NAssetService::GetBackend() != Backend )
	{
		return false;
	}
	FMatrix4x4 Sampled;
	if ( !Backend->SampleModelAnimFrameLocalMatrix( Instance->GetHandle(), AttachIndex, Frame, Time, Sampled ) || !IsFiniteAffineMatrix( Sampled ) )
	{
		return false;
	}
	OutMatrix = Sampled;
	return true;
}
