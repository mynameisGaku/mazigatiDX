#include "MeshRender.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Render/MeshMaterialEffectResources.h"
#include "Library/Framework/Ecs/SystemParams.h"

namespace
{
	using EPlaybackState = FMeshDrawBatch::EEffectPlaybackState;

	// 一周期内へ負数も含めて折り返し、UVの小数位相を有限値で返す。
	float64 WrapMeshUVPhase( float64 Phase, float64 Delta )
	{
		float64 Wrapped = std::fmod( Phase + std::fmod( Delta, 1.0 ), 1.0 );
		if ( Wrapped < 0.0 ) Wrapped += 1.0;
		return Wrapped;
	}

	// 元法線を逆スケール後に回転し、つぶれた軸を安全に0として扱う。
	FVector3 TransformMeshNormal( const FVector3& Normal, const FTransform& World )
	{
		const auto InverseScaled = []( float32 Value, float32 Scale ) { return std::abs( Scale ) > 1.0e-12f ? Value / Scale : 0.0f; };
		const FVector3 InverseScaleNormal( InverseScaled( Normal.x, World.Scale.x ), InverseScaled( Normal.y, World.Scale.y ), InverseScaled( Normal.z, World.Scale.z ) );
		const FVector3 Rotated = World.Rotation.Rotate( InverseScaleNormal );
		const float32 Length = Rotated.Length();
		return std::isfinite( Length ) && Length > 1.0e-12f ? Rotated / Length : FVector3::Zero;
	}

	// 時刻と保持値から、全chunkに共通する検証済み描画設定を作る。
	FDrawMaterialMeshData MakeMeshEffectData( const FMeshMaterialEffectResources& Resources, int32 NumVertices, int32 NumIndices, float32 NormalizedTime, float64 SourceOffsetU, float64 SourceOffsetV, float64 NoiseOffsetU, float64 NoiseOffsetV )
	{
		const FMeshMaterialEffectDefinition& Definition = Resources.Definition;
		FDrawMaterialMeshData Data;
		Data.Texture = Resources.TextureHandle;
		Data.NumVertices = NumVertices;
		Data.NumIndices = NumIndices;
		Data.Material.MaskTexture = Resources.MaskTextureHandle;
		Data.Material.UVScale[0] = Definition.Material.UVScale.x;
		Data.Material.UVScale[1] = Definition.Material.UVScale.y;
		Data.Material.MaskScale = Definition.Material.MaskScale;
		Data.Material.EdgeWidth = Definition.Material.EdgeWidth;
		Data.Material.EdgeEmission = Definition.Material.EdgeEmission;
		Data.Material.EdgeColor = { Definition.Material.EdgeColor.r, Definition.Material.EdgeColor.g, Definition.Material.EdgeColor.b, Definition.Material.EdgeColor.a };
		Data.Material.bDissolve = Definition.Material.bDissolve ? 1 : 0;
		Data.Material.DepthTest = 1;
		Data.Material.DepthWrite = 0;
		Data.UVOffset[0] = static_cast<float32>( SourceOffsetU );
		Data.UVOffset[1] = static_cast<float32>( SourceOffsetV );
		Data.Dissolve = Definition.Material.bDissolve ? Definition.Material.Dissolve.Evaluate( NormalizedTime, 0.0f ) : 0.0f;
		Data.Emission = Definition.Material.Emission.Evaluate( NormalizedTime, 1.0f );
		Data.DistortionStrength = Definition.Material.Distortion.bEnabled ? Definition.Material.Distortion.Strength.Evaluate( NormalizedTime, 0.0f ) : 0.0f;
		Data.DistortionTexture = Data.DistortionStrength > 0.0f ? Resources.DistortionTextureHandle : -1;
		Data.DistortionUVScale[0] = Definition.Material.Distortion.UVScale.x;
		Data.DistortionUVScale[1] = Definition.Material.Distortion.UVScale.y;
		Data.DistortionUVOffset[0] = static_cast<float32>( NoiseOffsetU );
		Data.DistortionUVOffset[1] = static_cast<float32>( NoiseOffsetV );
		Data.bTwoSided = Resources.bTwoSided ? 1 : 0;
		return Data;
	}

