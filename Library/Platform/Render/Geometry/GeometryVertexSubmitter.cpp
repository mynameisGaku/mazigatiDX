// SPDX-License-Identifier: MIT
#include "GeometryVertexSubmitter.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/Render/Geometry/GeometryCommandHelpers.h"
#include "Library/Platform/Render/Geometry/GeometryQuadBuilder.h"
#include "Library/Platform/Render/Geometry/GeometryShapeBuilder.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	using NGeometryCommand::Finite;
	using NGeometryCommand::GetFogVisibility;
	using NGeometryCommand::GetHeightFogVisibility;
	using NGeometryCommand::ToByte;
}

CGeometryVertexSubmitter::CGeometryVertexSubmitter( FGeometryExecution& InRun, CGeometryForwardMesh& InForward )
	: Run( InRun )
	, Forward( InForward )
	, Textures( Run.Textures )
	, Geometry( Run.Geometry )
	, World( Run.World )
	, Camera( Run.Camera )
	, State( Run.State )
	, Lighting( Run.Lighting )
	, Fog( Run.Fog )
	, HeightFog( Run.HeightFog )
	, TextureTransform( Run.TextureTransform )
	, ShaderSource( Run.ShaderSource )
	, bShadowCasting( Run.bShadowCasting )
	, LightViewProjection( Run.LightViewProjection )
	, ShadowSlotMaps( Run.ShadowSlotMaps )
	, ReceiverSlot( Run.ReceiverSlot )
{
}

