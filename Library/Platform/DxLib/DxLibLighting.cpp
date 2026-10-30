// SPDX-License-Identifier: MIT
#include "DxLibLighting.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 描画の終わりに戻す材質と光の計算の仕方（DxLib に取得関数が無いので、こちらで持つ。メインスレッドだけが読み書きする）
	FDrawSurfaceMaterialData GDefaultMaterial;
	FDrawLightingOptionsData GDefaultOptions;

	COLOR_F ToColor( const float32 ( &Rgb )[3], float32 Alpha )
	{
		return GetColorF( Rgb[0], Rgb[1], Rgb[2], Alpha );
	}

	VECTOR ToVector( const float32 ( &Xyz )[3] )
	{
		return VGet( Xyz[0], Xyz[1], Xyz[2] );
	}

	// 向きは、長さ 1 にして渡す（DxLib は、方向ライトでは正規化するが、スポットライトの円錐の計算では、向きの長さがそのまま効いてしまう）
	VECTOR ToDirection( const float32 ( &Xyz )[3] )
	{
		return VNorm( ToVector( Xyz ) );
	}

	// 材質を DxLib へ反映する
	void ApplyMaterial( const FDrawSurfaceMaterialData& Data )
	{
		MATERIALPARAM Material = {};
		Material.Diffuse = ToColor( Data.Diffuse, 1.0f );
		Material.Ambient = ToColor( Data.Ambient, 0.0f );
		Material.Specular = ToColor( Data.Specular, 0.0f );
		Material.Emissive = ToColor( Data.Emissive, 0.0f );
		Material.Power = Data.Shininess;
		SetMaterialParam( Material );
		SetMaterialUseVertDifColor( Data.UseVertexDiffuse != 0 ? TRUE : FALSE );
		SetMaterialUseVertSpcColor( Data.UseVertexSpecular != 0 ? TRUE : FALSE );
	}

	// 光の計算の仕方を DxLib へ反映する
	void ApplyOptions( const FDrawLightingOptionsData& Data )
	{
		SetUseSpecular( Data.UseSpecular != 0 ? TRUE : FALSE );
		SetUsePixelLighting( Data.PixelLighting != 0 ? TRUE : FALSE );
		SetUseHalfLambertLighting( Data.WrapLighting != 0 ? TRUE : FALSE );
		// DxLib の「角度の減衰」は、面の向きと光の角度による明るさの変化のこと（切ると、面の向きに関係なく全力で照らす）
		SetUseLightAngleAttenuation( Data.IgnoreSurfaceAngle != 0 ? FALSE : TRUE );
	}

	// ライト 1 つの DxLib のハンドルを作る（失敗したら -1）
	int CreateLight( const FDrawLightData& Data )
	{
		int Handle = -1;
		switch ( static_cast<EDrawLightType>( Data.Type ) )
		{
		case EDrawLightType::Point:
			Handle = CreatePointLightHandle( ToVector( Data.Position ), Data.Range, Data.ConstantAttenuation, Data.LinearAttenuation, Data.QuadraticAttenuation );
			break;
		case EDrawLightType::Spot:
			Handle = CreateSpotLightHandle( ToVector( Data.Position ), ToDirection( Data.Direction ), Data.OuterAngle, Data.InnerAngle, Data.Range, Data.ConstantAttenuation, Data.LinearAttenuation, Data.QuadraticAttenuation );
			break;
		default:
			Handle = CreateDirLightHandle( ToDirection( Data.Direction ) );
			break;
		}
		if ( Handle < 0 )
		{
			return -1;
		}
		SetLightDifColorHandle( Handle, ToColor( Data.Diffuse, 1.0f ) );
		SetLightSpcColorHandle( Handle, ToColor( Data.Specular, 0.0f ) );
		SetLightAmbColorHandle( Handle, ToColor( Data.Ambient, 0.0f ) );
		SetLightEnableHandle( Handle, TRUE );
		return Handle;
	}
}

void NDxLibLighting::ApplyDefaults()
{
	GDefaultMaterial = FDrawSurfaceMaterialData();
	GDefaultOptions = FDrawLightingOptionsData();
	SetUseLighting( TRUE );
	SetGlobalAmbientLight( GetColorF( 0.0f, 0.0f, 0.0f, 0.0f ) );
	ApplyMaterial( GDefaultMaterial );
	ApplyOptions( GDefaultOptions );
}