	// slot全チャンクをworld座標へ写し、どれか一つでも無効なら何も返さない。
	bool BuildWorldEffectChunks( const FMeshMaterialEffectResources& Resources, const FTransform& World, float32 NormalizedTime, float64 SourceOffsetU, float64 SourceOffsetV, float64 NoiseOffsetU, float64 NoiseOffsetV, TArray<TArray<FMeshVertex>>& OutVertices )
	{
		OutVertices.SetNum( Resources.Chunks.Num() );
		const float32 Opacity = Resources.Definition.Opacity;
		for ( int32 ChunkIndex = 0; ChunkIndex < Resources.Chunks.Num(); ++ChunkIndex )
		{
			const FMeshMaterialEffectResources::FChunk& SourceChunk = Resources.Chunks[ChunkIndex];
			TArray<FMeshVertex>& WorldVertices = OutVertices[ChunkIndex];
			const FDrawMaterialMeshData Data = MakeMeshEffectData( Resources, SourceChunk.Vertices.Num(), SourceChunk.Indices.Num(), NormalizedTime, SourceOffsetU, SourceOffsetV, NoiseOffsetU, NoiseOffsetV );
			if ( !NDrawMaterialMesh::IsValidHeader( Data ) ) return false;
			WorldVertices.SetNum( SourceChunk.Vertices.Num() );
			for ( int32 VertexIndex = 0; VertexIndex < SourceChunk.Vertices.Num(); ++VertexIndex )
			{
				const FMeshVertex& SourceVertex = SourceChunk.Vertices[VertexIndex];
				FMeshVertex& WorldVertex = WorldVertices[VertexIndex];
				const FVector3 LocalPosition( SourceVertex.Position[0], SourceVertex.Position[1], SourceVertex.Position[2] );
				const FVector3 WorldPosition = World.TransformPoint( LocalPosition );
				const FVector3 LocalNormal( SourceVertex.Normal[0], SourceVertex.Normal[1], SourceVertex.Normal[2] );
				const FVector3 WorldNormal = TransformMeshNormal( LocalNormal, World );
				if ( !std::isfinite( WorldPosition.x ) || !std::isfinite( WorldPosition.y ) || !std::isfinite( WorldPosition.z ) || !std::isfinite( WorldNormal.x ) || !std::isfinite( WorldNormal.y ) || !std::isfinite( WorldNormal.z ) ) return false;
				WorldVertex = SourceVertex;
				WorldVertex.Position[0] = WorldPosition.x;
				WorldVertex.Position[1] = WorldPosition.y;
				WorldVertex.Position[2] = WorldPosition.z;
				WorldVertex.Normal[0] = WorldNormal.x;
				WorldVertex.Normal[1] = WorldNormal.y;
				WorldVertex.Normal[2] = WorldNormal.z;
				WorldVertex.Color[3] = static_cast<uint8>( std::clamp( static_cast<int32>( std::lround( static_cast<float64>( SourceVertex.Color[3] ) * Opacity ) ), 0, 255 ) );
			}
			if ( !NDrawMaterialMesh::IsValid( Data, WorldVertices.GetData(), SourceChunk.Indices.GetData() ) ) return false;
		}
		return true;
	}

	// loopなら有界のDuration内、one-shotなら終端までの進行秒を返す。
	float64 AdvanceCurveTime( FMeshDrawBatch& Batch, float64 DeltaTime, float64 Duration )
	{
		if ( Batch.EffectResources->Definition.bLoop )
		{
			Batch.EffectTimeSeconds = std::fmod( Batch.EffectTimeSeconds + std::fmod( DeltaTime, Duration ), Duration );
			return DeltaTime;
		}
		const float64 Applied = std::min( DeltaTime, std::max( 0.0, Duration - Batch.EffectTimeSeconds ) );
		Batch.EffectTimeSeconds += Applied;
		if ( Batch.EffectTimeSeconds >= Duration ) Batch.EffectState = EPlaybackState::Finished;
		return Applied;
	}
}

