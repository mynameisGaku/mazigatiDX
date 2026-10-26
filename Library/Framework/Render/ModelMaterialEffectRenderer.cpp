// SPDX-License-Identifier: NOASSERTION
#include "ModelMaterialEffectRenderer.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Framework/Render/ModelMaterialEffectResources.h"
#include "Library/Framework/Render/DrawContext.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelMaterialEffect, Log )

namespace
{
	// 読み込み時だけ画像を取得し、readyでない画像は資源へ固定しない。
	TSharedPtr<CTexture> LoadReadyModelEffectTexture( const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const FString& Path )
	{
		if ( !LoadTexture || Path.IsEmpty() ) return nullptr;
		// ローダーから得た共有画像。
		TSharedPtr<CTexture> Texture = LoadTexture( Path );
		return Texture.IsValid() && Texture->IsReady() && Texture->GetHandle() >= 0 ? Texture : TSharedPtr<CTexture>();
	}

	// 有効な強度keyがあるときだけゆがみ画像を必要とする。
	bool ModelEffectNeedsDistortionTexture( const FEffectUVDistortionDefinition& Distortion )
	{
		if ( !Distortion.bEnabled ) return false;
		// textureを必要とする正のstrength key。
		for ( const FEffectScalarCurve::FKey& Key : Distortion.Strength.Keys ) if ( Key.Value > 0.0f ) return true;
		return false;
	}

	// curve時刻と個体offsetからfixed ModelMaterial commandの材質値を作る。
	FDrawMaterialMeshData MakeModelEffectMaterial( const FModelMaterialEffectResources& Resources, float32 NormalizedTime, float64 SourceOffsetU, float64 SourceOffsetV, float64 NoiseOffsetU, float64 NoiseOffsetV )
	{
		// 描画値へ変換するimmutable定義。
		const FMeshMaterialEffectDefinition& Definition = Resources.Definition;
		// 固定ModelMaterial commandに含める効果材質値。
		FDrawMaterialMeshData Data;
		Data.Texture = Resources.TextureHandle;
		Data.NumVertices = 0;
		Data.NumIndices = 0;
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

	// affine matrix12要素をfixed commandへ値コピーする。
	void PackModelMatrix( float32 ( &Out )[12], const FMatrix4x4& Matrix )
	{
		// affine行列で現在読み出す行。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			// affine行列で出力する3成分。
			for ( int32 Column = 0; Column < 3; ++Column ) Out[Row * 3 + Column] = Matrix.m[Row][Column];
		}
	}
}

bool FModelMaterialEffectRenderer::Prepare( const TSharedPtr<CModel>& Source, int32 MaterialIndex, const FMeshMaterialEffectDefinition& Definition, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const IDrawExecutor& Executor, bool bTwoSided )
{
	Clear();
	if ( !Definition.Material.bEnabled ) return false;
	if ( !NAssetService::IsMainThread() )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果の準備はアセットのメインスレッドで行う必要があります。" );
		return false;
	}
	if ( !Source.IsValid() || Source->GetHandle() < 0 )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果の元モデルが無効です。" );
		return false;
	}
	if ( !Definition.IsValidSettings() )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果の設定が不正です。" );
		return false;
	}
	if ( !Executor.SupportsModelMaterialEffects() )
	{
		LOG( LogModelMaterialEffect, Warning, "現在の描画実行先はモデル材質効果に対応していません。" );
		return false;
	}
	if ( !Source->IsReady() )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果の元モデルがまだ使用できません。" );
		return false;
	}
	// 借用画像と三角形の対応を調べる不変面データ。
	const TSharedPtr<const FStaticModelSurface> Snapshot = Source->CreateStaticSurfaceSnapshot();
	if ( !Snapshot.IsValid() )
	{
		LOG( LogModelMaterialEffect, Warning, "元モデルのbackendは静的な面データを提供できません。" );
		return false;
	}
	// 選択材質の範囲を検証する取得済み面データ。
	const FStaticModelSurfaceData& Surface = Snapshot->GetData();
	if ( !Surface.Materials.IsValidIndex( MaterialIndex ) || Surface.Indices.IsEmpty() )
	{
		LOG( LogModelMaterialEffect, Warning, "静的モデルに指定材質の面がありません。" );
		return false;
	}
	// 選択材質が有効なtriangle sectionを少なくとも一つ持つか確かめる。
	// 選択材質が描ける三角形を持つことを示す結果。
	bool bHasSelectedIndices = false;
	// section列を順に検査する範囲値。
	for ( const FStaticModelSurfaceSection& Section : Surface.Sections )
	{
		if ( Section.MaterialIndex != MaterialIndex || Section.NumIndices < 3 || Section.NumIndices % 3 != 0 || Section.FirstIndex < 0 || Section.FirstIndex > Surface.Indices.Num() - Section.NumIndices ) continue;
		bHasSelectedIndices = true;
		break;
	}
	if ( !bHasSelectedIndices )
	{
		LOG( LogModelMaterialEffect, Warning, "静的モデルに指定材質の有効な三角形範囲がありません。" );
		return false;
	}

	// 全準備が成功するまで公開しない作業資源。
	TSharedPtr<FModelMaterialEffectResources> NewResources = MakeShared<FModelMaterialEffectResources>();
	NewResources->Source = Snapshot->GetSource();
	NewResources->Definition = Definition;
	NewResources->bTwoSided = bTwoSided;
	// 選択した元材質のborrowed image情報。
	const FStaticModelSurfaceMaterial& SurfaceMaterial = Surface.Materials[MaterialIndex];
	if ( SurfaceMaterial.BorrowedDiffuseTexture < -1 )
	{
		LOG( LogModelMaterialEffect, Warning, "元モデルの材質画像handleが不正です。" );
		return false;
	}
	if ( SurfaceMaterial.BorrowedDiffuseTexture >= 0 ) NewResources->TextureHandle = SurfaceMaterial.BorrowedDiffuseTexture;
	else
	{
		// 元Modelに画像が無いときだけ所有する組み込み白画像を読み込む。
		NewResources->OwnedTexture = LoadReadyModelEffectTexture( LoadTexture, FString( "Effects/material_white.tga" ) );
		if ( !NewResources->OwnedTexture.IsValid() )
		{
			LOG( LogModelMaterialEffect, Warning, "モデル材質効果用の白画像を読み込めませんでした。" );
			return false;
		}
		NewResources->TextureHandle = NewResources->OwnedTexture->GetHandle();
	}
	if ( Definition.Material.bDissolve && !Definition.Material.MaskTexture.IsEmpty() )
	{
		NewResources->MaskTexture = LoadReadyModelEffectTexture( LoadTexture, Definition.Material.MaskTexture );
		if ( !NewResources->MaskTexture.IsValid() )
		{
			LOG( LogModelMaterialEffect, Warning, "モデル材質効果の消失mask画像を読み込めませんでした。" );
			return false;
		}
		NewResources->MaskTextureHandle = NewResources->MaskTexture->GetHandle();
	}
	if ( ModelEffectNeedsDistortionTexture( Definition.Material.Distortion ) )
	{
		NewResources->DistortionTexture = LoadReadyModelEffectTexture( LoadTexture, Definition.Material.Distortion.Texture );
		if ( !NewResources->DistortionTexture.IsValid() )
		{
			LOG( LogModelMaterialEffect, Warning, "モデル材質効果のゆがみ画像を読み込めませんでした。" );
			return false;
		}
		NewResources->DistortionTextureHandle = NewResources->DistortionTexture->GetHandle();
	}
	if ( NewResources->TextureHandle < 0 || ( NewResources->MaskTexture.IsValid() && NewResources->MaskTextureHandle < 0 ) || ( NewResources->DistortionTexture.IsValid() && NewResources->DistortionTextureHandle < 0 ) )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果の画像handleが無効です。" );
		return false;
	}
	// 画像をすべてready確認したあと、選択材質の不変geometryを作る。
	// source modelと同じasset backendへgeometryを作る窓口。
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr )
	{
		LOG( LogModelMaterialEffect, Warning, "モデル材質効果のgeometry作成先がありません。" );
		return false;
	}
	NewResources->GeometryHandle = Backend->CreateModelMaterialGeometry( Surface, MaterialIndex );
	if ( NewResources->GeometryHandle < 0 )
	{
		LOG( LogModelMaterialEffect, Warning, "backendがモデル材質効果のgeometryを作成できませんでした。" );
		return false;
	}
	Resources = NewResources;
	Restart();
	return true;
}

