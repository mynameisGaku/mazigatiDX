// SPDX-License-Identifier: MIT
#include "MaterialPreviewSurface.h"

#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Framework/Render/Types/RenderTargetTypes.h"
#include "Library/Math/MathUtils.h"

namespace
{
	const FColor32 BackgroundColor( 38, 40, 48, 255 );

	// 後ろの市松模様（半透明のマテリアルが、見えるように）
	void RecordChecker( FDrawContext& Draw, int32 Size )
	{
		constexpr int32 Cell = 16;
		for ( int32 Y = 0; Y < Size; Y += Cell )
		{
			for ( int32 X = 0; X < Size; X += Cell )
			{
				const bool bLight = ( ( X / Cell ) + ( Y / Cell ) ) % 2 == 0;
				const uint8 Value = bLight ? 92 : 62;
				Draw.Box( X, Y, X + Cell, Y + Cell, FColor32( Value, Value, Value + 6, 255 ), true );
			}
		}
	}

	// ポストプロセス用の決まった場面: 上は色の帯、下は黒から白への勾配
	void RecordTestScene( FDrawContext& Draw, int32 Size )
	{
		const FColor32 Bars[7] = { FColor32( 235, 235, 235 ), FColor32( 235, 225, 40 ), FColor32( 40, 225, 225 ), FColor32( 40, 225, 60 ), FColor32( 225, 50, 225 ), FColor32( 225, 50, 50 ), FColor32( 50, 60, 225 ) };
		const int32 BarHeight = Size * 2 / 3;
		for ( int32 Index = 0; Index < 7; ++Index )
		{
			const int32 Left = Size * Index / 7;
			const int32 Right = Size * ( Index + 1 ) / 7;
			Draw.Box( Left, 0, Right, BarHeight, Bars[Index], true );
		}
		for ( int32 X = 0; X < Size; ++X )
		{
			const uint8 Value = static_cast<uint8>( X * 255 / ( Size - 1 ) );
			Draw.Box( X, BarHeight, X + 1, Size, FColor32( Value, Value, Value, 255 ), true );
		}
	}
}

FMaterialPreviewSurface::~FMaterialPreviewSurface()
{
	Release();
}

void FMaterialPreviewSurface::Release()
{
	Target.Reset();
	SceneTarget.Reset();
}

int32 FMaterialPreviewSurface::GetHandle() const
{
	return Target.IsValid() ? Target->GetImageHandle().Value : -1;
}

bool FMaterialPreviewSurface::EnsureTargets()
{
	if ( Target.IsValid() && SceneTarget.IsValid() )
	{
		return true;
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return false;
	}
	FRenderTargetDesc Desc;
	Desc.Width = Size;
	Desc.Height = Size;
	Desc.bAlpha = false;
	Desc.bDepth = true;
	const TSharedPtr<CRenderTarget> NewTarget = Assets->CreateRenderTarget( "Editor/Material/Preview", Desc );
	Desc.bDepth = false;
	const TSharedPtr<CRenderTarget> NewScene = Assets->CreateRenderTarget( "Editor/Material/PreviewScene", Desc );
	if ( !NewTarget.IsValid() || !NewScene.IsValid() )
	{
		return false;
	}
	Target = NewTarget;
	SceneTarget = NewScene;
	return true;
}

bool FMaterialPreviewSurface::Render( CMaterial& Material, EMaterialPreviewShape Shape, float32 Yaw, float32 Pitch, const FMaterialFrameState& Frame, IDrawExecutor& Executor )
{
	if ( !Material.IsValid() || !EnsureTargets() || !Material.PrepareShaders() )
	{
		return false;
	}
	const float32 Extent = static_cast<float32>( Size );
	const EMaterialDomain Domain = Material.GetDomain();

	// ポストプロセス: 先に、決まった場面を、別の描画先へ
	if ( Domain == EMaterialDomain::PostProcess )
	{
		FDrawBuffer SceneBuffer;
		{
			FDrawContext Draw( SceneBuffer );
			if ( Draw.PushTarget( SceneTarget ) )
			{
				Draw.Clear( BackgroundColor );
				RecordTestScene( Draw, Size );
				Draw.PopTarget();
			}
		}
		Executor.Execute( FDrawSpan { SceneBuffer.Words.GetData(), SceneBuffer.Words.Num() } );
	}

	FDrawBuffer Buffer;
	bool bDrawn = true;
	{
		FDrawContext Draw( Buffer );
		if ( !Draw.PushTarget( Target ) )
		{
			return false;
		}
		Draw.Clear( BackgroundColor );
		Draw.ClearDepth();
		Draw.Blend( EDrawBlend::Opaque );
		Draw.Tint( FColor32( 255, 255, 255 ) );
		switch ( Domain )
		{
		case EMaterialDomain::UI:
		case EMaterialDomain::Particle:
		{
			FDepthState NoDepth;
			NoDepth.bTestDepth = false;
			NoDepth.bWriteDepth = false;
			Draw.Depth( NoDepth );
			RecordChecker( Draw, Size );
			Draw.Blend( EDrawBlend::Alpha, 255 );
			bDrawn = Material.DrawRect( Draw, 0.0f, 0.0f, Extent, Extent, FColor32( 255, 255, 255, 255 ), Frame );
			Draw.Blend( EDrawBlend::Opaque );
			break;
		}
		case EMaterialDomain::PostProcess:
			bDrawn = Material.DrawPostProcess( Draw, 0.0f, 0.0f, Extent, Extent, SceneTarget, TSharedPtr<IImage>(), Frame );
			break;
		default:
		{
			if ( Sphere.Vertices.IsEmpty() )
			{
				Sphere = NMaterialPreview::MakeSphere( 48, 24 );
				Cube = NMaterialPreview::MakeCube();
				Plane = NMaterialPreview::MakePlane();
			}
			const FMaterialPreviewMesh& Mesh = Shape == EMaterialPreviewShape::Cube ? Cube : ( Shape == EMaterialPreviewShape::Plane ? Plane : Sphere );
			const float32 Distance = 3.6f;
			const FVector3 Eye( NMath::Sin( Yaw ) * NMath::Cos( Pitch ) * Distance, NMath::Sin( Pitch ) * Distance, -NMath::Cos( Yaw ) * NMath::Cos( Pitch ) * Distance );
			FCamera Camera = FCamera::MakePerspective( Eye, FVector3( 0.0f, 0.0f, 0.0f ), 0.7f );
			Camera.Near = 0.1f;
			Camera.Far = 50.0f;
			FMaterialSceneState Scene;
			Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( FCameraViewport { Size, Size } );
			Scene.CameraPosition = Eye;
			Scene.Time = Frame.Time;
			Scene.NearPlane = Camera.Near;
			Scene.FarPlane = Camera.Far;
			FDepthState DepthState;
			DepthState.bTestDepth = true;
			DepthState.bWriteDepth = true;
			Draw.Depth( DepthState );
			Draw.Culling( EDrawCulling::None );
			bDrawn = Material.BeginSurface( Draw, Scene );
			if ( bDrawn )
			{
				const FMatrix4x4 World = Shape == EMaterialPreviewShape::Plane ? FMatrix4x4::Scale( FVector3( 1.6f, 1.6f, 1.6f ) ) : FMatrix4x4::Identity;
				Material.DrawSurfaceMesh( Draw, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), World, FColor32( 255, 255, 255, 255 ) );
				Material.EndSurface( Draw );
			}
			break;
		}
		}
		Draw.PopTarget();
	}
	Executor.Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
	return bDrawn;
}
