#include "MeshMaterialEffectPlayback.h"


#include "Library/Core/Types/Numeric.h"

namespace
{
	// 一周期内へ負数も含めて折り返し、UVの小数位相を有限値で返す。
	float64 WrapMeshUVPhase( float64 Phase, float64 Delta )
	{
		// 秒単位scrollを減らしてから加算する折返し値。
		float64 Wrapped = NMath::Fmod( Phase + NMath::Fmod( Delta, 1.0 ), 1.0 );
		if ( Wrapped < 0.0 ) Wrapped += 1.0;
		return Wrapped;
	}

	// loopなら有界のDuration内、one-shotなら終端までの進行秒を返す。
	float64 AdvanceCurveTime( FMeshMaterialEffectPlayback& Playback, float64 DeltaTime, float64 Duration, bool bLoop )
	{
		if ( bLoop )
		{
			Playback.EffectTimeSeconds = NMath::Fmod( Playback.EffectTimeSeconds + NMath::Fmod( DeltaTime, Duration ), Duration );
			return DeltaTime;
		}
		// one-shotの終端より先へ進めない実適用秒数。
		const float64 Applied = NMath::Min( DeltaTime, NMath::Max( 0.0, Duration - Playback.EffectTimeSeconds ) );
		Playback.EffectTimeSeconds += Applied;
		if ( Playback.EffectTimeSeconds >= Duration ) Playback.EffectState = FMeshMaterialEffectPlayback::EEffectPlaybackState::Finished;
		return Applied;
	}

	// 再生開始・停止時に両方のUV位相を同じく初期化する。
	void ResetOffsets( FMeshMaterialEffectPlayback& Playback )
	{
		Playback.EffectUVOffset[0] = Playback.EffectUVOffset[1] = 0.0;
		Playback.DistortionUVOffset[0] = Playback.DistortionUVOffset[1] = 0.0;
	}
}

void FMeshMaterialEffectPlayback::Update( float32 DeltaTime, const FMeshMaterialEffectDefinition& Definition )
{
	if ( EffectState != EEffectPlaybackState::Playing || !NMath::IsFinite( DeltaTime ) || DeltaTime <= 0.0f || !NMath::IsFinite( Definition.Duration ) || Definition.Duration <= 0.0f ) return;
	// float32 deltaを計算用の秒数へ広げた値。
	const float64 SafeDelta = static_cast<float64>( DeltaTime );
	// curveとscrollへ反映する実際の進行秒数。
	const float64 Applied = AdvanceCurveTime( *this, SafeDelta, Definition.Duration, Definition.bLoop );
	EffectUVOffset[0] = WrapMeshUVPhase( EffectUVOffset[0], Applied * Definition.Material.UVScroll.x );
	EffectUVOffset[1] = WrapMeshUVPhase( EffectUVOffset[1], Applied * Definition.Material.UVScroll.y );
	DistortionUVOffset[0] = WrapMeshUVPhase( DistortionUVOffset[0], Applied * Definition.Material.Distortion.UVScroll.x );
	DistortionUVOffset[1] = WrapMeshUVPhase( DistortionUVOffset[1], Applied * Definition.Material.Distortion.UVScroll.y );
}

void FMeshMaterialEffectPlayback::Play()
{
	if ( EffectState == EEffectPlaybackState::Stopped || EffectState == EEffectPlaybackState::Finished )
	{
		EffectTimeSeconds = 0.0;
		ResetOffsets( *this );
	}
	EffectState = EEffectPlaybackState::Playing;
}

void FMeshMaterialEffectPlayback::Pause()
{
	if ( EffectState == EEffectPlaybackState::Playing ) EffectState = EEffectPlaybackState::Paused;
}

void FMeshMaterialEffectPlayback::Stop()
{
	EffectState = EEffectPlaybackState::Stopped;
	EffectTimeSeconds = 0.0;
	ResetOffsets( *this );
}

void FMeshMaterialEffectPlayback::Restart()
{
	EffectState = EEffectPlaybackState::Playing;
	EffectTimeSeconds = 0.0;
	ResetOffsets( *this );
}
