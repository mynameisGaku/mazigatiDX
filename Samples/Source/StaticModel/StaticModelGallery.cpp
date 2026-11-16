// SPDX-License-Identifier: MIT
#include "StaticModelGallery.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
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
		if ( Solid.IsValid() )
		{
			Draw.Model( Solid->GetHandle(), FMatrix4x4::RotateY( Time * 0.5f ) * FMatrix4x4::Translate( FVector3( -1.5f, 0, 0 ) ) );
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