void FMeshRenderer::Update( float32 DeltaTime )
{
	if ( !std::isfinite( DeltaTime ) || DeltaTime <= 0.0f ) return;
	const float64 SafeDelta = static_cast<float64>( DeltaTime );
	for ( FMeshDrawBatch& Batch : Batches )
	{
		if ( Batch.EffectState != EPlaybackState::Playing || !Batch.EffectResources.IsValid() ) continue;
		const float64 Duration = Batch.EffectResources->Definition.Duration;
		const float64 Applied = AdvanceCurveTime( Batch, SafeDelta, Duration );
		Batch.EffectUVOffset[0] = WrapMeshUVPhase( Batch.EffectUVOffset[0], Applied * Batch.EffectResources->Definition.Material.UVScroll.x );
		Batch.EffectUVOffset[1] = WrapMeshUVPhase( Batch.EffectUVOffset[1], Applied * Batch.EffectResources->Definition.Material.UVScroll.y );
		Batch.DistortionUVOffset[0] = WrapMeshUVPhase( Batch.DistortionUVOffset[0], Applied * Batch.EffectResources->Definition.Material.Distortion.UVScroll.x );
		Batch.DistortionUVOffset[1] = WrapMeshUVPhase( Batch.DistortionUVOffset[1], Applied * Batch.EffectResources->Definition.Material.Distortion.UVScroll.y );
	}
}

void FMeshRenderer::PlayEffects()
{
	for ( FMeshDrawBatch& Batch : Batches )
	{
		if ( !Batch.bEffectRequested ) continue;
		if ( Batch.EffectState == EPlaybackState::Stopped )
		{
			Batch.EffectTimeSeconds = 0.0;
			Batch.EffectUVOffset[0] = Batch.EffectUVOffset[1] = 0.0;
			Batch.DistortionUVOffset[0] = Batch.DistortionUVOffset[1] = 0.0;
		}
		if ( Batch.EffectState == EPlaybackState::Finished )
		{
			Batch.EffectTimeSeconds = 0.0;
			Batch.EffectUVOffset[0] = Batch.EffectUVOffset[1] = 0.0;
			Batch.DistortionUVOffset[0] = Batch.DistortionUVOffset[1] = 0.0;
		}
		Batch.EffectState = EPlaybackState::Playing;
	}
}

bool FMeshRenderer::IsEffectsFinished() const
{
	bool bHasValidEffect = false;
	for ( const FMeshDrawBatch& Batch : Batches )
	{
		if ( !Batch.bEffectRequested || !Batch.EffectResources.IsValid() ) continue;
		bHasValidEffect = true;
		if ( Batch.EffectState != EPlaybackState::Finished ) return false;
	}
	return bHasValidEffect;
}

void FMeshRenderer::PauseEffects()
{
	for ( FMeshDrawBatch& Batch : Batches ) if ( Batch.EffectState == EPlaybackState::Playing ) Batch.EffectState = EPlaybackState::Paused;
}

void FMeshRenderer::StopEffects()
{
	for ( FMeshDrawBatch& Batch : Batches )
	{
		if ( !Batch.bEffectRequested ) continue;
		Batch.EffectState = EPlaybackState::Stopped;
		Batch.EffectTimeSeconds = 0.0;
		Batch.EffectUVOffset[0] = Batch.EffectUVOffset[1] = 0.0;
		Batch.DistortionUVOffset[0] = Batch.DistortionUVOffset[1] = 0.0;
	}
}

void FMeshRenderer::RestartEffects()
{
	for ( FMeshDrawBatch& Batch : Batches )
	{
		if ( !Batch.bEffectRequested ) continue;
		Batch.EffectState = EPlaybackState::Playing;
		Batch.EffectTimeSeconds = 0.0;
		Batch.EffectUVOffset[0] = Batch.EffectUVOffset[1] = 0.0;
		Batch.DistortionUVOffset[0] = Batch.DistortionUVOffset[1] = 0.0;
	}
}

