// SPDX-License-Identifier: MIT
#include "VertexLighting.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// DxLib に最初からある 1 つのライト（白い方向ライト。環境光は 0.33）
	FLight MakeBuiltInLight()
	{
		FLight Light = FLight::MakeDirectional( FVector3( 1.0f, -1.0f, 1.0f ) );
		Light.Ambient = FColor( 0.33f, 0.33f, 0.33f );
		return Light;
	}

	// ライト 1 つの分を Out へ足す
	void AddLight( const FLight& Light, const FSurfaceMaterial& Material, const FLightingOptions& Options, const FVector3& Point, const FVector3& Normal, const FVector3& ToEye,
				   const FColor& DiffuseColor, const FColor& SpecularColor, FColor& Out )
	{
		const FVector3 ToLight = Light.Type == EDrawLightType::Directional ? ( Light.Direction * -1.0f ).Normalized() : ( Light.Position - Point ).Normalized();
		const float32 Reach = Light.GetIntensityAt( Point );
		if ( Reach == 0.0f )
		{
			return;
		}
		const float32 Dot = FVector3::Dot( Normal, ToLight );
		float32 Angle = Dot > 0.0f ? Dot : 0.0f;
		if ( Options.bIgnoreSurfaceAngle )
		{
			Angle = 1.0f;
		}
		else if ( Options.bWrapLighting )
		{
			Angle = ( Dot * 0.5f + 0.5f ) * ( Dot * 0.5f + 0.5f );
		}
		float32 Highlight = 0.0f;
		if ( Options.bUseSpecular && Dot > 0.0f )
		{
			const float32 Base = FVector3::Dot( Normal, ( ToLight + ToEye ).Normalized() );
			if ( Base > 0.0f )
			{
				Highlight = NMath::Pow( Base, Material.Shininess );
			}
		}
		Out.r += Reach * ( DiffuseColor.r * Light.Diffuse.r * Angle + Material.Ambient.r * Light.Ambient.r + SpecularColor.r * Light.Specular.r * Highlight );
		Out.g += Reach * ( DiffuseColor.g * Light.Diffuse.g * Angle + Material.Ambient.g * Light.Ambient.g + SpecularColor.g * Light.Specular.g * Highlight );
		Out.b += Reach * ( DiffuseColor.b * Light.Diffuse.b * Angle + Material.Ambient.b * Light.Ambient.b + SpecularColor.b * Light.Specular.b * Highlight );
	}
}

bool CVertexLighting::SetMaterial( const FSurfaceMaterial& InMaterial )
{
	if ( !InMaterial.IsValid() )
	{
		return false;
	}
	Material = InMaterial;
	return true;
}

bool CVertexLighting::PushLight( const FLight& Light )
{
	if ( !Light.IsValid() )
	{
		return false;
	}
	Lights.Add( Light );
	return true;
}

void CVertexLighting::PopLight()
{
	if ( !Lights.IsEmpty() )
	{
		Lights.Pop();
	}
}

FColor CVertexLighting::Evaluate( const FVector3& Point, const FVector3& Normal, const FVector3& Eye, const FColor& VertexDiffuse, const FColor& VertexSpecular, int32 ShadowMask, FColor* OutShadowed ) const
{
	const FColor DiffuseColor = Material.bUseVertexDiffuse ? VertexDiffuse : Material.Diffuse;
	const FColor SpecularColor = Material.bUseVertexSpecular ? VertexSpecular : Material.Specular;
	if ( !bEnabled )
	{
		return FColor( DiffuseColor.r + SpecularColor.r, DiffuseColor.g + SpecularColor.g, DiffuseColor.b + SpecularColor.b, 1.0f );
	}
	FColor Out( Material.Emissive.r + Material.Ambient.r * Ambient.r, Material.Emissive.g + Material.Ambient.g * Ambient.g, Material.Emissive.b + Material.Ambient.b * Ambient.b, 1.0f );
	const FVector3 ToEye = ( Eye - Point ).Normalized();
	int32 NumActive = 0;   // 効いたライトの数（最初からあるライトが先）
	if ( OutShadowed )
	{
		*OutShadowed = FColor( 0.0f, 0.0f, 0.0f, 0.0f );
	}
	// 影を受けるライトの分は、OutShadowed へ。それ以外（と、環境光・自己発光）は、戻り値へ。
	const auto Target = [&]( const FLight& Light ) -> FColor& { return OutShadowed && ( ShadowMask & Light.ShadowMapSlots ) != 0 ? *OutShadowed : Out; };
	if ( bDefaultLight )
	{
		FLight Built = MakeBuiltInLight();
		Built.ShadowMapSlots = DefaultLightShadowSlots;
		AddLight( Built, Material, Options, Point, Normal, ToEye, DiffuseColor, SpecularColor, Target( Built ) );
		++NumActive;
	}
	for ( const FLight& Light : Lights )
	{
		if ( NumActive >= FLight::MaxActiveLights )
		{
			break;
		}
		AddLight( Light, Material, Options, Point, Normal, ToEye, DiffuseColor, SpecularColor, Target( Light ) );
		++NumActive;
	}
	return Out;
}