bool CGeometryVertexSubmitter::Submit( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, int32 Texture, bool bLit, bool bAllowTransient )
{
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Geometry.Submit" );
	if ( !Geometry || Vertices.IsEmpty() || Vertices.Num() > 65536 || Geometry->GetWidth() <= 0 || Geometry->GetHeight() <= 0 || !Camera.IsValid() )
	{
		return false;
	}
	FTextureDrawView Image; // 全頂点の変換前に借用画像の有効性を固定する。
	if ( Texture >= 0 && ( !Textures || !Textures->GetTextureDrawView( Texture, Image ) || Image.Width <= 0 || Image.Height <= 0 || Image.Binding == 0 ) )
	{
		return false;
	}
	if ( bShadowCasting )
	{
		return SubmitShadowCaster( Vertices, Topology, Alpha, Texture, bAllowTransient ); // 影の絵へ、光から見た深度として描く。
	}
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Geometry.SubmitMain" );
	if ( bAllowTransient && Topology == EGeometryTopology::Triangles && Vertices.Num() % 3 == 0 )
	{
		FStaticModelDrawSection Section = { 0, Vertices.Num(), Texture }; // 一度だけ描く三角形の範囲。
		FStaticModelDrawView Mesh = { Vertices.GetData(), Vertices.Num(), nullptr, Vertices.Num(), &Section, 1, 1.0f, 1, 1 }; // 鍵は判定用。実行先には登録しない。
		bool bFinite = true; // GPUへ渡す前に全頂点を検査する。
		for ( const auto& Vertex : Vertices )
		{
			bFinite = bFinite && Finite( Vertex.Position ) && Finite( Vertex.Normal ) && NMath::IsFinite( Vertex.U ) && NMath::IsFinite( Vertex.V );
		}
		if ( bFinite && Forward.CanDrawForward( Mesh ) && Forward.DrawMeshForward( Mesh, Alpha, bAlphaBlend, false, Bright, bLit, true ) == 1 )
		{
			return true;
		}
	}
	// 影を受ける三角形: 影の外の色と影の中で消える色を、頂点ごとに計算し、画素ごとに影の絵と比べる（組み込みのシェーダー）。
	FMatrix4x4 ReceiverViewProjection;
	float32 ReceiverAdjust = 0.0f;
	int32 ReceiverWidth = 0, ReceiverHeight = 0;
	const bool bReceive = bLit && Topology == EGeometryTopology::Triangles && ReceiverSlot >= 0 && ShaderSource
		&& ShaderSource->GetShadowPass( ShadowSlotMaps[ReceiverSlot], ReceiverViewProjection, ReceiverAdjust, ReceiverWidth, ReceiverHeight );
	TArray<FDrawShaderVertex3D> ShadowVertices; // 影を受ける描画の頂点。
	const FCameraViewport Viewport = { Geometry->GetWidth(), Geometry->GetHeight() }; // 同じ論理画面で射影する。
	const FMatrix4x4 Transform = World * Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ローカルからクリップへの変換。
	FMatrix4x4 NormalMatrix = World; // 法線は、逆行列の転置で変換する（一様でない拡大でも、面に垂直なまま）。
	FMatrix4x4 InverseWorld; // 逆行列が無い（つぶれた変換）ときは、そのままの変換を使う。
	if ( bLit && World.TryInverse( InverseWorld ) )
	{
		NormalMatrix = InverseWorld.Transposed();
	}
	const bool bAnyFog = Fog.Mode != 0 || HeightFog.Mode != 0; // 距離か高さのフォグがあるか。
	const bool bFog = bAnyFog && Texture < 0; // 頂点の色へ混ぜる（画像つきは、画像の色を掛けたあとに混ぜるので、下のシェーダーで混ぜる）。
	const bool bFogTextured = bAnyFog && Texture >= 0 && !bReceive && Topology == EGeometryTopology::Triangles && ShaderSource != nullptr; // 画像つきの三角形のフォグ。
	TArray<FDrawShaderVertex3D> FogVertices; // 画像つきのフォグの頂点。
	const bool bPixelLit = bLit && Lighting.IsEnabled() && Lighting.GetOptions().bPixelLighting && !bReceive && !bAnyFog && Topology == EGeometryTopology::Triangles && ShaderSource != nullptr; // 画素ごとの光（フォグと重なるときは、頂点ごと）。
	TArray<FDrawShaderVertex3D> PixelLitVertices; // 画素ごとの光の頂点。
	const FMatrix4x4 ViewMatrix = Camera.GetViewMatrix(); // フォグの距離（カメラから見た奥行き）に使う。
	TArray<FGeometryVertex> Output; // 全頂点が正しい場合だけ送る所有コピー。
	Output.Reserve( Vertices.Num() );
	for ( const auto& Original : Vertices )
	{
		FDrawVertex3D Input = Original; // 画像の変換を掛けた頂点。
		if ( TextureTransform.Enabled != 0 && NMath::IsFinite( Input.U ) && NMath::IsFinite( Input.V ) )
		{
			// 中心を軸に、拡大・回転してから、移動する（UV に足す）。
			const float32 CenteredU = ( Input.U - TextureTransform.CenterU ) * TextureTransform.ScaleU, CenteredV = ( Input.V - TextureTransform.CenterV ) * TextureTransform.ScaleV;
			const float32 Cosine = NMath::Cos( TextureTransform.Rotation ), Sine = NMath::Sin( TextureTransform.Rotation );
			Input.U = CenteredU * Cosine - CenteredV * Sine + TextureTransform.CenterU + TextureTransform.TranslationU;
			Input.V = CenteredU * Sine + CenteredV * Cosine + TextureTransform.CenterV + TextureTransform.TranslationV;
		}
		if ( !Finite( Input.Position ) || !NMath::IsFinite( Input.U ) || !NMath::IsFinite( Input.V ) )
		{
			return false;
		}
		FVector4 Clip = Transform.Transform( FVector4( Input.Position[0], Input.Position[1], Input.Position[2], 1 ) ); // 透視除算しない。
		Clip.x += 2 * Camera.ScreenOffset.x / Viewport.Width * Clip.w;
		Clip.y -= 2 * Camera.ScreenOffset.y / Viewport.Height * Clip.w;
		FGeometryVertex Value; // GPUが頂点色を透視補間する。
		Value.Position[0] = Clip.x;
		Value.Position[1] = Clip.y;
		Value.Position[2] = Clip.z;
		Value.Position[3] = Clip.w;
		if ( !Finite( Value.Position ) )
		{
			return false;
		}
		if ( bReceive )
		{
			if ( !Finite( Input.Normal ) )
			{
				return false;
			}
			const FVector3 Point = World.TransformPoint( FVector3( Input.Position[0], Input.Position[1], Input.Position[2] ) ); // 光を受ける世界の点。
			FVector3 Normal = NormalMatrix.TransformVector( FVector3( Input.Normal[0], Input.Normal[1], Input.Normal[2] ) ); // 世界での面の向き。
			const float32 NormalLength = Normal.Length();
			Normal = NormalLength > NMath::SmallNumber ? Normal / NormalLength : FVector3::Up;
			FColor Shadowed; // 影の中で消える分。
			const FColor Lit = Lighting.Evaluate( Point, Normal, Camera.Position, FColor( Input.Diffuse[0] / 255.0f, Input.Diffuse[1] / 255.0f, Input.Diffuse[2] / 255.0f, 1.0f ),
				FColor( Input.Specular[0] / 255.0f, Input.Specular[1] / 255.0f, Input.Specular[2] / 255.0f, 1.0f ), 1 << ReceiverSlot, &Shadowed );
			const FVector4 LightClip = ReceiverViewProjection.Transform( FVector4( Point.x, Point.y, Point.z, 1.0f ) ); // 光から見た位置。
			FDrawShaderVertex3D Vertex;
			FMemory::Memzero( &Vertex, sizeof( Vertex ) );
			FMemory::Memcpy( Vertex.Position, Input.Position, sizeof( Vertex.Position ) );
			Vertex.SubPosition[0] = Value.Position[0];
			Vertex.SubPosition[1] = Value.Position[1];
			Vertex.SubPosition[2] = Value.Position[2];
			Vertex.SubPosition[3] = Value.Position[3];
			Vertex.Normal[0] = LightClip.x;
			Vertex.Normal[1] = LightClip.y;
			Vertex.Normal[2] = LightClip.z;
			Vertex.Diffuse[0] = static_cast<uint8>( ToByte( Lit.r ) * Bright.R / 255 );
			Vertex.Diffuse[1] = static_cast<uint8>( ToByte( Lit.g ) * Bright.G / 255 );
			Vertex.Diffuse[2] = static_cast<uint8>( ToByte( Lit.b ) * Bright.B / 255 );
			Vertex.Diffuse[3] = static_cast<uint8>( Input.Diffuse[3] * Alpha / 255 );
			Vertex.Specular[0] = static_cast<uint8>( ToByte( Shadowed.r ) * Bright.R / 255 );
			Vertex.Specular[1] = static_cast<uint8>( ToByte( Shadowed.g ) * Bright.G / 255 );
			Vertex.Specular[2] = static_cast<uint8>( ToByte( Shadowed.b ) * Bright.B / 255 );
			Vertex.U = Input.U;
			Vertex.V = Input.V;
			ShadowVertices.Add( Vertex );
			continue;
		}
		if ( bLit )
		{
			if ( !Finite( Input.Normal ) )
			{
				return false;
			}
			const FVector3 Point = World.TransformPoint( FVector3( Input.Position[0], Input.Position[1], Input.Position[2] ) ); // 光を受ける世界の点。
			FVector3 Normal = NormalMatrix.TransformVector( FVector3( Input.Normal[0], Input.Normal[1], Input.Normal[2] ) ); // 世界での面の向き。
			const float32 NormalLength = Normal.Length(); // 長さ0の法線は、上向きとして扱う。
			Normal = NormalLength > NMath::SmallNumber ? Normal / NormalLength : FVector3::Up;
			if ( bPixelLit )
			{
				FDrawShaderVertex3D Vertex; // 世界の位置と法線を渡して、光を画素ごとに計算する。
				FMemory::Memzero( &Vertex, sizeof( Vertex ) );
				Vertex.Position[0] = Point.x;
				Vertex.Position[1] = Point.y;
				Vertex.Position[2] = Point.z;
				Vertex.SubPosition[0] = Value.Position[0];
				Vertex.SubPosition[1] = Value.Position[1];
				Vertex.SubPosition[2] = Value.Position[2];
				Vertex.SubPosition[3] = Value.Position[3];
				Vertex.Normal[0] = Normal.x;
				Vertex.Normal[1] = Normal.y;
				Vertex.Normal[2] = Normal.z;
				for ( int32 Channel = 0; Channel < 3; ++Channel )
				{
					Vertex.Diffuse[Channel] = Input.Diffuse[Channel];
					Vertex.Specular[Channel] = Input.Specular[Channel];
				}
				Vertex.Diffuse[3] = static_cast<uint8>( Input.Diffuse[3] * Alpha / 255 );
				Vertex.U = Input.U;
				Vertex.V = Input.V;
				PixelLitVertices.Add( Vertex );
				continue;
			}
			const FColor Lit = Lighting.Evaluate( Point, Normal, Camera.Position, FColor( Input.Diffuse[0] / 255.0f, Input.Diffuse[1] / 255.0f, Input.Diffuse[2] / 255.0f, 1.0f ),
				FColor( Input.Specular[0] / 255.0f, Input.Specular[1] / 255.0f, Input.Specular[2] / 255.0f, 1.0f ) ); // 頂点ごとの光の計算。
			Value.Color = FColor32( static_cast<uint8>( ToByte( Lit.r ) * Bright.R / 255 ), static_cast<uint8>( ToByte( Lit.g ) * Bright.G / 255 ),
				static_cast<uint8>( ToByte( Lit.b ) * Bright.B / 255 ), static_cast<uint8>( Input.Diffuse[3] * Alpha / 255 ) );
		}
		else
		{
			Value.Color = FColor32( static_cast<uint8>( NMath::Min( 255, Input.Diffuse[0] + Input.Specular[0] ) * Bright.R / 255 ),
				static_cast<uint8>( NMath::Min( 255, Input.Diffuse[1] + Input.Specular[1] ) * Bright.G / 255 ),
				static_cast<uint8>( NMath::Min( 255, Input.Diffuse[2] + Input.Specular[2] ) * Bright.B / 255 ), static_cast<uint8>( Input.Diffuse[3] * Alpha / 255 ) );
		}
		if ( bFog || bFogTextured )
		{
			const FVector3 WorldPoint = World.TransformPoint( FVector3( Input.Position[0], Input.Position[1], Input.Position[2] ) ); // 世界の点。
			const float32 VisibilityDistance = Fog.Mode != 0 ? GetFogVisibility( Fog, NMath::Max( 0.0f, ViewMatrix.TransformPoint( WorldPoint ).z ) ) : 1.0f; // 距離のフォグに隠れない割合。
			const float32 VisibilityHeight = HeightFog.Mode != 0 ? GetHeightFogVisibility( HeightFog, WorldPoint.y ) : 1.0f; // 高さのフォグに隠れない割合。
			if ( bFog )
			{
				const auto Mix = [&]( uint8 Channel, uint8 FogChannel, float32 Visibility ) { return static_cast<uint8>( FogChannel + ( Channel - FogChannel ) * Visibility + 0.5f ); }; // フォグの色へ寄せる。
				if ( Fog.Mode != 0 )
				{
					Value.Color = FColor32( Mix( Value.Color.r, Fog.Color.R, VisibilityDistance ), Mix( Value.Color.g, Fog.Color.G, VisibilityDistance ), Mix( Value.Color.b, Fog.Color.B, VisibilityDistance ), Value.Color.a );
				}
				if ( HeightFog.Mode != 0 )
				{
					Value.Color = FColor32( Mix( Value.Color.r, HeightFog.Color.R, VisibilityHeight ), Mix( Value.Color.g, HeightFog.Color.G, VisibilityHeight ), Mix( Value.Color.b, HeightFog.Color.B, VisibilityHeight ), Value.Color.a );
				}
			}
			else
			{
				// 画像つき: 距離 → 高さの順に混ぜたのと同じ結果を、1つの色と1つの割合で表す。
				const float32 Total = VisibilityDistance * VisibilityHeight; // 全体で隠れない割合。
				const float32 Weight = 1.0f - Total; // フォグの色の割合。
				const float32 DistanceWeight = Weight > 1.0e-6f ? VisibilityHeight * ( 1.0f - VisibilityDistance ) / Weight : 0.0f; // フォグの色のうち、距離のフォグの分。
				const float32 HeightWeight = Weight > 1.0e-6f ? ( 1.0f - VisibilityHeight ) / Weight : 0.0f;
				FDrawShaderVertex3D FogVertex;
				FMemory::Memzero( &FogVertex, sizeof( FogVertex ) );
				FMemory::Memcpy( FogVertex.Position, Input.Position, sizeof( FogVertex.Position ) );
				FogVertex.SubPosition[0] = Value.Position[0];
				FogVertex.SubPosition[1] = Value.Position[1];
				FogVertex.SubPosition[2] = Value.Position[2];
				FogVertex.SubPosition[3] = Value.Position[3];
				FogVertex.Diffuse[0] = Value.Color.r;
				FogVertex.Diffuse[1] = Value.Color.g;
				FogVertex.Diffuse[2] = Value.Color.b;
				FogVertex.Diffuse[3] = Value.Color.a;
				FogVertex.Specular[0] = static_cast<uint8>( NMath::Clamp( DistanceWeight * Fog.Color.R + HeightWeight * HeightFog.Color.R + 0.5f, 0.0f, 255.0f ) );
				FogVertex.Specular[1] = static_cast<uint8>( NMath::Clamp( DistanceWeight * Fog.Color.G + HeightWeight * HeightFog.Color.G + 0.5f, 0.0f, 255.0f ) );
				FogVertex.Specular[2] = static_cast<uint8>( NMath::Clamp( DistanceWeight * Fog.Color.B + HeightWeight * HeightFog.Color.B + 0.5f, 0.0f, 255.0f ) );
				FogVertex.Specular[3] = static_cast<uint8>( NMath::Clamp( Total * 255.0f + 0.5f, 0.0f, 255.0f ) );
				FogVertex.U = Input.U;
				FogVertex.V = Input.V;
				FogVertices.Add( FogVertex );
				continue;
			}
		}
		Value.U = Input.U;
		Value.V = Input.V;
		Output.Add( Value );
	}
	State.Topology = Topology;
	State.bAlphaBlend = bAlphaBlend;
	if ( bPixelLit )
	{
		float32 Constants[( 7 + FLight::MaxActiveLights * 7 ) * 4] = {};
		Forward.FillLightConstants( Constants, Bright );
		FShaderConstantData Constant;
		Constant.Stage = static_cast<int32>( EShaderStage::Pixel );
		Constant.Slot = 6;
		Constant.Bytes = TArrayView<const uint8>( reinterpret_cast<const uint8*>( Constants ), sizeof( Constants ) );
		FShaderDrawRequest Request;
		Request.Builtin = EBuiltInVertexShader::PixelLit;
		Request.BuiltinPixel = Texture >= 0 ? EBuiltInPixelShader::PixelLitTextured : EBuiltInPixelShader::PixelLit;
		Request.State = State;
		Request.Topology = EGeometryTopology::Triangles;
		Request.bUseDepth = true;
		Request.Vertices = reinterpret_cast<const uint8*>( PixelLitVertices.GetData() );
		Request.NumVertices = PixelLitVertices.Num();
		Request.Constants = TArrayView<const FShaderConstantData>( &Constant, 1 );
		Request.Textures[0] = Texture;
		return ShaderSource->DrawShader( Request, *Geometry );
	}
	if ( bFogTextured )
	{
		FShaderDrawRequest FogRequest; // 画像つきの三角形を、フォグへ混ぜて描く。
		FogRequest.Builtin = EBuiltInVertexShader::ShadowLit;
		FogRequest.BuiltinPixel = EBuiltInPixelShader::FogTextured;
		FogRequest.State = State;
		FogRequest.Topology = EGeometryTopology::Triangles;
		FogRequest.bUseDepth = true;
		FogRequest.Vertices = reinterpret_cast<const uint8*>( FogVertices.GetData() );
		FogRequest.NumVertices = FogVertices.Num();
		FogRequest.Textures[0] = Texture;
		return ShaderSource->DrawShader( FogRequest, *Geometry );
	}
	if ( bReceive )
	{
		const float32 Constants[4] = { static_cast<float32>( ReceiverWidth ), static_cast<float32>( ReceiverHeight ), ReceiverAdjust, 0.0f }; // 影の絵の大きさと、補正の深さ。
		FShaderConstantData Constant;
		Constant.Stage = static_cast<int32>( EShaderStage::Pixel );
		Constant.Slot = 4;
		Constant.Bytes = TArrayView<const uint8>( reinterpret_cast<const uint8*>( Constants ), sizeof( Constants ) );
		FShaderDrawRequest Request;
		Request.Builtin = EBuiltInVertexShader::ShadowLit;
		Request.BuiltinPixel = Texture >= 0 ? EBuiltInPixelShader::ShadowLitTextured : EBuiltInPixelShader::ShadowLit;
		Request.State = State;
		Request.Topology = EGeometryTopology::Triangles;
		Request.bUseDepth = true;
		Request.Vertices = reinterpret_cast<const uint8*>( ShadowVertices.GetData() );
		Request.NumVertices = ShadowVertices.Num();
		Request.Constants = TArrayView<const FShaderConstantData>( &Constant, 1 );
		Request.Textures[0] = Texture;
		Request.Textures[1] = ShadowSlotMaps[ReceiverSlot];
		return ShaderSource->DrawShader( Request, *Geometry );
	}
	return Texture >= 0 ? Geometry->DrawTexturedGeometry( Output.GetData(), Output.Num(), State, Image ) : Geometry->DrawGeometry( Output.GetData(), Output.Num(), State );
}