void FModelMaterialEffectRenderer::Clear()
{
	Resources.Reset();
	Stop();
}

void FModelMaterialEffectRenderer::Update( float32 DeltaTime )
{
	if ( !Resources.IsValid() ) return;
	FMeshMaterialEffectPlayback::Update( DeltaTime, Resources->Definition );
}

void FModelMaterialEffectRenderer::Play()
{
	if ( Resources.IsValid() ) FMeshMaterialEffectPlayback::Play();
}

void FModelMaterialEffectRenderer::Pause()
{
	FMeshMaterialEffectPlayback::Pause();
}

void FModelMaterialEffectRenderer::Stop()
{
	FMeshMaterialEffectPlayback::Stop();
}

void FModelMaterialEffectRenderer::Restart()
{
	if ( Resources.IsValid() ) FMeshMaterialEffectPlayback::Restart();
}

bool FModelMaterialEffectRenderer::IsPrepared() const
{
	return Resources.IsValid();
}

FMeshMaterialEffectPlayback::EEffectPlaybackState FModelMaterialEffectRenderer::GetPlaybackState() const
{
	return EffectState;
}

float64 FModelMaterialEffectRenderer::GetEffectTime() const
{
	return EffectState == EEffectPlaybackState::Stopped ? 0.0 : EffectTimeSeconds;
}

bool FModelMaterialEffectRenderer::Record( FDrawContext& Draw, const TSharedPtr<CModelInstance>& Instance, const FMatrix4x4& World ) const
{
	if ( EffectState == EEffectPlaybackState::Stopped || !Resources.IsValid() || !Instance.IsValid() || Instance->GetHandle() < 0 || Instance->GetNumAttached() > 0 || Instance->GetSource().Get() != Resources->Source.Get() ) return false;
	// active effectが持つ不正行列でnative full Modelへ漏らさず、このframeの描画を抑止する。
	// 変換行列全体が有限かを調べる行。
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		// 現在行の有限性を確かめる列。
		for ( int32 Column = 0; Column < 4; ++Column ) if ( !NMath::IsFinite( World.m[Row][Column] ) ) return true;
	}
	// curve評価へ渡す正規化時刻。
	const float32 NormalizedTime = NMath::Clamp( static_cast<float32>( EffectTimeSeconds / Resources->Definition.Duration ), 0.0f, 1.0f );
	// 固定命令へ詰めるModel・材質・効果値。
	FDrawModelMaterialData Data;
	Data.Handle = Instance->GetHandle();
	PackModelMatrix( Data.M, World );
	Data.Geometry = Resources->GeometryHandle;
	Data.BlendMode = static_cast<int32>( Resources->Definition.Blend );
	Data.Opacity = Resources->Definition.Opacity;
	Data.Material = MakeModelEffectMaterial( *Resources, NormalizedTime, EffectUVOffset[0], EffectUVOffset[1], DistortionUVOffset[0], DistortionUVOffset[1] );
	return Draw.ModelMaterial( Data );
}
