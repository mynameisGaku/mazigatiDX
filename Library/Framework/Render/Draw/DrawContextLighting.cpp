#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

namespace
{
	// 色の 3 成分（アルファは使わない）を、命令のデータに入れる
	void SetColor3( float32 ( &Out )[3], const FColor& Color )
	{
		Out[0] = Color.r;
		Out[1] = Color.g;
		Out[2] = Color.b;
	}
}

void FDrawContextLighting::UseLighting( bool bEnabled )
{
	FDrawUseLightingData Data;
	Data.Enabled = bEnabled ? 1 : 0;
	Push( EDrawCommand::UseLighting, 0, Data );
}

void FDrawContextLighting::DefaultLight( bool bEnabled, int32 ShadowMapSlots )
{
	FDrawDefaultLightData Data;
	Data.Enabled = bEnabled ? 1 : 0;
	Data.ShadowMapSlots = ShadowMapSlots & NDrawShadowMap::AllSlots;
	Push( EDrawCommand::DefaultLight, 0, Data );
}

void FDrawContextLighting::LightingOptions( const FLightingOptions& Options )
{
	FDrawLightingOptionsData Data;
	Data.UseSpecular = Options.bUseSpecular ? 1 : 0;
	Data.PixelLighting = Options.bPixelLighting ? 1 : 0;
	Data.WrapLighting = Options.bWrapLighting ? 1 : 0;
	Data.IgnoreSurfaceAngle = Options.bIgnoreSurfaceAngle ? 1 : 0;
	Push( EDrawCommand::LightingOptions, 0, Data );
}

void FDrawContextLighting::PopLight()
{
	const FDrawPopLightData Data;
	Push( EDrawCommand::PopLight, 0, Data );
}

void FDrawContextLighting::EndShadowMap()
{
	const FDrawEndShadowMapData Data;
	Push( EDrawCommand::EndShadowMap, 0, Data );
}

void FDrawContextLighting::AmbientLight( const FColor& Color )
{
	if ( !NMath::IsFinite( Color.r ) || !NMath::IsFinite( Color.g ) || !NMath::IsFinite( Color.b ) )
	{
		return;
	}
	FDrawAmbientLightData Data;
	SetColor3( Data.Color, Color );
	Push( EDrawCommand::AmbientLight, 0, Data );
}

void FDrawContextLighting::SurfaceMaterial( const FSurfaceMaterial& Material )
{
	if ( !Material.IsValid() )
	{
		return;
	}
	FDrawSurfaceMaterialData Data;
	SetColor3( Data.Diffuse, Material.Diffuse );
	SetColor3( Data.Ambient, Material.Ambient );
	SetColor3( Data.Specular, Material.Specular );
	SetColor3( Data.Emissive, Material.Emissive );
	Data.Shininess = Material.Shininess;
	Data.UseVertexDiffuse = Material.bUseVertexDiffuse ? 1 : 0;
	Data.UseVertexSpecular = Material.bUseVertexSpecular ? 1 : 0;
	Push( EDrawCommand::SurfaceMaterial, 0, Data );
}

bool FDrawContextLighting::PushLight( const FLight& Light )
{
	if ( !Light.IsValid() )
	{
		return false;
	}
	FDrawLightData Data;
	Data.Type = static_cast<int32>( Light.Type );
	Set3( Data.Position, Light.Position );
	Set3( Data.Direction, Light.Direction );
	SetColor3( Data.Diffuse, Light.Diffuse );
	SetColor3( Data.Specular, Light.Specular );
	SetColor3( Data.Ambient, Light.Ambient );
	Data.Range = Light.Range;
	Data.ConstantAttenuation = Light.ConstantAttenuation;
	Data.LinearAttenuation = Light.LinearAttenuation;
	Data.QuadraticAttenuation = Light.QuadraticAttenuation;
	Data.OuterAngle = Light.OuterAngle;
	Data.InnerAngle = Light.InnerAngle;
	Data.ShadowMapSlots = Light.ShadowMapSlots;
	Push( EDrawCommand::PushLight, 0, Data );
	return true;
}

bool FDrawContextLighting::BeginShadowMap( FShadowMapHandle ShadowMap, const FShadowMapView& View )
{
	// 使えない値でも記録する（ハンドルを負にして）。記録しないと、影を落とすはずの図形が、画面にそのまま描かれてしまう
	const bool bValid = ShadowMap.IsValid() && View.IsValid();
	FDrawBeginShadowMapData Data;
	if ( bValid )
	{
		Data.Handle = ShadowMap.Value;
		Set3( Data.Direction, View.LightDirection );
		Data.HasArea = View.bFitToCamera ? 0 : 1;
		Set3( Data.AreaMin, View.AreaMin );
		Set3( Data.AreaMax, View.AreaMax );
		Data.AdjustDepth = View.AdjustDepth;
	}
	Push( EDrawCommand::BeginShadowMap, 0, Data );
	return bValid;
}

bool FDrawContextLighting::UseShadowMap( int32 Slot, FShadowMapHandle ShadowMap )
{
	if ( Slot < 0 || Slot >= NDrawShadowMap::NumSlots || !ShadowMap.IsValid() )
	{
		return false;
	}
	FDrawUseShadowMapData Data;
	Data.Slot = Slot;
	Data.Handle = ShadowMap.Value;
	Push( EDrawCommand::UseShadowMap, 0, Data );
	return true;
}

void FDrawContextLighting::StopUsingShadowMap( int32 Slot )
{
	if ( Slot < 0 || Slot >= NDrawShadowMap::NumSlots )
	{
		return;
	}
	FDrawUseShadowMapData Data;
	Data.Slot = Slot;
	Data.Handle = -1;
	Push( EDrawCommand::UseShadowMap, 0, Data );
}

bool FDrawContextLighting::PreviewShadowMap( FShadowMapHandle ShadowMap, int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	if ( !ShadowMap.IsValid() || X1 <= X0 || Y1 <= Y0 )
	{
		return false;
	}
	FDrawPreviewShadowMapData Data;
	Data.Handle = ShadowMap.Value;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Push( EDrawCommand::PreviewShadowMap, 0, Data );
	return true;
}
