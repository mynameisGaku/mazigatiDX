// SPDX-License-Identifier: MIT
#include "GeometryForwardMesh.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/Render/Geometry/GeometryCommandHelpers.h"
#include "Library/Platform/Render/Geometry/GeometryQuadBuilder.h"
#include "Library/Platform/Render/Geometry/GeometryShapeBuilder.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"

CGeometryForwardMesh::CGeometryForwardMesh( FGeometryExecution& InRun )
	: Run( InRun )
	, Geometry( Run.Geometry )
	, World( Run.World )
	, Camera( Run.Camera )
	, State( Run.State )
	, Lighting( Run.Lighting )
	, Fog( Run.Fog )
	, HeightFog( Run.HeightFog )
	, TextureTransform( Run.TextureTransform )
	, ShaderSource( Run.ShaderSource )
	, bUseForwardPath( Run.bUseForwardPath )
	, bShadowCasting( Run.bShadowCasting )
	, LightViewProjection( Run.LightViewProjection )
	, ShadowSlotMaps( Run.ShadowSlotMaps )
	, ReceiverSlot( Run.ReceiverSlot )
{
}

void CGeometryForwardMesh::FillLightConstants( float32* Constants, FDrawRgb Bright ) const
{
	// 光・材質・カメラ（b6）。並びは、シェーダーの cbPixelLight と同じ（先頭の 7 つ + ライトごとに 7 つの float4）。
	FLight Active[FLight::MaxActiveLights];
	const int32 NumLights = Lighting.CollectActiveLights( Active );
	const FSurfaceMaterial& Material = Lighting.GetMaterial();
	const FLightingOptions& Options = Lighting.GetOptions();
	const FColor GlobalAmbient = Lighting.GetAmbient();
	const auto Set = [&]( int32 Index, float32 X, float32 Y, float32 Z, float32 W )
	{
		Constants[Index * 4] = X;
		Constants[Index * 4 + 1] = Y;
		Constants[Index * 4 + 2] = Z;
		Constants[Index * 4 + 3] = W;
	};
	Set( 0, Camera.Position.x, Camera.Position.y, Camera.Position.z, static_cast<float32>( NumLights ) );
	Set( 1, Material.Emissive.r + Material.Ambient.r * GlobalAmbient.r, Material.Emissive.g + Material.Ambient.g * GlobalAmbient.g, Material.Emissive.b + Material.Ambient.b * GlobalAmbient.b, 0.0f );
	Set( 2, Material.Diffuse.r, Material.Diffuse.g, Material.Diffuse.b, Material.bUseVertexDiffuse ? 1.0f : 0.0f );
	Set( 3, Material.Specular.r, Material.Specular.g, Material.Specular.b, Material.bUseVertexSpecular ? 1.0f : 0.0f );
	Set( 4, Material.Ambient.r, Material.Ambient.g, Material.Ambient.b, Material.Shininess );
	Set( 5, Options.bUseSpecular ? 1.0f : 0.0f, Options.bWrapLighting ? 1.0f : 0.0f, Options.bIgnoreSurfaceAngle ? 1.0f : 0.0f, 0.0f );
	Set( 6, Bright.R / 255.0f, Bright.G / 255.0f, Bright.B / 255.0f, 1.0f );
	for ( int32 Index = 0; Index < NumLights; ++Index )
	{
		const FLight& Light = Active[Index];
		const FVector3 Direction = Light.Direction.Normalized(); // 向きは、長さを問わない値なので、正規化して渡す。
		const int32 Base = 7 + Index * 7;
		Set( Base, Light.Position.x, Light.Position.y, Light.Position.z, Light.Type == EDrawLightType::Directional ? 0.0f : Light.Type == EDrawLightType::Point ? 1.0f : 2.0f );
		Set( Base + 1, Direction.x, Direction.y, Direction.z, Light.Range );
		Set( Base + 2, Light.ConstantAttenuation, Light.LinearAttenuation, Light.QuadraticAttenuation, NMath::Cos( Light.OuterAngle * 0.5f ) );
		Set( Base + 3, NMath::Cos( Light.InnerAngle * 0.5f ), static_cast<float32>( Light.ShadowMapSlots ), 0.0f, 0.0f );
		Set( Base + 4, Light.Diffuse.r, Light.Diffuse.g, Light.Diffuse.b, 0.0f );
		Set( Base + 5, Light.Specular.r, Light.Specular.g, Light.Specular.b, 0.0f );
		Set( Base + 6, Light.Ambient.r, Light.Ambient.g, Light.Ambient.b, 0.0f );
	}
}

bool CGeometryForwardMesh::CanDrawForward() const
{
	static const bool bEnabledByCommandLine = !NCommandLine::HasFlag( "no-gpu-forward" ); // 比べるための切り替え（--no-gpu-forward で、従来の CPU の経路にする）。
	return bEnabledByCommandLine && ShaderSource != nullptr && Geometry != nullptr
		&& Camera.IsValid() && Geometry->GetWidth() > 0 && Geometry->GetHeight() > 0 && bUseForwardPath;
}