FDrawSurfaceMaterialData NDxLibLighting::GetDefaultMaterial()
{
	return GDefaultMaterial;
}

FDrawLightingOptionsData NDxLibLighting::GetDefaultOptions()
{
	return GDefaultOptions;
}

bool FDxLibLighting::IsStateCommand( EDrawCommand Type )
{
	switch ( Type )
	{
	case EDrawCommand::UseLighting:
	case EDrawCommand::DefaultLight:
	case EDrawCommand::AmbientLight:
	case EDrawCommand::SurfaceMaterial:
	case EDrawCommand::LightingOptions:
	case EDrawCommand::PushLight:
	case EDrawCommand::PopLight:
		return true;
	default:
		return false;
	}
}

bool FDxLibLighting::Apply( const FDrawCommandView& Command )
{
	switch ( Command.Type )
	{
	case EDrawCommand::UseLighting:
	{
		if ( Command.CanRead<FDrawUseLightingData>() )
		{
			SetUseLighting( Command.Read<FDrawUseLightingData>().Enabled != 0 ? TRUE : FALSE );
			bUseLightingChanged = true;
		}
		return true;
	}
	case EDrawCommand::DefaultLight:
	{
		if ( Command.CanRead<FDrawDefaultLightData>() )
		{
			if ( !bDefaultLightChanged )
			{
				OldDefaultLightEnable = GetLightEnable();
				bDefaultLightChanged = true;
			}
			SetLightEnable( Command.Read<FDrawDefaultLightData>().Enabled != 0 ? TRUE : FALSE );
		}
		return true;
	}
	case EDrawCommand::AmbientLight:
	{
		if ( Command.CanRead<FDrawAmbientLightData>() )
		{
			SetGlobalAmbientLight( ToColor( Command.Read<FDrawAmbientLightData>().Color, 0.0f ) );
			bAmbientLightChanged = true;
		}
		return true;
	}
	case EDrawCommand::SurfaceMaterial:
	{
		if ( Command.CanRead<FDrawSurfaceMaterialData>() )
		{
			ApplyMaterial( Command.Read<FDrawSurfaceMaterialData>() );
			bMaterialChanged = true;
		}
		return true;
	}
	case EDrawCommand::LightingOptions:
	{
		if ( Command.CanRead<FDrawLightingOptionsData>() )
		{
			ApplyOptions( Command.Read<FDrawLightingOptionsData>() );
			bOptionsChanged = true;
		}
		return true;
	}
	case EDrawCommand::PushLight:
	{
		if ( Command.CanRead<FDrawLightData>() )
		{
			// 同時に効くライトの数に空きがなければ、作らない（作っても効かない）。外すときの数を合わせるため、-1 を積む
			int Handle = -1;
			if ( GetEnableLightHandleNum() < NDrawCommand::MaxActiveLights )
			{
				Handle = CreateLight( Command.Read<FDrawLightData>() );
			}
			Lights.Add( Handle );
		}
		return true;
	}
	case EDrawCommand::PopLight:
	{
		if ( !Lights.IsEmpty() )
		{
			const int Handle = Lights.Pop();
			if ( Handle >= 0 )
			{
				DeleteLightHandle( Handle );
			}
		}
		return true;
	}
	default:
		return false;
	}
}

void FDxLibLighting::Restore()
{
	// 外し忘れたライトを、新しいほうから外す
	while ( !Lights.IsEmpty() )
	{
		const int Handle = Lights.Pop();
		if ( Handle >= 0 )
		{
			DeleteLightHandle( Handle );
		}
	}
	if ( bDefaultLightChanged )
	{
		SetLightEnable( OldDefaultLightEnable );
		bDefaultLightChanged = false;
	}
	if ( bUseLightingChanged )
	{
		SetUseLighting( TRUE );
		bUseLightingChanged = false;
	}
	if ( bAmbientLightChanged )
	{
		SetGlobalAmbientLight( GetColorF( 0.0f, 0.0f, 0.0f, 0.0f ) );
		bAmbientLightChanged = false;
	}
	if ( bMaterialChanged )
	{
		ApplyMaterial( NDxLibLighting::GetDefaultMaterial() );
		bMaterialChanged = false;
	}
	if ( bOptionsChanged )
	{
		ApplyOptions( NDxLibLighting::GetDefaultOptions() );
		bOptionsChanged = false;
	}
}
