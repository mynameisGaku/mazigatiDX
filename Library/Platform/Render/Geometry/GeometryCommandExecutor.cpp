// SPDX-License-Identifier: MIT
#include "GeometryCommandExecutor.h"
#include "GeometryShapeBuilder.h"
#include "GeometryQuadBuilder.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	// ワールド座標や行列に非有限値がないか。
	template<usize Count>
	bool Finite( const float32 ( &Values )[Count] )
	{
		for ( float32 Value : Values )
		{
			if ( !NMath::IsFinite( Value ) )
			{
				return false;
			}
		}
		return true;
	}
	// 共通の12要素形式からアフィン行列へ戻す。
	FMatrix4x4 ToMatrix( const FDrawMatrixData& Data )
	{
		FMatrix4x4 Result; // 最終列は(0,0,0,1)。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 3; ++Column )
			{
				Result.m[Row][Column] = Data.M[Row * 3 + Column];
			}
		}
		return Result;
	}
	// 図形の座標と色を共通頂点へ移す。
	FDrawVertex3D Vertex( const float32 ( &Position )[3], FDrawRgb Color )
	{
		FDrawVertex3D Result; // 法線と画像座標は使わない。
		FMemory::Memcpy( Result.Position, Position, sizeof( Position ) );
		Result.Diffuse[0] = Color.R;
		Result.Diffuse[1] = Color.G;
		Result.Diffuse[2] = Color.B;
		return Result;
	}
	// 図形の鏡面色を、全頂点へ付ける（光の計算で使う）。
	void SetSpecular( TArray<FDrawVertex3D>& Vertices, FDrawRgb Specular )
	{
		for ( FDrawVertex3D& Value : Vertices )
		{
			Value.Specular[0] = Specular.R;
			Value.Specular[1] = Specular.G;
			Value.Specular[2] = Specular.B;
		}
	}
	// 3成分の色の命令の値を、色にする。
	FColor ToColor( const float32 ( &Values )[3] )
	{
		return FColor( Values[0], Values[1], Values[2], 1.0f );
	}
	// 距離 Depth（カメラから見た奥行き）での、フォグに隠れない割合（1 = 隠れない、0 = 完全にフォグの色）。
	float32 GetFogVisibility( const FDrawFogData& Fog, float32 Depth )
	{
		float32 Visibility = 1.0f; // 通常の（フォグなしの）見え方。
		switch ( static_cast<EDrawFogMode>( Fog.Mode ) )
		{
		case EDrawFogMode::Exponential:
			Visibility = NMath::Exp( -Fog.Density * Depth );
			break;
		case EDrawFogMode::ExponentialSquared:
			Visibility = NMath::Exp( -( Fog.Density * Depth ) * ( Fog.Density * Depth ) );
			break;
		case EDrawFogMode::Linear:
			Visibility = Fog.End > Fog.Start ? ( Fog.End - Depth ) / ( Fog.End - Fog.Start ) : ( Depth < Fog.End ? 1.0f : 0.0f );
			break;
		default:
			break;
		}
		return NMath::IsFinite( Visibility ) ? NMath::Clamp( Visibility, 0.0f, 1.0f ) : 1.0f;
	}
	// 高さ Height（ワールドの Y）での、フォグに隠れない割合。Exponential / ExponentialSquared は、Start より下だけ、低いほど濃い。Linear は、Start で 0、End で 1 の濃さ。
	float32 GetHeightFogVisibility( const FDrawFogData& Fog, float32 Height )
	{
		float32 Visibility = 1.0f; // 通常の（フォグなしの）見え方。
		const float32 Below = NMath::Max( 0.0f, Fog.Start - Height ); // Start より下の深さ。
		switch ( static_cast<EDrawFogMode>( Fog.Mode ) )
		{
		case EDrawFogMode::Exponential:
			Visibility = NMath::Exp( -Fog.Density * Below );
			break;
		case EDrawFogMode::ExponentialSquared:
			Visibility = NMath::Exp( -( Fog.Density * Below ) * ( Fog.Density * Below ) );
			break;
		case EDrawFogMode::Linear:
			Visibility = 1.0f - NMath::Clamp( Fog.End != Fog.Start ? ( Height - Fog.Start ) / ( Fog.End - Fog.Start ) : ( Height < Fog.End ? 1.0f : 0.0f ), 0.0f, 1.0f );
			break;
		default:
			break;
		}
		return NMath::IsFinite( Visibility ) ? NMath::Clamp( Visibility, 0.0f, 1.0f ) : 1.0f;
	}
	// 0..1 の色の成分を、0..255 に丸める（範囲の外は端へ）。
	int32 ToByte( float32 Value )
	{
		return NMath::IsFinite( Value ) ? static_cast<int32>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f + 0.5f ) : 0;
	}
}

CGeometryCommandExecutor::CGeometryCommandExecutor( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels ) : Textures( InTextures ), Models( InModels ), Buffers( InBuffers ), Geometry( InGeometry )
{
}

void CGeometryCommandExecutor::FillLightConstants( float32* Constants, FDrawRgb Bright ) const
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
		Set( Base + 3, NMath::Cos( Light.InnerAngle * 0.5f ), 0.0f, 0.0f, 0.0f );
		Set( Base + 4, Light.Diffuse.r, Light.Diffuse.g, Light.Diffuse.b, 0.0f );
		Set( Base + 5, Light.Specular.r, Light.Specular.g, Light.Specular.b, 0.0f );
		Set( Base + 6, Light.Ambient.r, Light.Ambient.g, Light.Ambient.b, 0.0f );
	}
}

