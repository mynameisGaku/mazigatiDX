// SPDX-License-Identifier: MIT
#include "StaticModelGallery.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/Asset/Model/ModelAppearance.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Platform/System/CommandLine.h"

void CStaticModelGallery::OnSpawn()
{
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		Source = Assets->LoadModel( "StaticModel/Pyramid.obj" );
	}
	if ( Source.IsValid() )
	{
		Solid = Source->CreateInstance();
		Faded = Source->CreateInstance();
		const TOptional<FString> ModelOpacity = NCommandLine::GetValue( "static-model-opacity" ); // 効果と個体の不透明度を比較する。
		if ( Solid.IsValid() && ModelOpacity.IsSet() ) { Solid->GetAppearance().SetOpacity( static_cast<float32>( ModelOpacity.GetValue().ToFloat( 1.0 ) ) ); }
		const TOptional<FString> EffectOption = NCommandLine::GetValue( "static-material-opacity" ); // 一材質の選択効果を比べる。
		bMaterialEffectRequested = EffectOption.IsSet();
		if ( bMaterialEffectRequested )
		{
			CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
			FMeshMaterialEffectDefinition Definition;
			Definition.Material.bEnabled = true;
			Definition.Opacity = static_cast<float32>( EffectOption.GetValue().ToFloat( 1.0 ) );
			if ( Assets && Executor && MaterialEffect.Prepare( Source, 0, Definition, [Assets]( const FString& Path ) -> TSharedPtr<CTexture> { return Path == "Effects/material_white.tga" ? TSharedPtr<CTexture>( Assets->CreateDynamicTexture( "static.effect.white", FPixelImage( 1, 1, FColor32::White ) ) ) : Assets->LoadTexture( Path ); }, *Executor ) )
			{
				MaterialEffect.Play();
			}
		}
		if ( Faded.IsValid() )
		{
			Faded->GetAppearance().SetOpacity( 0.4f );
		}
	}
}

void CStaticModelGallery::Tick( float32 DeltaTime )
{
	// 絵を比べる検査のために、--model-time=<秒> で、時計を固定できる（GPU が変換と光を計算する経路と、CPU の経路を、同じ姿で比べる）。
	static const TOptional<FString> FixedTime = NCommandLine::GetValue( "model-time" );
	if ( FixedTime.IsSet() )
	{
		Time = static_cast<float32>( FixedTime.GetValue().ToFloat( 0.0 ) );
		return;
	}
	Time += DeltaTime;
}

void CStaticModelGallery::Draw( FDrawContext& Draw )
{
	const FCamera Camera = FCamera::MakePerspective( FVector3( 5, 4, -9 ), FVector3( 0, 0.6f, 0 ) ); // 二つの個体を同じ視点で比べる。
	if ( Draw.PushCamera( Camera ) )
	{
		static const bool bLit = NCommandLine::HasFlag( "model-lit" ); // 検査用: 光を使う（既定は、光なし）。
		Draw.UseLighting( bLit );

		const TOptional<FString> ForwardOption = NCommandLine::GetValue( "forward-case" ); // 組み合わせの比較撮影用。
		if ( ForwardOption.IsSet() )
		{
			const FString Case = ForwardOption.GetValue(); // CPUとGPUへ同じ描画命令を記録する。
			Draw.UseLighting( true );
			FLightingOptions Options;
			Options.bPixelLighting = Case == "pixel" || Case == "combined";
			Draw.LightingOptions( Options );
			const FColor32 DistanceColor( 70, 100, 160 ); // 距離フォグの比較色。
			const FColor32 HeightColor( 130, 90, 50 ); // 高さフォグの比較色。
			if ( Case == "distance-linear" || Case == "combined" ) { Draw.Fog( FFogSettings::MakeLinear( DistanceColor, 4.0f, 20.0f ) ); }
			if ( Case == "distance-exp" ) { Draw.Fog( FFogSettings::MakeExponential( DistanceColor, 0.08f ) ); }
			if ( Case == "distance-exp2" ) { Draw.Fog( FFogSettings::MakeExponentialSquared( DistanceColor, 0.08f ) ); }
			if ( Case == "height-linear" ) { Draw.HeightFog( FFogSettings::MakeLinear( HeightColor, -1.0f, 3.0f ) ); }
			if ( Case == "height-exp" || Case == "combined" ) { Draw.HeightFog( FFogSettings::MakeExponential( HeightColor, 0.3f, 2.0f ) ); }
			if ( Case == "height-exp2" ) { Draw.HeightFog( FFogSettings::MakeExponentialSquared( HeightColor, 0.3f, 2.0f ) ); }
		}
		if ( Solid.IsValid() )
		{
			const FMatrix4x4 World = FMatrix4x4::RotateY( Time * 0.5f ) * FMatrix4x4::Translate( FVector3( -1.5f, 0, 0 ) ); // 通常描画と効果の配置を揃える。
			if ( bMaterialEffectRequested )
			{
				MaterialEffect.Record( Draw, Solid, World ); // 準備に失敗した場合は比較で検出する。
			}
			else
			{
				Draw.Model( Solid->GetHandle(), World );
			}
		}
		if ( Faded.IsValid() )
		{
			FDepthState Translucent; // 半透明の面を、後の面の深度へ書き込まない。
			Translucent.bWriteDepth = false;
			Draw.Depth( Translucent );
			Draw.Model( Faded->GetHandle(), FMatrix4x4::RotateY( Time * 0.5f ) * FMatrix4x4::Translate( FVector3( 1.5f, 0, 0 ) ) );
		}
		Draw.PopCamera();
	}
	Draw.Text( 20, 80, Solid.IsValid() ? "Shared OBJ geometry / independent opacity" : "Model unavailable on this backend", FColor32( 255, 255, 255 ) );
}