bool CGeometryForwardMesh::CanDrawForward( const FStaticModelDrawView& Model ) const
{
	return CanDrawForward() && Model.MeshKey != 0 && Model.NumVertices <= static_cast<int32>( NGraphicsPipelineLimits::MaxRegisteredVertices ); // 共有メッシュ（登録するメッシュ）の上限。
}

int32 CGeometryForwardMesh::DrawMeshForward( const FStaticModelDrawView& Model, int32 ModelAlpha, bool bAlphaBlend, bool bInferAlpha, FDrawRgb Bright, bool bLit, bool bTransient )
{
	if ( bShadowCasting )
	{
		return DrawShadowDepth( Model, ModelAlpha, bTransient );
	}
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Geometry.ForwardMesh" );
	const FCameraViewport Viewport = { Geometry->GetWidth(), Geometry->GetHeight() };
	const FMatrix4x4 Transform = World * Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ローカルからクリップへの変換。
	FForwardMeshDraw Draw;
	const float32 ShiftX = 2.0f * Camera.ScreenOffset.x / Viewport.Width, ShiftY = 2.0f * Camera.ScreenOffset.y / Viewport.Height; // 画面のずれ（クリップ座標の x・y に w の倍数を足す）。
	FMatrix4x4 Clip = Transform;
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		Clip.m[Row][0] = Transform.m[Row][0] + ShiftX * Transform.m[Row][3];
		Clip.m[Row][1] = Transform.m[Row][1] - ShiftY * Transform.m[Row][3];
	}
	FMatrix4x4 NormalMatrix = World; // 法線は、逆行列の転置で変換する（一様でない拡大でも、面に垂直なまま）。逆行列が無いときは、そのままの変換。
	FMatrix4x4 InverseWorld;
	if ( World.TryInverse( InverseWorld ) )
	{
		NormalMatrix = InverseWorld.Transposed();
	}
	FMemory::Memcpy( Draw.ClipMatrix, Clip.m, sizeof( Draw.ClipMatrix ) );
	FMemory::Memcpy( Draw.World, World.m, sizeof( Draw.World ) );
	FMemory::Memcpy( Draw.NormalMatrix, NormalMatrix.m, sizeof( Draw.NormalMatrix ) );
	static_assert( sizeof( FMatrix4x4 ) == sizeof( Draw.ClipMatrix ), "FMatrix4x4 must be 16 floats" );
	FillLightConstants( Draw.Lighting, Bright ); // 呼び出し側の描画規約に合わせた明るさ。
	if ( TextureTransform.Enabled != 0 )
	{
		Draw.UvTransform[0] = TextureTransform.ScaleU;
		Draw.UvTransform[1] = TextureTransform.ScaleV;
		Draw.UvTransform[2] = TextureTransform.CenterU;
		Draw.UvTransform[3] = TextureTransform.CenterV;
		Draw.UvTransform[4] = TextureTransform.TranslationU;
		Draw.UvTransform[5] = TextureTransform.TranslationV;
		Draw.UvTransform[6] = NMath::Sin( TextureTransform.Rotation );
		Draw.UvTransform[7] = NMath::Cos( TextureTransform.Rotation );
	}
	const FMatrix4x4 FogView = Camera.GetViewMatrix(); // 距離フォグの基準となるカメラの奥行き。
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		Draw.Fog[Row] = FogView.m[Row][2];
	}
	const auto SetFog = [&]( int32 Offset, const FDrawFogData& Value )
	{
		Draw.Fog[Offset] = Value.Start;
		Draw.Fog[Offset + 1] = Value.End;
		Draw.Fog[Offset + 2] = Value.Density;
		Draw.Fog[Offset + 3] = static_cast<float32>( Value.Mode );
		Draw.Fog[Offset + 4] = Value.Color.R / 255.0f;
		Draw.Fog[Offset + 5] = Value.Color.G / 255.0f;
		Draw.Fog[Offset + 6] = Value.Color.B / 255.0f;
	};
	SetFog( 4, Fog );
	SetFog( 12, HeightFog );
	// 影を受ける描画は、影の絵と比べる（従来の CPU の経路と同じく、画素ごとの光とフォグは使わない）。
	FMatrix4x4 ReceiverViewProjection;
	float32 ReceiverAdjust = 0.0f;
	int32 ReceiverWidth = 0, ReceiverHeight = 0;
	const bool bReceive = bLit && Lighting.IsEnabled() && ReceiverSlot >= 0 && ShaderSource->GetShadowPass( ShadowSlotMaps[ReceiverSlot], ReceiverViewProjection, ReceiverAdjust, ReceiverWidth, ReceiverHeight ) && ReceiverWidth > 0 && ReceiverHeight > 0;
	if ( bReceive )
	{
		Draw.ShadowMap = ShadowSlotMaps[ReceiverSlot];
		Draw.ShadowMask = 1 << ReceiverSlot;
		FMemory::Memcpy( Draw.LightViewProjection, ReceiverViewProjection.m, sizeof( Draw.LightViewProjection ) );
		Draw.ShadowParams[0] = static_cast<float32>( ReceiverWidth );
		Draw.ShadowParams[1] = static_cast<float32>( ReceiverHeight );
		Draw.ShadowParams[2] = ReceiverAdjust;
		Draw.Fog[7] = 0.0f; // 距離のフォグの方式（なし）。
		Draw.Fog[15] = 0.0f; // 高さのフォグの方式（なし）。
	}
	// 従来の経路と同じく、フォグ併用時は頂点照明を選ぶ。
	Draw.bPixelLighting = bLit && Lighting.IsEnabled() && Lighting.GetOptions().bPixelLighting && Fog.Mode == 0 && HeightFog.Mode == 0 && !bReceive;
	Draw.bTransient = bTransient;
	Draw.MeshKey = bTransient ? 0 : Model.MeshKey;
	Draw.MeshVersion = Model.MeshVersion;
	Draw.Vertices = Model.Vertices;
	Draw.NumVertices = Model.NumVertices;
	Draw.Indices = Model.Indices;
	Draw.NumIndices = Model.NumIndices;
	Draw.SkinVertices = Model.SkinVertices; // GPU スキニング（無ければ nullptr）
	Draw.BoneMatrices = Model.BoneMatrices;
	Draw.NumBones = Model.NumBones;
	Draw.AlphaScale = static_cast<float32>( ModelAlpha ) / 255.0f;
	Draw.bLit = bLit && Lighting.IsEnabled();
	bool bAnyTranslucent = false; // 頂点のアルファが 1 未満のものがあれば、合成して描く（画像の透明画素と同じ）。
	for ( int32 Index = 0; Index < Model.NumVertices && !bAnyTranslucent; ++Index )
	{
		bAnyTranslucent = Model.Vertices[Index].Diffuse[3] < 255;
	}
	int32 NumDrawn = 0;
	for ( int32 SectionIndex = 0; SectionIndex < Model.NumSections; ++SectionIndex )
	{
		const FStaticModelDrawSection& Section = Model.Sections[SectionIndex];
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumIndices;
		Draw.Texture = Section.Texture;
		Draw.State = State;
		Draw.State.Topology = EGeometryTopology::Triangles;
		Draw.State.bAlphaBlend = bAlphaBlend || ( bInferAlpha && ( ModelAlpha < 255 || Section.Texture >= 0 || bAnyTranslucent ) );
		if ( !ShaderSource->DrawForwardMesh( Draw, *Geometry ) )
		{
			break; // 描けなかった範囲から先は、CPU の経路で描く。
		}
		++NumDrawn;
	}
	return NumDrawn;
}