bool CGeometryCommandExecutor::Submit( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend, int32 Texture, bool bLit )
{
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
		return SubmitShadowCaster( Vertices, Topology, Alpha, Texture ); // 影の絵へ、光から見た深度として描く。
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
		FillLightConstants( Constants, Bright );
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

void CGeometryCommandExecutor::SetShadowReceiver( int32 Slot, int32 ShadowMap )
{
	if ( Slot < 0 || Slot >= 3 )
	{
		return;
	}
	ShadowSlotMaps[Slot] = ShadowMap < 0 ? -1 : ShadowMap;
	ReceiverSlot = -1;
	for ( int32 Index = 0; Index < 3 && ReceiverSlot < 0; ++Index )
	{
		ReceiverSlot = ShadowSlotMaps[Index] >= 0 ? Index : -1;
	}
}

bool CGeometryCommandExecutor::SubmitShadowCaster( const TArray<FDrawVertex3D>& Vertices, EGeometryTopology Topology, int32 Alpha, int32 Texture )
{
	if ( Topology != EGeometryTopology::Triangles || Texture >= 0 || Alpha < 250 )
	{
		return true; // 影を落とさない図形。
	}
	if ( !ShaderSource || !Geometry )
	{
		return false;
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

bool CGeometryCommandExecutor::BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out )
{
	if ( !Geometry || Geometry->GetWidth() <= 0 || Geometry->GetHeight() <= 0 || !Camera.IsValid() )
	{
		return false;
	}
	TArray<FDrawVertex3D> Vertices; // 表とUVを、共通処理で作る。
	if ( !CGeometryQuadBuilder::Build( Data.Quad, Camera, Vertices ) )
	{
		return false;
	}
	const FCameraViewport Viewport = { Geometry->GetWidth(), Geometry->GetHeight() }; // 同じ論理画面で射影する。
	const FMatrix4x4 Transform = World * Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ローカルからクリップへの変換。
	TArray<FDrawShaderVertex3D> Result; // 全頂点が正しい場合だけ渡す。
	Result.Reserve( Vertices.Num() );
	for ( const FDrawVertex3D& Input : Vertices )
	{
		if ( !Finite( Input.Position ) || !NMath::IsFinite( Input.U ) || !NMath::IsFinite( Input.V ) )
		{
			return false;
		}
		FVector4 Clip = Transform.Transform( FVector4( Input.Position[0], Input.Position[1], Input.Position[2], 1 ) ); // 透視除算しない（wを渡す）。
		Clip.x += 2 * Camera.ScreenOffset.x / Viewport.Width * Clip.w;
		Clip.y -= 2 * Camera.ScreenOffset.y / Viewport.Height * Clip.w;
		FDrawShaderVertex3D Vertex; // 法線などは使わない（0）。
		FMemory::Memzero( &Vertex, sizeof( Vertex ) );
		FMemory::Memcpy( Vertex.Position, Input.Position, sizeof( Vertex.Position ) );
		Vertex.SubPosition[0] = Clip.x;
		Vertex.SubPosition[1] = Clip.y;
		Vertex.SubPosition[2] = Clip.z;
		Vertex.SubPosition[3] = Clip.w;
		FMemory::Memcpy( Vertex.Diffuse, Input.Diffuse, sizeof( Vertex.Diffuse ) );
		Vertex.U = Input.U;
		Vertex.V = Input.V;
		Vertex.SubU = Data.Extra[0]; // 四角形ごとの4つの数（ピクセルシェーダーのTEXCOORD1）。
		Vertex.SubV = Data.Extra[1];
		Vertex.Tangent[0] = Data.Extra[2];
		Vertex.Tangent[1] = Data.Extra[3];
		if ( !Finite( Vertex.SubPosition ) )
		{
			return false;
		}
		Result.Add( Vertex );
	}
	Out = MoveTemp( Result );
	return true;
}

bool CGeometryCommandExecutor::CanDrawForward( const FStaticModelDrawView& Model ) const
{
	static const bool bEnabledByCommandLine = !NCommandLine::HasFlag( "no-gpu-forward" ); // 比べるための切り替え（--no-gpu-forward で、従来の CPU の経路にする）。
	return bEnabledByCommandLine && ShaderSource != nullptr && Geometry != nullptr && Model.MeshKey != 0 && Model.NumVertices <= 65536 && !bShadowCasting && ReceiverSlot < 0 && Fog.Mode == 0 && HeightFog.Mode == 0
		&& TextureTransform.Enabled == 0 && !( Lighting.IsEnabled() && Lighting.GetOptions().bPixelLighting ) && Camera.IsValid() && Geometry->GetWidth() > 0 && Geometry->GetHeight() > 0 && bUseForwardPath;
}

int32 CGeometryCommandExecutor::DrawModelForward( const FStaticModelDrawView& Model, int32 ModelAlpha, bool bAlphaBlend )
{
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
	FillLightConstants( Draw.Lighting, FDrawRgb() ); // モデルは、描画の明るさ（Bright）を使わない（従来の経路と同じ）。
	Draw.MeshKey = Model.MeshKey;
	Draw.MeshVersion = Model.MeshVersion;
	Draw.Vertices = Model.Vertices;
	Draw.NumVertices = Model.NumVertices;
	Draw.Indices = Model.Indices;
	Draw.NumIndices = Model.NumIndices;
	Draw.AlphaScale = static_cast<float32>( ModelAlpha ) / 255.0f;
	Draw.bLit = Lighting.IsEnabled();
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
		Draw.State.bAlphaBlend = bAlphaBlend || ModelAlpha < 255 || Section.Texture >= 0 || bAnyTranslucent;
		if ( !ShaderSource->DrawForwardMesh( Draw, *Geometry ) )
		{
			break; // 描けなかった範囲から先は、CPU の経路で描く。
		}
		++NumDrawn;
	}
	return NumDrawn;
}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::DrawModel( const FDrawModelData& Data, int32 Alpha, bool bAlphaBlend )
{
	FStaticModelDrawView Model; // 同期実行中だけ借りる不変形状。
	const bool bFiniteMatrix = Finite( Data.M );
	if ( bFiniteMatrix )
	{
		if ( Data.AnimIndex >= 0 )
		{
			Models->SetStaticModelAnimTime( Data.Handle, Data.AnimIndex, Data.AnimTime ); // DxLibと同じく、付けていなくても、そのまま描く。
		}
		Models->SetStaticModelPlacement( Data.Handle, Data.M );
	}
	const bool bGotView = bFiniteMatrix && Models->GetStaticModelDrawView( Data.Handle, Model );
	if ( bGotView && Model.NumIndices == 0 && Model.NumSections == 0 )
	{
		return EResult::Executed; // 描くものが無い（形の無いモデル・すべて非表示）。
	}
	if ( !bGotView || !Model.Vertices || !Model.Indices || !Model.Sections
		|| Model.NumVertices <= 0 || Model.NumVertices > 786432 || Model.NumIndices <= 0 || Model.NumIndices > 786432 || Model.NumSections <= 0 || Model.NumSections > 4096
		|| !NMath::IsFinite( Model.Opacity ) || Model.Opacity < 0 || Model.Opacity > 1 )
	{
		return EResult::Rejected;
	}
	int32 ExpectedIndex = 0; // すべての範囲を描く前に検査する。
	for ( int32 Index = 0; Index < Model.NumSections; ++Index )
	{
		const auto& Section = Model.Sections[Index]; // 連続した材質範囲。
		if ( Section.FirstIndex != ExpectedIndex || Section.NumIndices <= 0 || Section.NumIndices % 3 != 0 || static_cast<int64>( Section.FirstIndex ) + Section.NumIndices > Model.NumIndices )
		{
			return EResult::Rejected;
		}
		if ( Section.Texture >= 0 )
		{
			FTextureDrawView Image; // 後の材質の画像が無い場合も先に失敗する。
			if ( !Textures || !Textures->GetTextureDrawView( Section.Texture, Image ) || Image.Binding == 0 || Image.Width <= 0 || Image.Height <= 0 )
			{
				return EResult::Rejected;
			}
		}
		ExpectedIndex += Section.NumIndices;
	}
	if ( ExpectedIndex != Model.NumIndices )
	{
		return EResult::Rejected;
	}
	for ( int32 Index = 0; Index < Model.NumVertices; ++Index )
	{
		const auto& Vertex = Model.Vertices[Index]; // 数値の検査も描画より前に済ませる。
		if ( !Finite( Vertex.Position ) || !NMath::IsFinite( Vertex.U ) || !NMath::IsFinite( Vertex.V ) )
		{
			return EResult::Rejected;
		}
	}
	for ( int32 Index = 0; Index < Model.NumIndices; ++Index )
	{
		if ( Model.Indices[Index] >= static_cast<uint32>( Model.NumVertices ) )
		{
			return EResult::Rejected;
		}
	}
	if ( Model.Opacity == 0 )
	{
		return EResult::Executed;
	}
	const FMatrix4x4 OldWorld = World; // モデルの配置をほかの命令へ漏らさない。
	FDrawMatrixData Matrix; // モデル個体の配置。
	FMemory::Memcpy( Matrix.M, Data.M, sizeof( Data.M ) );
	World = ToMatrix( Matrix );
	const int32 ModelAlpha = NMath::Clamp( static_cast<int32>( Alpha * Model.Opacity + 0.5f ), 0, 255 ); // 個体の不透明度を一度だけ掛ける。
	const int32 NumForward = CanDrawForward( Model ) ? DrawModelForward( Model, ModelAlpha, bAlphaBlend ) : 0; // GPU が変換と光を計算できる範囲は、共有のメッシュで描く。
	bool bSuccess = true; // GPU作成失敗時も配置は復元する。
	for ( int32 SectionIndex = NumForward; SectionIndex < Model.NumSections && bSuccess; ++SectionIndex )
	{
		const auto& Section = Model.Sections[SectionIndex]; // 三角形順を保つ範囲。
		for ( int32 Offset = 0; Offset < Section.NumIndices && bSuccess; )
		{
			const int32 Count = NMath::Min( 65535, Section.NumIndices - Offset ); // 三角形単位でGPU上限以内へ分ける。
			TArray<FDrawVertex3D> Ordered; // 今回の分割だけを所有する。
			Ordered.Reserve( Count );
			bool bUseAlpha = bAlphaBlend || ModelAlpha < 255 || Section.Texture >= 0; // 画像の透明画素も合成する。
			for ( int32 Index = 0; Index < Count; ++Index )
			{
				const auto& Vertex = Model.Vertices[Model.Indices[Section.FirstIndex + Offset + Index]]; // 検査済みの参照。
				Ordered.Add( Vertex );
				bUseAlpha = bUseAlpha || Vertex.Diffuse[3] < 255;
			}
			bSuccess = Submit( Ordered, EGeometryTopology::Triangles, FDrawRgb(), ModelAlpha, bUseAlpha, Section.Texture, true );
			Offset += Count;
		}
	}
	World = OldWorld;
	return bSuccess ? EResult::Executed : EResult::Rejected;

}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	switch ( View.Type )
	{
	case EDrawCommand::PushCamera:
	{
		if ( !View.CanRead<FDrawCameraData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCameraData>(); // 検査後の固定部。
		FCamera Next; // 検査完了まで現在のカメラを保つ。
		Next.Position = FVector3( Data.Position[0], Data.Position[1], Data.Position[2] );
		Next.Target = FVector3( Data.Target[0], Data.Target[1], Data.Target[2] );
		Next.Up = FVector3( Data.Up[0], Data.Up[1], Data.Up[2] );
		Next.Projection = Data.Orthographic ? ECameraProjection::Orthographic : ECameraProjection::Perspective;
		Next.FovY = Data.FovY;
		Next.OrthoHeight = Data.OrthoHeight;
		Next.Near = Data.Near;
		Next.Far = Data.Far;
		Next.HorizontalStretch = Data.HorizontalStretch;
		Next.ScreenOffset = FVector2( Data.ScreenOffsetX, Data.ScreenOffsetY );
		if ( !Next.IsValid() )
		{
			return EResult::Rejected;
		}
		CameraStack.Add( Camera );
		Camera = Next;
		return EResult::Executed;
	}
	case EDrawCommand::PopCamera:
	{
		if ( !View.CanRead<FDrawPopCameraData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopCameraData>(); // 検査後の固定部。
		(void)Data;
		if ( !CameraStack.IsEmpty() )
		{
			Camera = CameraStack.Pop();
		}
		return EResult::Executed;
	}
	case EDrawCommand::WorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EResult::Rejected;
		}
		World = ToMatrix( Data );
		return EResult::Executed;
	}
	case EDrawCommand::PushWorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EResult::Rejected;
		}
		WorldStack.Add( World );
		World = ToMatrix( Data ) * World;
		return EResult::Executed;
	}
	case EDrawCommand::PopWorldMatrix:
	{
		if ( !View.CanRead<FDrawPopWorldMatrixData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopWorldMatrixData>(); // 検査後の固定部。
		(void)Data;
		if ( !WorldStack.IsEmpty() )
		{
			World = WorldStack.Pop();
		}
		return EResult::Executed;
	}
	case EDrawCommand::Depth:
	{
		if ( !View.CanRead<FDrawDepthData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawDepthData>(); // 検査後の固定部。
		if ( Data.Compare < 0 || Data.Compare > 7 )
		{
			return EResult::Rejected;
		}
		State.bTestDepth = Data.TestDepth != 0;
		State.bWriteDepth = Data.WriteDepth != 0;
		State.Compare = static_cast<EGeometryCompare>( Data.Compare );
		State.DepthBias = Data.Bias;
		return EResult::Executed;
	}
	case EDrawCommand::Culling:
	{
		if ( !View.CanRead<FDrawCullingData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCullingData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > 2 )
		{
			return EResult::Rejected;
		}
		State.Culling = static_cast<EGeometryCulling>( Data.Mode );
		return EResult::Executed;
	}
	case EDrawCommand::ClearDepth:
	{
		if ( !View.CanRead<FDrawClearDepthData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawClearDepthData>(); // 検査後の固定部。
		(void)Data;
		if ( !Geometry )
		{
			return EResult::Rejected;
		}
		Geometry->ClearDepth();
		return EResult::Executed;
	}
	case EDrawCommand::UseLighting:
	{
		if ( !View.CanRead<FDrawUseLightingData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawUseLightingData>(); // 検査後の固定部。
		Lighting.SetEnabled( Data.Enabled != 0 );
		return EResult::Executed;
	}
	case EDrawCommand::DefaultLight:
	{
		if ( !View.CanRead<FDrawDefaultLightData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawDefaultLightData>(); // 検査後の固定部。
		Lighting.SetDefaultLight( Data.Enabled != 0, Data.ShadowMapSlots & NDrawShadowMap::AllSlots );
		return EResult::Executed;
	}
	case EDrawCommand::AmbientLight:
	{
		if ( !View.CanRead<FDrawAmbientLightData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawAmbientLightData>(); // 検査後の固定部。
		if ( !Finite( Data.Color ) )
		{
			return EResult::Rejected;
		}
		Lighting.SetAmbient( ToColor( Data.Color ) );
		return EResult::Executed;
	}
	case EDrawCommand::SurfaceMaterial:
	{
		if ( !View.CanRead<FDrawSurfaceMaterialData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawSurfaceMaterialData>(); // 検査後の固定部。
		FSurfaceMaterial Material; // 検査が済むまで、現在の材質を保つ。
		Material.Diffuse = ToColor( Data.Diffuse );
		Material.Ambient = ToColor( Data.Ambient );
		Material.Specular = ToColor( Data.Specular );
		Material.Emissive = ToColor( Data.Emissive );
		Material.Shininess = Data.Shininess;
		Material.bUseVertexDiffuse = Data.UseVertexDiffuse != 0;
		Material.bUseVertexSpecular = Data.UseVertexSpecular != 0;
		return Lighting.SetMaterial( Material ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::LightingOptions:
	{
		if ( !View.CanRead<FDrawLightingOptionsData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawLightingOptionsData>(); // 検査後の固定部。
		FLightingOptions Options; // 画素ごとの光は、この経路では頂点ごとになる。
		Options.bUseSpecular = Data.UseSpecular != 0;
		Options.bPixelLighting = Data.PixelLighting != 0;
		Options.bWrapLighting = Data.WrapLighting != 0;
		Options.bIgnoreSurfaceAngle = Data.IgnoreSurfaceAngle != 0;
		Lighting.SetOptions( Options );
		return EResult::Executed;
	}
	case EDrawCommand::PushLight:
	{
		if ( !View.CanRead<FDrawLightData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawLightData>(); // 検査後の固定部。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawLightType::Spot ) )
		{
			return EResult::Rejected;
		}
		FLight Light; // 命令の値を、計算用の値へ移す。
		Light.Type = static_cast<EDrawLightType>( Data.Type );
		Light.Position = FVector3( Data.Position[0], Data.Position[1], Data.Position[2] );
		Light.Direction = FVector3( Data.Direction[0], Data.Direction[1], Data.Direction[2] );
		Light.Diffuse = ToColor( Data.Diffuse );
		Light.Specular = ToColor( Data.Specular );
		Light.Ambient = ToColor( Data.Ambient );
		Light.Range = Data.Range;
		Light.ConstantAttenuation = Data.ConstantAttenuation;
		Light.LinearAttenuation = Data.LinearAttenuation;
		Light.QuadraticAttenuation = Data.QuadraticAttenuation;
		Light.OuterAngle = Data.OuterAngle;
		Light.InnerAngle = Data.InnerAngle;
		Light.ShadowMapSlots = Data.ShadowMapSlots & NDrawShadowMap::AllSlots;
		if ( !Lighting.PushLight( Light ) )
		{
			// 外すときの数を合わせるため、使えないライトも、光らないライトとして積む。
			FLight Dark = FLight::MakeDirectional( FVector3( 0.0f, -1.0f, 0.0f ), FColor( 0.0f, 0.0f, 0.0f, 0.0f ) );
			Lighting.PushLight( Dark );
		}
		return EResult::Executed;
	}
	case EDrawCommand::PopLight:
	{
		if ( !View.CanRead<FDrawPopLightData>() )
		{
			return EResult::Rejected;
		}
		Lighting.PopLight();
		return EResult::Executed;
	}
	case EDrawCommand::Fog:
	{
		if ( !View.CanRead<FDrawFogData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawFogData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFogMode::Linear ) || !NMath::IsFinite( Data.Start ) || !NMath::IsFinite( Data.End ) || !NMath::IsFinite( Data.Density ) )
		{
			return EResult::Rejected;
		}
		Fog = Data;
		return EResult::Executed;
	}
	case EDrawCommand::HeightFog:
	{
		if ( !View.CanRead<FDrawFogData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawFogData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFogMode::Linear ) || !NMath::IsFinite( Data.Start ) || !NMath::IsFinite( Data.End ) || !NMath::IsFinite( Data.Density ) )
		{
			return EResult::Rejected;
		}
		HeightFog = Data;
		return EResult::Executed;
	}
	case EDrawCommand::TextureTransform:
	{
		if ( !View.CanRead<FDrawTextureTransformData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawTextureTransformData>(); // 検査後の固定部。
		if ( Data.Enabled != 0 && !( NMath::IsFinite( Data.TranslationU ) && NMath::IsFinite( Data.TranslationV ) && NMath::IsFinite( Data.ScaleU ) && NMath::IsFinite( Data.ScaleV ) && NMath::IsFinite( Data.CenterU )
			&& NMath::IsFinite( Data.CenterV ) && NMath::IsFinite( Data.Rotation ) ) )
		{
			return EResult::Rejected;
		}
		TextureTransform = Data;
		return EResult::Executed;
	}
	case EDrawCommand::Anisotropy:
	{
		if ( !View.CanRead<FDrawAnisotropyData>() )
		{
			return EResult::Rejected;
		}
		AnisotropyLevel = NMath::Clamp( View.Read<FDrawAnisotropyData>().Level, 1, 16 ); // 異方性の最大の強さ。Filter が異方性のときだけ効く。
		State.MaxAnisotropy = bAnisotropicFilter ? static_cast<uint8>( AnisotropyLevel ) : 1;
		return EResult::Executed;
	}
	case EDrawCommand::Pixel3D:
	{
		if ( !View.CanRead<FDrawPixel3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPixel3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // 点 1 つ。
		Vertices.Add( Vertex( Data.Pos, Data.Color ) );
		return Submit( Vertices, EGeometryTopology::Points, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Cone3D:
	{
		if ( !View.CanRead<FDrawCone3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCone3DData>(); // 頂点と、底面の中心。
		TArray<FDrawVertex3D> Vertices; // 面または線。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildCone( FVector3( Data.Top[0], Data.Top[1], Data.Top[2] ), FVector3( Data.Bottom[0], Data.Bottom[1], Data.Bottom[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend, -1, bFill ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Line3D:
	{
		if ( !View.CanRead<FDrawLine3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawLine3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // この命令の点列。
		Vertices.Add( Vertex( Data.P0, Data.Color ) );
		Vertices.Add( Vertex( Data.P1, Data.Color ) );
		return Submit( Vertices, EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Triangle3D:
	{
		if ( !View.CanRead<FDrawTriangle3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawTriangle3DData>(); // 検査後の固定部。
		TArray<FDrawVertex3D> Vertices; // この命令の点列。
		if ( View.Has( FDrawFlag::Fill ) )
		{
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
		}
		else
		{
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P1, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
			Vertices.Add( Vertex( Data.P2, Data.Color ) );
			Vertices.Add( Vertex( Data.P0, Data.Color ) );
		}
		return Submit( Vertices, View.Has( FDrawFlag::Fill ) ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Cube3D:
	{
		if ( !View.CanRead<FDrawCube3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCube3DData>(); // 検査後の固定部。
		if ( !Finite( Data.Min ) || !Finite( Data.Max ) )
		{
			return EResult::Rejected;
		}
		FDrawVertex3D Corners[8]; // XYZ各軸をbitで選んだ箱の角。
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			const float32 Point[3] = { Index & 1 ? Data.Max[0] : Data.Min[0], Index & 2 ? Data.Max[1] : Data.Min[1], Index & 4 ? Data.Max[2] : Data.Min[2] }; // 箱の角。
			Corners[Index] = Vertex( Point, Data.Diffuse );
		}
		const int32 Faces[] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,4,2, 2,4,6, 1,3,5, 3,7,5, 0,1,4, 1,5,4, 2,6,3, 3,6,7 }; // 外側を向く各面。
		const int32 Edges[] = { 0,1, 0,2, 0,4, 1,3, 1,5, 2,3, 2,6, 3,7, 4,5, 4,6, 5,7, 6,7 }; // 重複しない12辺。
		const float32 FaceNormals[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } }; // Faces の面の順（6頂点ずつ）の、外向きの法線。
		TArray<FDrawVertex3D> Vertices; // 展開済みの頂点列。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 面または辺。
		const int32* Indices = bFill ? Faces : Edges; // 同じ角を再利用する順序。
		for ( int32 Index = 0; Index < ( bFill ? 36 : 24 ); ++Index )
		{
			FDrawVertex3D Corner = Corners[Indices[Index]]; // 面ごとに法線が違うので、角を複写する。
			if ( bFill )
			{
				FMemory::Memcpy( Corner.Normal, FaceNormals[Index / 6], sizeof( Corner.Normal ) );
			}
			Vertices.Add( Corner );
		}
		SetSpecular( Vertices, Data.Specular );
		return Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend, -1, bFill ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Sphere3D:
	{
		if ( !View.CanRead<FDrawSphere3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawSphere3DData>(); // ローカルの球。
		TArray<FDrawVertex3D> Vertices; // 面または緯線・経線。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildSphere( FVector3( Data.Center[0], Data.Center[1], Data.Center[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend, -1, bFill ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Capsule3D:
	{
		if ( !View.CanRead<FDrawCapsule3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawCapsule3DData>(); // 両端は半球の中心。
		TArray<FDrawVertex3D> Vertices; // GPUによらない展開結果。
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしを選ぶ。
		if ( !CGeometryShapeBuilder::BuildCapsule( FVector3( Data.P0[0], Data.P0[1], Data.P0[2] ), FVector3( Data.P1[0], Data.P1[1], Data.P1[2] ), Data.Radius, Data.Div, bFill, FColor32( Data.Diffuse.R, Data.Diffuse.G, Data.Diffuse.B ), Vertices ) )
		{
			return EResult::Rejected;
		}
		SetSpecular( Vertices, Data.Specular );
		return Vertices.IsEmpty() || Submit( Vertices, bFill ? EGeometryTopology::Triangles : EGeometryTopology::Lines, Bright, Alpha, bAlphaBlend, -1, bFill ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Quad3D:
	{
		if ( !View.CanRead<FDrawQuad3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawQuad3DData>(); // 向きと基準点を持つ画像。
		TArray<FDrawVertex3D> Vertices; // 表とUVを共通処理で作る。
		return CGeometryQuadBuilder::Build( Data, Camera, Vertices ) && SubmitQuad( Vertices, Data.Handle, Bright, Alpha, true ) ? EResult::Executed : EResult::Rejected; // Quad3Dは光を受ける。
	}
	case EDrawCommand::Billboard3D:
	case EDrawCommand::BillboardPivot3D:
	case EDrawCommand::Sprite3D:
	case EDrawCommand::SpriteStretch3D:
	case EDrawCommand::SpritePivot3D:
	{
		FDrawQuad3DData Quad; // 大きさ・基準点の異なる命令を同じ四角形へそろえる。
		bool bPixelSize = false, bPixelPivot = false; // 画素単位で受け取った項目。
		if ( View.Type == EDrawCommand::Billboard3D )
		{
			if ( !View.CanRead<FDrawBillboard3DData>() )
			{
				return EResult::Rejected;
			}
			const auto Data = View.Read<FDrawBillboard3DData>(); // 中心を基準に横幅で指定する画像。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Size;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
		}
		else if ( View.Type == EDrawCommand::BillboardPivot3D )
		{
			if ( !View.CanRead<FDrawBillboardPivot3DData>() )
			{
				return EResult::Rejected;
			}
			const auto Data = View.Read<FDrawBillboardPivot3DData>(); // 0〜1を基準とする画像。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Size;
			Quad.PivotX = Data.PivotX;
			Quad.PivotY = Data.PivotY;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
		}
		else if ( View.Type == EDrawCommand::Sprite3D )
		{
			if ( !View.CanRead<FDrawSprite3DData>() )
			{
				return EResult::Rejected;
			}
			const auto Data = View.Read<FDrawSprite3DData>(); // 1画素あたりのワールド長。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Quad.Height = Data.Scale;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
			bPixelSize = true;
		}
		else if ( View.Type == EDrawCommand::SpriteStretch3D )
		{
			if ( !View.CanRead<FDrawSpriteStretch3DData>() )
			{
				return EResult::Rejected;
			}
			const auto Data = View.Read<FDrawSpriteStretch3DData>(); // 横と縦で違う画素の大きさ。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Data.ScaleX;
			Quad.Height = Data.ScaleY;
			Quad.Handle = Data.Handle;
			bPixelSize = true;
		}
		else
		{
			if ( !View.CanRead<FDrawSpritePivot3DData>() )
			{
				return EResult::Rejected;
			}
			const auto Data = View.Read<FDrawSpritePivot3DData>(); // 画素単位の基準点と拡大率。
			FMemory::Memcpy( Quad.Pos, Data.Pos, sizeof( Quad.Pos ) );
			Quad.Width = Data.ScaleX;
			Quad.Height = Data.ScaleY;
			Quad.PivotX = Data.PivotX;
			Quad.PivotY = Data.PivotY;
			Quad.Angle = Data.Angle;
			Quad.Handle = Data.Handle;
			bPixelSize = bPixelPivot = true;
		}
		FTextureDrawView Image; // 縦横比と画素寸法だけを使い、GPUの型は見ない。
		if ( Quad.Handle < 0 || !Textures || !Textures->GetTextureDrawView( Quad.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 || Image.Binding == 0 )
		{
			return EResult::Rejected;
		}
		if ( bPixelSize )
		{
			Quad.Width *= Image.Width;
			Quad.Height *= Image.Height;
		}
		else
		{
			Quad.Height *= static_cast<float32>( Image.Height ) / Image.Width;
		}
		if ( bPixelPivot )
		{
			Quad.PivotX /= Image.Width;
			Quad.PivotY /= Image.Height;
		}
		if ( View.Has( FDrawFlag::FlipX ) )
		{
			Quad.U0 = 1;
			Quad.U1 = 0;
		}
		if ( View.Has( FDrawFlag::FlipY ) )
		{
			Quad.V0 = 1;
			Quad.V1 = 0;
		}
		TArray<FDrawVertex3D> Vertices; // 反転はUVだけに反映し、基準点を動かさない。
		return CGeometryQuadBuilder::Build( Quad, Camera, Vertices ) && SubmitQuad( Vertices, Quad.Handle, Bright, Alpha ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::BillboardQuad3D:
	{
		if ( !View.CanRead<FDrawBillboardQuad3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawBillboardQuad3DData>(); // 四隅を自由に動かした画像。
		TArray<FDrawVertex3D> Vertices; // つぶれていない三角形だけを描く。
		return CGeometryQuadBuilder::BuildCorners( FVector3( Data.Pos[0], Data.Pos[1], Data.Pos[2] ), Data.Corner, Camera, Vertices ) && SubmitQuad( Vertices, Data.Handle, Bright, Alpha ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Ribbon3D:
	{
		// 経路の点（ワールド座標）を、カメラへ面を向けた帯の三角形にして描く。ワールド行列は使わず、表裏の区別もしない。
		constexpr int32 DataWords = sizeof( FDrawRibbonData ) / 4; // 固定部の長さ（ワード）。
		constexpr int32 PointWords = sizeof( FDrawRibbonData::FPoint ) / 4; // 点1つの長さ（ワード）。
		if ( View.NumDataWords < DataWords || !Camera.IsValid() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawRibbonData>(); // 検査前の固定部。
		if ( Data.NumPoints < 2 || Data.NumPoints > FDrawRibbonData::MaxPoints || View.NumDataWords != DataWords + Data.NumPoints * PointWords )
		{
			return EResult::Rejected;
		}
		TArray<FDrawRibbonData::FPoint> Points; // 借用メモリのalignmentに頼らず、所有コピーを作る。
		Points.SetNum( Data.NumPoints );
		FMemory::Memcpy( Points.GetData(), View.Data + DataWords, static_cast<usize>( Data.NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
		FDrawRibbonCamera RibbonCamera; // 帯の幅を向けるカメラ。
		RibbonCamera.Position = Camera.Position;
		RibbonCamera.Right = Camera.GetRight();
		RibbonCamera.Up = Camera.GetUpVector();
		RibbonCamera.Front = Camera.GetForward();
		FDrawRibbonMesh Mesh; // 画像の繰り返しは、整数のUの境界で分ける（DxLibの描画と同じ）。
		if ( !NDrawRibbonMesh::Build( Data, Points.GetData(), RibbonCamera, true, Mesh ) )
		{
			return EResult::Rejected;
		}
		TArray<FDrawVertex3D> Triangles; // 番号を解決した三角形のリスト。
		Triangles.Reserve( Mesh.Indices.Num() );
		for ( const uint16 Index : Mesh.Indices )
		{
			Triangles.Add( Mesh.Vertices[Index] );
		}
		const FMatrix4x4 OldWorld = World; // 成功・失敗のどちらでも元に戻す。
		const EGeometryCulling OldCulling = State.Culling;
		World = FMatrix4x4::Identity;
		State.Culling = EGeometryCulling::None;
		const bool bSuccess = Submit( Triangles, EGeometryTopology::Triangles, Bright, bAlphaBlend ? Alpha : 255, true, Data.Texture, true ); // 上書きのときも、通常のアルファで描く。
		World = OldWorld;
		State.Culling = OldCulling;
		return bSuccess ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Primitive3D:
	{
		if ( !View.CanRead<FDrawPrimitive3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitive3DData>(); // 検査後の固定部。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) || Data.NumVertices <= 0 || Data.NumIndices < 0
			|| FDrawPrimitive3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) != static_cast<size_t>( View.NumDataWords + 1 ) )
		{
			return EResult::Rejected;
		}
		const auto Type = static_cast<EDrawPrimitive>( Data.Type ); // 形式を検査したつなぎ方。
		const int32 Count = Data.NumIndices > 0 ? Data.NumIndices : Data.NumVertices; // 参照する要素数。
		if ( !NDrawPrimitive::IsValidCount( Type, Count ) || Count > 65536 )
		{
			return EResult::Rejected;
		}
		TArray<FDrawVertex3D> Source; // 可変長の入力を所有コピーしてから調べる。
		Source.SetNum( Data.NumVertices );
		const auto* Bytes = reinterpret_cast<const uint8*>( View.Data ) + sizeof( Data ); // 頂点列の先頭。
		FMemory::Memcpy( Source.GetData(), Bytes, static_cast<usize>( Data.NumVertices ) * sizeof( FDrawVertex3D ) );
		const auto* Indices = Bytes + static_cast<usize>( Data.NumVertices ) * sizeof( FDrawVertex3D ); // index列の先頭。
		TArray<FDrawVertex3D> Ordered; // indexを解決した点列。
		Ordered.Reserve( Count );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			uint16 Value = 0; // 借用メモリのalignmentに頼らず読む。
			if ( Data.NumIndices > 0 )
			{
				FMemory::Memcpy( &Value, Indices + Index * sizeof( uint16 ), sizeof( Value ) );
			}
			const int32 VertexIndex = Data.NumIndices > 0 ? Value : Index; // 解決した参照先。
			if ( VertexIndex >= Source.Num() || !Finite( Source[VertexIndex].Position ) )
			{
				return EResult::Rejected;
			}
			Ordered.Add( Source[VertexIndex] );
		}
		return SubmitPrimitive( Ordered, Type, Data.Texture, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Filter:
	{
		if ( !View.CanRead<FDrawFilterData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawFilterData>(); // 最近傍・線形補間・異方性（線形のうえで、異方性の強さを使う）。
		if ( Data.Mode < 0 || Data.Mode > 2 )
		{
			return EResult::Rejected;
		}
		State.bLinearFilter = Data.Mode >= 1;
		bAnisotropicFilter = Data.Mode == 2;
		State.MaxAnisotropy = bAnisotropicFilter ? static_cast<uint8>( AnisotropyLevel ) : 1;
		return EResult::Executed;
	}
	case EDrawCommand::TextureAddress:
	{
		if ( !View.CanRead<FDrawTextureAddressData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawTextureAddressData>(); // UVの外側の参照方法。
		if ( Data.U < 0 || Data.U > 3 || Data.V < 0 || Data.V > 3 )
		{
			return EResult::Rejected;
		}
		State.AddressU = static_cast<EGeometryTextureAddress>( Data.U );
		State.AddressV = static_cast<EGeometryTextureAddress>( Data.V );
		return EResult::Executed;
	}
	case EDrawCommand::PrimitiveBuffer3D:
	{
		if ( !Buffers || !View.CanRead<FDrawPrimitiveBuffer3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitiveBuffer3DData>(); // 記録時の登録番号と範囲。
		FGeometryVertexBufferView Vertices; // 同期の処理中だけ借用する。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) || Data.Start < 0 || Data.Num <= 0 || Data.Num > 65536
			|| !Buffers->GetVertexBufferView( Data.VertexBuffer, Vertices ) || !Vertices.Vertices || Data.NumBufferVertices != Vertices.NumVertices )
		{
			return EResult::Rejected;
		}
		FGeometryIndexBufferView Indices; // indexありの場合だけ借りる。
		if ( Data.IndexBuffer >= 0 )
		{
			if ( !Buffers->GetIndexBufferView( Data.IndexBuffer, Indices ) || !Indices.Indices || Data.NumBufferIndices != Indices.NumIndices
				|| static_cast<int64>( Data.Start ) + Data.Num > Indices.NumIndices )
			{
				return EResult::Rejected;
			}
		}
		else if ( Data.BaseVertex != 0 || Data.NumBufferIndices != 0 || static_cast<int64>( Data.Start ) + Data.Num > Vertices.NumVertices )
		{
			return EResult::Rejected;
		}
		TArray<FDrawVertex3D> Ordered; // 基準頂点を含めて参照を解決した列。
		Ordered.Reserve( Data.Num );
		for ( int32 Index = 0; Index < Data.Num; ++Index )
		{
			const int64 VertexIndex = Data.IndexBuffer >= 0 ? static_cast<int64>( Indices.Indices[Data.Start + Index] ) + Data.BaseVertex : Data.Start + Index; // 加算は64bit。
			if ( VertexIndex < 0 || VertexIndex >= Vertices.NumVertices )
			{
				return EResult::Rejected;
			}
			Ordered.Add( Vertices.Vertices[static_cast<int32>( VertexIndex )] );
		}
		return SubmitPrimitive( Ordered, static_cast<EDrawPrimitive>( Data.Type ), Data.Texture, Bright, Alpha, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::Model3D:
	{
		if ( !Models || !View.CanRead<FDrawModelData>() )
		{
			return EResult::Rejected;
		}
		return DrawModel( View.Read<FDrawModelData>(), Alpha, bAlphaBlend ); // 個体の配置と、静的モデルの登録番号。
	}
	// 次に描くモデルの、アニメーションの状態（DxLibのMV1SetAttachAnimTime / MV1SetAttachAnimBlendRate / MV1SetAttachAnimBlendRateToFrame）。
	case EDrawCommand::ModelAnim:
	{
		if ( !View.CanRead<FDrawModelAnimData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawModelAnimData>();
		if ( Models )
		{
			Models->SetStaticModelAnimTime( Data.Handle, Data.AttachIndex, Data.Time );
			Models->SetStaticModelAnimRate( Data.Handle, Data.AttachIndex, Data.Rate );
		}
		return EResult::Executed;
	}
	case EDrawCommand::ModelAnimFrame:
	{
		if ( !View.CanRead<FDrawModelAnimFrameData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawModelAnimFrameData>();
		if ( Models && Data.Handle >= 0 && Data.AttachIndex >= 0 && Data.Frame >= 0 && NMath::IsFinite( Data.Rate ) )
		{
			Models->SetStaticModelAnimFrameRate( Data.Handle, Data.AttachIndex, Data.Frame, NMath::Clamp( Data.Rate, 0.0f, 1.0f ), Data.bIncludeChildren != 0 );
		}
		return EResult::Executed;
	}
	case EDrawCommand::ModelFramePose3D:
	{
		// 描く間だけ骨を上書きして、描いたあとに元へ戻す（DxLib 版の ExecuteModelFramePose と同じ）。
		FDrawModelFramePoseData Header;
		if ( !Models || !NDrawModelFramePose::IsValidCommand( View, Header ) )
		{
			return EResult::Rejected;
		}
		FDrawModelData Draw;
		Draw.Handle = Header.Handle;
		FMemory::Memcpy( Draw.M, Header.M, sizeof( Draw.M ) );
		Draw.AnimIndex = Header.AnimIndex;
		Draw.AnimTime = Header.AnimTime;
		if ( Header.NumOverrides == 0 )
		{
			return DrawModel( Draw, Alpha, bAlphaBlend );
		}
		IModelBackend* Backend = Models->GetStaticModelBackend();
		if ( !Backend )
		{
			return EResult::Rejected;
		}
		TArray<FDrawModelFrameOverrideData> Overrides;
		for ( int32 Index = 0; Index < Header.NumOverrides; ++Index )
		{
			FDrawModelFrameOverrideData Override;
			if ( !NDrawModelFramePose::ReadOverride( View, Index, Override ) )
			{
				return EResult::Rejected;
			}
			Overrides.Add( Override );
		}
		FModelFrameOverrideScope Scope( *Backend, Header.Handle, Overrides.GetData(), Overrides.Num() );
		if ( !Scope.IsApplied() )
		{
			return EResult::Rejected;
		}
		const EResult Result = DrawModel( Draw, Alpha, bAlphaBlend );
		Scope.Restore();
		return Result;
	}
	case EDrawCommand::Mesh3D:
	{
		if ( !Buffers || !View.CanRead<FDrawMeshData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawMeshData>(); // 固定したメッシュの描画範囲。
		FGeometryVertexBufferView Vertices; // ローカル頂点。
		FGeometryIndexBufferView Indices; // 三角形の参照順。
		if ( !Finite( Data.M ) || Data.NumVertices < 3 || Data.StartIndex < 0 || Data.NumIndices <= 0 || Data.NumIndices > 65536 || Data.NumIndices % 3 != 0
			|| !Buffers->GetVertexBufferView( Data.VertexBuffer, Vertices ) || !Vertices.Vertices || Data.NumVertices > Vertices.NumVertices
			|| !Buffers->GetIndexBufferView( Data.IndexBuffer, Indices ) || !Indices.Indices || static_cast<int64>( Data.StartIndex ) + Data.NumIndices > Indices.NumIndices )
		{
			return EResult::Rejected;
		}
		TArray<FDrawVertex3D> Ordered; // 全indexを検査してから記録する。
		Ordered.Reserve( Data.NumIndices );
		for ( int32 Index = 0; Index < Data.NumIndices; ++Index )
		{
			const uint32 VertexIndex = Indices.Indices[Data.StartIndex + Index]; // この面が参照する頂点。
			if ( VertexIndex >= static_cast<uint32>( Data.NumVertices ) )
			{
				return EResult::Rejected;
			}
			Ordered.Add( Vertices.Vertices[VertexIndex] );
		}
		const FMatrix4x4 OldWorld = World; // この1命令の後で必ず復元する。
		const EGeometryCulling OldCulling = State.Culling; // 両面指定はこのメッシュに限る。
		FDrawMatrixData Matrix; // 同じ12要素の形式。
		FMemory::Memcpy( Matrix.M, Data.M, sizeof( Data.M ) );
		World = ToMatrix( Matrix );
		if ( View.Has( FDrawFlag::TwoSided ) )
		{
			State.Culling = EGeometryCulling::None;
		}
		// Mesh3Dは既存の契約に合わせBrightを重ねず、頂点色を使う。
		const bool bResult = Submit( Ordered, EGeometryTopology::Triangles, FDrawRgb(), Alpha, bAlphaBlend, Data.Texture, true ); // 失敗時も状態を戻す。
		World = OldWorld;
		State.Culling = OldCulling;
		return bResult ? EResult::Executed : EResult::Rejected;
	}
	default:
		return EResult::NotHandled;
	}
}

bool CGeometryCommandExecutor::SubmitPrimitive( const TArray<FDrawVertex3D>& Ordered, EDrawPrimitive Type, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
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

// 画像の四角形は両面を描き、完全な透明画素はGPU側で深度も含めて捨てる。
bool CGeometryCommandExecutor::SubmitQuad( const TArray<FDrawVertex3D>& Vertices, int32 Texture, FDrawRgb Bright, int32 Alpha, bool bLit )
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