bool FMeshRenderer::IsEffectsPlaying() const
{
	for ( const FMeshDrawBatch& Batch : Batches ) if ( Batch.bEffectRequested && Batch.EffectState == EPlaybackState::Playing ) return true;
	return false;
}

bool FMeshRenderer::IsEffectsPaused() const
{
	for ( const FMeshDrawBatch& Batch : Batches ) if ( Batch.bEffectRequested && Batch.EffectState == EPlaybackState::Paused ) return true;
	return false;
}

float64 FMeshRenderer::GetEffectTime( int32 BatchIndex ) const
{
	if ( !Batches.IsValidIndex( BatchIndex ) || !Batches[BatchIndex].bEffectRequested || Batches[BatchIndex].EffectState == EPlaybackState::Stopped ) return 0.0;
	return Batches[BatchIndex].EffectTimeSeconds;
}

bool FMeshRenderer::HasEffectPreparationFailures() const
{
	for ( const FMeshDrawBatch& Batch : Batches ) if ( Batch.bEffectRequested && !Batch.EffectResources.IsValid() ) return true;
	return false;
}

void FMeshRenderer::Record( FDrawContext& Draw, const FTransform& World ) const
{
	if ( !bVisible || !Buffer.IsValid() || Buffer->GetVertexBuffer() < 0 || Buffer->GetIndexBuffer() < 0 ) return;
	const FMatrix4x4 Matrix = World.ToMatrix();
	for ( const FMeshDrawBatch& Batch : Batches )
	{
		if ( Batch.NumIndices < 3 ) continue;
		if ( !Batch.bEffectRequested || Batch.EffectState == EPlaybackState::Stopped )
		{
			const int32 TextureHandle = Batch.Texture.IsValid() ? Batch.Texture->GetHandle() : -1;
			Draw.Mesh3D( Buffer->GetVertexBuffer(), Buffer->GetIndexBuffer(), Buffer->GetNumVertices(), Batch.StartIndex, Batch.NumIndices, TextureHandle, Matrix, Batch.bTwoSided );
			continue;
		}
		if ( !Batch.EffectResources.IsValid() || Batch.EffectResources->Chunks.IsEmpty() ) continue;
		const FMeshMaterialEffectResources& Resources = *Batch.EffectResources;
		const float32 Duration = Resources.Definition.Duration;
		const float32 NormalizedTime = std::clamp( static_cast<float32>( Batch.EffectTimeSeconds / Duration ), 0.0f, 1.0f );
		TArray<TArray<FMeshVertex>> WorldVertices;
		if ( !BuildWorldEffectChunks( Resources, World, NormalizedTime, Batch.EffectUVOffset[0], Batch.EffectUVOffset[1], Batch.DistortionUVOffset[0], Batch.DistortionUVOffset[1], WorldVertices ) ) continue;
		const EDrawBlend PreviousBlend = Draw.GetBlendMode();
		const int32 PreviousBlendParam = Draw.GetBlendParam();
		if ( PreviousBlend != Resources.Definition.Blend || PreviousBlendParam != 255 ) Draw.Blend( Resources.Definition.Blend, 255 );
		for ( int32 ChunkIndex = 0; ChunkIndex < Resources.Chunks.Num(); ++ChunkIndex )
		{
			const FMeshMaterialEffectResources::FChunk& SourceChunk = Resources.Chunks[ChunkIndex];
			const FDrawMaterialMeshData Data = MakeMeshEffectData( Resources, WorldVertices[ChunkIndex].Num(), SourceChunk.Indices.Num(), NormalizedTime, Batch.EffectUVOffset[0], Batch.EffectUVOffset[1], Batch.DistortionUVOffset[0], Batch.DistortionUVOffset[1] );
			Draw.MaterialMesh3D( Data, WorldVertices[ChunkIndex].GetData(), SourceChunk.Indices.GetData() );
		}
		if ( PreviousBlend != Resources.Definition.Blend || PreviousBlendParam != 255 ) Draw.Blend( PreviousBlend, PreviousBlendParam );
	}
}

void FMeshRenderer::RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const
{
	Record( Draw, Transform.GetWorld() );
}