int32 CGeometryForwardMesh::DrawShadowDepth( const FStaticModelDrawView& Model, int32 ModelAlpha, bool bTransient )
{
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Geometry.ShadowCasterForward" );
	const FMatrix4x4 Clip = World * LightViewProjection; // ローカルから、光のクリップ座標への変換（従来の CPU の経路と同じ。画面のずれは掛けない）。
	FForwardMeshDraw Draw;
	FMemory::Memcpy( Draw.ClipMatrix, Clip.m, sizeof( Draw.ClipMatrix ) );
	FMemory::Memcpy( Draw.World, World.m, sizeof( Draw.World ) );
	Draw.bDepthOnly = true;
	Draw.bTransient = bTransient;
	Draw.MeshKey = bTransient ? 0 : Model.MeshKey;
	Draw.MeshVersion = Model.MeshVersion;
	Draw.Vertices = Model.Vertices;
	Draw.NumVertices = Model.NumVertices;
	Draw.Indices = Model.Indices;
	Draw.NumIndices = Model.NumIndices;
	Draw.SkinVertices = Model.SkinVertices; // GPU スキニング（無ければ nullptr）
	Draw.BoneMatrices = Model.BoneMatrices;
	Draw.NumBones = Model.NumBones;
	Draw.State.Topology = EGeometryTopology::Triangles;
	Draw.State.bTestDepth = true;
	Draw.State.bWriteDepth = true;
	Draw.State.Compare = EGeometryCompare::LessEqual;
	Draw.State.Culling = EGeometryCulling::None; // 裏表に関係なく、影を落とす。
	Draw.State.bAlphaBlend = false;
	int32 NumDrawn = 0;
	for ( int32 SectionIndex = 0; SectionIndex < Model.NumSections; ++SectionIndex )
	{
		const FStaticModelDrawSection& Section = Model.Sections[SectionIndex];
		if ( Section.Texture >= 0 || ModelAlpha < 250 )
		{
			++NumDrawn; // 影を落とさない範囲（画像つき・半透明）。
			continue;
		}
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumIndices;
		if ( !ShaderSource->DrawForwardMesh( Draw, *Geometry ) )
		{
			break; // 描けなかった範囲から先は、CPU の経路で描く。
		}
		++NumDrawn;
	}
	return NumDrawn;
}
