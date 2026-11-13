// SPDX-License-Identifier: MIT
#include "MaterialShadowMap.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Material/MaterialHlsl.h"
#include "Library/Framework/Material/MaterialSceneConstants.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Math/MathUtils.h"

TSharedPtr<CMaterialShadowMap> CMaterialShadowMap::Create( CAssetSubsystem& Assets, const FString& Name, int32 Size )
{
	FRenderTargetDesc Desc;
	Desc.Width = Size;
	Desc.Height = Size;
	Desc.bDepth = true;
	Desc.Format = ERenderTargetFormat::Float32;
	Desc.Channels = 1;
	if ( !Desc.IsValid() )
	{
		return TSharedPtr<CMaterialShadowMap>();
	}
	TSharedPtr<CMaterialShadowMap> Map( new CMaterialShadowMap() );
	Map->Target = Assets.CreateRenderTarget( Name, Desc );
	Map->Vertex = CShader::Compile( EShaderStage::Vertex, NMaterialHlsl::GetSurfaceVertexSource(), "main", nullptr );
	Map->Pixel = CShader::Compile( EShaderStage::Pixel, NMaterialHlsl::GetSurfaceDepthPixelSource(), "main", nullptr );
	if ( !Map->Target || !Map->Vertex || !Map->Pixel )
	{
		return TSharedPtr<CMaterialShadowMap>();
	}
	Map->Size = Size;
	Map->SetLight( FVector3( -0.4969f, -0.7951f, -0.3478f ), FVector3( 0.0f, 0.0f, 0.0f ), 10.0f );
	return Map;
}

void CMaterialShadowMap::SetLight( const FVector3& LightDirection, const FVector3& Center, float32 InRadius )
{
	const float32 LengthSquared = FVector3::Dot( LightDirection, LightDirection );
	if ( LengthSquared <= 1.0e-12f || InRadius <= 0.0f )
	{
		return;
	}
	const FVector3 Direction = LightDirection * ( 1.0f / NMath::Sqrt( LengthSquared ) );
	Radius = InRadius;
	// 光のカメラ: 範囲の球を、ちょうど収める正射影。範囲の手前（光の側）にある物も影を落とすように、球の 2 倍手前から見る
	const FVector3 Eye = Center - Direction * ( Radius * 2.0f );
	const FVector3 Up = NMath::Abs( Direction.y ) > 0.99f ? FVector3( 0.0f, 0.0f, 1.0f ) : FVector3( 0.0f, 1.0f, 0.0f );
	ViewProjection = FMatrix4x4::LookAt( Eye, Center, Up ) * FMatrix4x4::Orthographic( Radius * 2.0f, Radius * 2.0f, 0.0f, Radius * 4.0f );
}

bool CMaterialShadowMap::Begin( FDrawContext& Draw )
{
	if ( !Target || !Vertex || !Pixel || !Draw.PushTarget( Target ) )
	{
		return false;
	}
	// 1 = いちばん遠い（何も無い所は、影を落とさない）
	Draw.Clear( FColor32( 255, 255, 255, 255 ) );
	Draw.ClearDepth();
	FDepthState DepthState;
	DepthState.bTestDepth = true;
	DepthState.bWriteDepth = true;
	Draw.Depth( DepthState );
	Draw.Blend( EDrawBlend::Opaque );
	FMaterialSceneConstants Constants;
	Constants.ViewProjection = ViewProjection;
	Draw.ShaderConstants( EShaderStage::Vertex, 4, Constants );
	Draw.ShaderConstants( EShaderStage::Pixel, 4, Constants );
	Draw.UseVertexShader( Vertex );
	Draw.UsePixelShader( Pixel );
	return true;
}

bool CMaterialShadowMap::DrawMesh( FDrawContext& Draw, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World )
{
	FMaterialObjectConstants Object;
	Object.World = World;
	Draw.ShaderConstants( EShaderStage::Vertex, 5, Object );
	Draw.ShaderConstants( EShaderStage::Pixel, 5, Object );
	return Draw.PrimitiveIndexedShader3D( Vertices, NumVertices, Indices, NumIndices, EDrawPrimitive::Triangles );
}

void CMaterialShadowMap::End( FDrawContext& Draw )
{
	Draw.StopUsingVertexShader();
	Draw.StopUsingPixelShader();
	Draw.PopTarget();
}