bool CGeometryVertexSubmitter::SubmitShadowCaster( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, int32 Alpha, int32 Texture, bool bAllowTransient )
{
	PROFILE_SCOPE_OWNER( NProfileCategory::Platform, "Draw", "Geometry.ShadowCaster" );
	if ( Topology != EGeometryTopology::Triangles || Texture >= 0 || Alpha < 250 )
	{
		return true; // 影を落とさない図形。
	}
	if ( !ShaderSource || !Geometry )
	{
		return false;
	}
	if ( bAllowTransient && Vertices.Num() % 3 == 0 )
	{
		// GPU が光のクリップ座標への変換をして、深度を書く（頂点は、フレームの転送領域へ）。描けなければ、下の CPU の経路で描く。
		FStaticModelDrawSection Section = { 0, Vertices.Num(), -1 }; // 一度だけ描く三角形の範囲。
		FStaticModelDrawView Mesh = { Vertices.GetData(), Vertices.Num(), nullptr, Vertices.Num(), &Section, 1, 1.0f, 1, 1 }; // 鍵は判定用。実行先には登録しない。
		bool bFinite = true; // GPUへ渡す前に全頂点を検査する。
		for ( const FDrawVertex3D& Vertex : Vertices )
		{
			bFinite = bFinite && Finite( Vertex.Position );
		}
		if ( bFinite && Forward.CanDrawForward( Mesh ) && Forward.DrawMeshForward( Mesh, Alpha, false, false, FDrawRgb(), false, true ) == 1 )
		{
			return true;
		}
	}
	const FMatrix4x4 Transform = World * LightViewProjection; // ローカルから、光のクリップ座標への変換。
	TArray<FDrawShaderVertex3D> Output;
	Output.Reserve( Vertices.Num() );
	for ( const FDrawVertex3D& Input : Vertices )
	{
		if ( !Finite( Input.Position ) )
		{
			return false;
		}
		const FVector4 Clip = Transform.Transform( FVector4( Input.Position[0], Input.Position[1], Input.Position[2], 1 ) );
		FDrawShaderVertex3D Vertex;
		FMemory::Memzero( &Vertex, sizeof( Vertex ) );
		Vertex.SubPosition[0] = Clip.x;
		Vertex.SubPosition[1] = Clip.y;
		Vertex.SubPosition[2] = Clip.z;
		Vertex.SubPosition[3] = Clip.w;
		if ( !Finite( Vertex.SubPosition ) )
		{
			return false;
		}
		Output.Add( Vertex );
	}
	FShaderDrawRequest Request;
	Request.Builtin = EBuiltInVertexShader::Quad3D;
	Request.BuiltinPixel = EBuiltInPixelShader::ShadowDepth;
	Request.State.bTestDepth = true;
	Request.State.bWriteDepth = true;
	Request.State.Compare = EGeometryCompare::LessEqual;
	Request.State.Culling = EGeometryCulling::None; // 裏表に関係なく、影を落とす。
	Request.State.bAlphaBlend = false;
	Request.Topology = EGeometryTopology::Triangles;
	Request.bUseDepth = true;
	Request.TargetFormats[0] = ERenderTargetColorFormat::R32Float;
	Request.Vertices = reinterpret_cast<const uint8*>( Output.GetData() );
	Request.NumVertices = Output.Num();
	return ShaderSource->DrawShader( Request, *Geometry );
}

bool CGeometryVertexSubmitter::SubmitPrimitive( const TArray<FDrawVertex3D>& Ordered, EDrawPrimitive Type, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	const int32 Count = Ordered.Num(); // 展開前の点数。
	if ( Count > 65536 || !NDrawPrimitive::IsValidCount( Type, Count ) )
	{
		return false;
	}
	TArray<FDrawVertex3D> Output; // リストへ展開した点列。
	EGeometryTopology Topology = EGeometryTopology::Triangles; // 出力のつなぎ方。
	if ( Type == EDrawPrimitive::Points || Type == EDrawPrimitive::Lines || Type == EDrawPrimitive::Triangles )
	{
		Topology = Type == EDrawPrimitive::Points ? EGeometryTopology::Points : Type == EDrawPrimitive::Lines ? EGeometryTopology::Lines : EGeometryTopology::Triangles;
		return Submit( Ordered, Topology, Bright, Alpha, bAlphaBlend, Texture, Topology == EGeometryTopology::Triangles );
	}
	else if ( Type == EDrawPrimitive::LineStrip )
	{
		Topology = EGeometryTopology::Lines;
		if ( Count > 32769 )
		{
			return false;
		}
		for ( int32 Index = 1; Index < Count; ++Index )
		{
			Output.Add( Ordered[Index - 1] );
			Output.Add( Ordered[Index] );
		}
	}
	else
	{
		if ( Count > 21847 )
		{
			return false;
		}
		for ( int32 Index = 2; Index < Count; ++Index )
		{
			Output.Add( Ordered[Type == EDrawPrimitive::TriangleFan ? 0 : Index - ( Index % 2 ? 1 : 2 )] );
			Output.Add( Ordered[Type == EDrawPrimitive::TriangleFan ? Index - 1 : Index - ( Index % 2 ? 2 : 1 )] );
			Output.Add( Ordered[Index] );
		}
	}
	return Submit( Output, Topology, Bright, Alpha, bAlphaBlend, Texture, Topology == EGeometryTopology::Triangles );
}

bool CGeometryVertexSubmitter::SubmitQuad( const TArray<FDrawVertex3D>& Vertices, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bLit )
{
	FTextureDrawView Image; // 大きさ0でも無効な画像の要求を見逃さない。
	if ( Texture < 0 || !Textures || !Textures->GetTextureDrawView( Texture, Image ) || Image.Width <= 0 || Image.Height <= 0 || Image.Binding == 0 )
	{
		return false;
	}
	if ( Vertices.IsEmpty() )
	{
		return true;
	}
	const EGeometryCulling OldCulling = State.Culling; // 成功・失敗のどちらでも元に戻す。
	State.Culling = EGeometryCulling::None;
	const bool bSuccess = Submit( Vertices, EGeometryTopology::Triangles, Bright, Alpha, true, Texture, bLit ); // 画像と頂点のAlphaを通常合成する。
	State.Culling = OldCulling;
	return bSuccess;
}
