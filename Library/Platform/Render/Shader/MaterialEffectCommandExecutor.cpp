// SPDX-License-Identifier: MIT
#include "MaterialEffectCommandExecutor.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawDistortedMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	// 組み込みのピクセルシェーダー（b7）へ渡す定数。並びは、シェーダーの cbEffectMaterial と同じ。
	struct FEffectConstants
	{
		float32 A[4] = {};            // 表示画像の倍率 xy、マスクの倍率 z、マスク画像を使うか w。
		float32 B[4] = {};            // 消失を使うか x、アルファの強さ y、ゆがみを使うか z、境界の幅 w。
		float32 Edge[4] = {};         // 境界の色（アルファと発光倍率を掛けた値）。
		float32 Bright[4] = {};       // 描画の明るさ（RGB）。
		float32 Distortion[4] = {};   // ゆがみ画像の倍率 xy。
	};
	static_assert( sizeof( FEffectConstants ) == 80, "FEffectConstants must match cbEffectMaterial" );

	// 帯の点の色（下位から赤・緑・青・不透明度）を、頂点の色にする。
	void SetColor( FDrawShaderVertex3D& Vertex, uint32 Color )
	{
		Vertex.Diffuse[0] = static_cast<uint8>( Color & 0xFFu );
		Vertex.Diffuse[1] = static_cast<uint8>( ( Color >> 8 ) & 0xFFu );
		Vertex.Diffuse[2] = static_cast<uint8>( ( Color >> 16 ) & 0xFFu );
		Vertex.Diffuse[3] = static_cast<uint8>( ( Color >> 24 ) & 0xFFu );
	}
}

CMaterialEffectCommandExecutor::CMaterialEffectCommandExecutor( CGeometryCommandExecutor& InGeometry, CShaderCommandExecutor& InShader ) : Geometry( InGeometry ), Shader( InShader )
{
}

bool CMaterialEffectCommandExecutor::IsMaterialEffectCommand( EDrawCommand Type )
{
	return Type == EDrawCommand::MaterialQuad3D || Type == EDrawCommand::DistortedMaterialQuad3D || Type == EDrawCommand::MaterialRibbon3D || Type == EDrawCommand::DistortedMaterialRibbon3D;
}

void CMaterialEffectCommandExecutor::SetVertexValues( FDrawShaderVertex3D& Vertex, float32 Dissolve, float32 Emission, float32 Strength, const float32* UVOffset, const float32* DistortionOffset )
{
	Vertex.Normal[0] = Dissolve;
	Vertex.Normal[1] = Emission;
	Vertex.Normal[2] = Strength;
	Vertex.Tangent[0] = UVOffset[0];
	Vertex.Tangent[1] = UVOffset[1];
	Vertex.Binormal[0] = DistortionOffset[0];
	Vertex.Binormal[1] = DistortionOffset[1];
}

CShaderCommandExecutor::EResult CMaterialEffectCommandExecutor::Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	switch ( View.Type )
	{
	case EDrawCommand::MaterialQuad3D:
	{
		if ( View.NumDataWords != static_cast<int32>( sizeof( FDrawMaterialQuadData ) / 4 ) )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		return DrawQuad( View.Read<FDrawMaterialQuadData>(), nullptr, Bright, Alpha, bAlphaBlend ) ? CShaderCommandExecutor::EResult::Executed : CShaderCommandExecutor::EResult::Rejected;
	}
	case EDrawCommand::DistortedMaterialQuad3D:
	{
		if ( View.NumDataWords != static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		const auto Data = View.Read<FDrawDistortedMaterialQuadData>(); // 検査前の固定部。
		return DrawQuad( Data.Material, &Data, Bright, Alpha, bAlphaBlend ) ? CShaderCommandExecutor::EResult::Executed : CShaderCommandExecutor::EResult::Rejected;
	}
	case EDrawCommand::MaterialRibbon3D:
	{
		constexpr int32 DataWords = sizeof( FDrawMaterialRibbonData ) / 4; // 固定部の長さ（ワード）。
		constexpr int32 PointWords = sizeof( FDrawMaterialRibbonData::FPoint ) / 4; // 点1つの長さ（ワード）。
		if ( View.NumDataWords < DataWords )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		FDrawMaterialRibbonData Data; // 点の数を読む前に、固定部を所有コピーする。
		FMemory::Memcpy( &Data, View.Data, sizeof( Data ) );
		if ( !NDrawMaterialRibbon::IsValidHeader( Data ) || View.NumDataWords != DataWords + Data.Ribbon.NumPoints * PointWords )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		TArray<FDrawMaterialRibbonData::FPoint> Points; // 借用メモリのalignmentに頼らず、所有コピーを作る。
		Points.SetNum( Data.Ribbon.NumPoints );
		FMemory::Memcpy( Points.GetData(), View.Data + DataWords, static_cast<usize>( Data.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
		if ( !NDrawMaterialRibbon::IsValid( Data, Points.GetData() ) )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		return DrawRibbon( Data, Points.GetData(), nullptr, nullptr, Bright, Alpha, bAlphaBlend ) ? CShaderCommandExecutor::EResult::Executed : CShaderCommandExecutor::EResult::Rejected;
	}
	case EDrawCommand::DistortedMaterialRibbon3D:
	{
		constexpr int32 DataWords = sizeof( FDrawDistortedMaterialRibbonData ) / 4; // 固定部の長さ（ワード）。
		constexpr int32 PointWords = sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4; // 点1つの長さ（ワード）。
		if ( View.NumDataWords < DataWords )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		FDrawDistortedMaterialRibbonData Data; // 点の数を読む前に、固定部を所有コピーする。
		FMemory::Memcpy( &Data, View.Data, sizeof( Data ) );
		if ( !NDrawDistortedMaterialRibbon::IsValidHeader( Data ) || View.NumDataWords != DataWords + Data.Material.Ribbon.NumPoints * PointWords )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		TArray<FDrawDistortedMaterialRibbonData::FPoint> Points; // 所有コピー。
		Points.SetNum( Data.Material.Ribbon.NumPoints );
		FMemory::Memcpy( Points.GetData(), View.Data + DataWords, static_cast<usize>( Data.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) );
		if ( !NDrawDistortedMaterialRibbon::IsValid( Data, Points.GetData() ) )
		{
			return CShaderCommandExecutor::EResult::Rejected;
		}
		return DrawRibbon( Data.Material, nullptr, &Data, Points.GetData(), Bright, Alpha, bAlphaBlend ) ? CShaderCommandExecutor::EResult::Executed : CShaderCommandExecutor::EResult::Rejected;
	}
	default:
		return CShaderCommandExecutor::EResult::NotHandled;
	}
}

bool CMaterialEffectCommandExecutor::DrawQuad( const FDrawMaterialQuadData& Data, const FDrawDistortedMaterialQuadData* Distortion, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	if ( Distortion != nullptr ? !NDrawDistortedMaterialQuad::IsValid( *Distortion ) : !NDrawMaterialQuad::IsValid( Data ) )
	{
		return false;
	}
	FEffectSettings Settings;
	Settings.SourceTexture = Data.Quad.Handle;
	Settings.MaskTexture = Data.MaskTexture;
	Settings.DistortionTexture = Distortion != nullptr ? Distortion->DistortionTexture : -1;
	Settings.UVScale[0] = Data.UVScale[0];
	Settings.UVScale[1] = Data.UVScale[1];
	Settings.MaskScale = Data.MaskScale;
	Settings.EdgeWidth = Data.EdgeWidth;
	Settings.EdgeEmission = Data.EdgeEmission;
	Settings.EdgeColor = Data.EdgeColor;
	Settings.bDissolve = Data.bDissolve != 0;
	Settings.bDepthTest = Data.DepthTest != 0;
	Settings.bDepthWrite = Data.DepthWrite != 0;
	if ( Distortion != nullptr )
	{
		Settings.DistortionScale[0] = Distortion->UVScale[0];
		Settings.DistortionScale[1] = Distortion->UVScale[1];
	}
	FDrawShaderQuad3DData QuadData; // 向き・寸法・色は、ShaderQuad3D と同じ決め方で、クリップ座標つきの頂点にする。
	QuadData.Quad = Data.Quad;
	TArray<FDrawShaderVertex3D> Vertices;
	if ( !Geometry.BuildShaderQuad( QuadData, Vertices ) )
	{
		return false;
	}
	const float32 DistortionOffset[2] = { Distortion != nullptr ? Distortion->UVOffset[0] : 0.0f, Distortion != nullptr ? Distortion->UVOffset[1] : 0.0f };
	for ( FDrawShaderVertex3D& Vertex : Vertices )
	{
		SetVertexValues( Vertex, Data.Dissolve, Data.Emission, Distortion != nullptr ? Distortion->Strength : 0.0f, Data.UVOffset, DistortionOffset );
	}
	return DrawVertices( Settings, Vertices, Bright, Alpha, bAlphaBlend );
}

bool CMaterialEffectCommandExecutor::DrawRibbon( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points, const FDrawDistortedMaterialRibbonData* Distortion, const FDrawDistortedMaterialRibbonData::FPoint* DistortionPoints, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	const FDrawRibbonData& Ribbon = Data.Ribbon;
	const FCamera& Camera = Geometry.GetCamera();
	const IRenderGeometry3D* Target = Geometry.GetGeometry();
	if ( ( Points == nullptr ) == ( DistortionPoints == nullptr ) || ( Distortion == nullptr ) != ( DistortionPoints == nullptr ) || !Target || Target->GetWidth() <= 0 || Target->GetHeight() <= 0 || !Camera.IsValid() )
	{
		return false;
	}
	const auto MaterialPoint = [&]( int32 Index ) -> const FDrawMaterialRibbonData::FPoint& // 帯の形式から、材質の点を読む。
	{
		return DistortionPoints != nullptr ? DistortionPoints[Index].Material : Points[Index];
	};
	TArray<FDrawRibbonData::FPoint> PathPoints; // 共通の展開が読む、経路の点。
	PathPoints.SetNum( Ribbon.NumPoints );
	for ( int32 Index = 0; Index < Ribbon.NumPoints; ++Index )
	{
		PathPoints[Index] = MaterialPoint( Index ).Path;
	}
	FDrawRibbonCamera RibbonCamera; // 帯の幅を向けるカメラ。
	RibbonCamera.Position = Camera.Position;
	RibbonCamera.Right = Camera.GetRight();
	RibbonCamera.Up = Camera.GetUpVector();
	RibbonCamera.Front = Camera.GetForward();
	FDrawRibbonMesh Mesh; // 画像のUは、つなげたまま（折り返しは、シェーダーが行う）。
	if ( !NDrawRibbonMesh::Build( Ribbon, PathPoints.GetData(), RibbonCamera, false, Mesh ) || Mesh.Indices.Num() < 3 || Mesh.Indices.Num() > ( Ribbon.NumPoints - 1 ) * 6 )
	{
		return false;
	}
	const FCameraViewport Viewport = { Target->GetWidth(), Target->GetHeight() }; // 同じ論理画面で射影する。
	const FMatrix4x4 Transform = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ); // ワールド行列は使わない。
	TArray<FDrawShaderVertex3D> Vertices; // 番号を解決した三角形のリスト。
	Vertices.Reserve( Mesh.Indices.Num() );
	for ( const uint16 Index : Mesh.Indices )
	{
		const int32 PointIndex = Index / 2; // 左右の頂点は、同じ点から作られる。
		if ( Index >= Mesh.Vertices.Num() || PointIndex >= Ribbon.NumPoints )
		{
			return false;
		}
		const FDrawVertex3D& Source = Mesh.Vertices[Index];
		const FDrawMaterialRibbonData::FPoint& Point = MaterialPoint( PointIndex );
		FVector4 Clip = Transform.Transform( FVector4( Source.Position[0], Source.Position[1], Source.Position[2], 1 ) ); // 透視除算しない（wを渡す）。
		Clip.x += 2 * Camera.ScreenOffset.x / Viewport.Width * Clip.w;
		Clip.y -= 2 * Camera.ScreenOffset.y / Viewport.Height * Clip.w;
		FDrawShaderVertex3D Vertex;
		FMemory::Memzero( &Vertex, sizeof( Vertex ) );
		FMemory::Memcpy( Vertex.Position, Source.Position, sizeof( Vertex.Position ) );
		Vertex.SubPosition[0] = Clip.x;
		Vertex.SubPosition[1] = Clip.y;
		Vertex.SubPosition[2] = Clip.z;
		Vertex.SubPosition[3] = Clip.w;
		for ( const float32 Number : Vertex.SubPosition )
		{
			if ( !NMath::IsFinite( Number ) )
			{
				return false;
			}
		}
		SetColor( Vertex, Point.Path.Color );
		Vertex.U = Source.U;
		Vertex.V = Source.V;
		const float32 DistortionOffset[2] = { DistortionPoints != nullptr ? DistortionPoints[PointIndex].UVOffset[0] : 0.0f, DistortionPoints != nullptr ? DistortionPoints[PointIndex].UVOffset[1] : 0.0f };
		SetVertexValues( Vertex, Point.Dissolve, Point.Emission, DistortionPoints != nullptr ? DistortionPoints[PointIndex].Strength : 0.0f, Point.UVOffset, DistortionOffset );
		Vertices.Add( Vertex );
	}
	const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
	FEffectSettings Settings;
	Settings.SourceTexture = Ribbon.Texture;
	Settings.MaskTexture = Material.MaskTexture;
	Settings.DistortionTexture = Distortion != nullptr ? Distortion->DistortionTexture : -1;
	Settings.UVScale[0] = Material.UVScale[0];
	Settings.UVScale[1] = Material.UVScale[1];
	Settings.MaskScale = Material.MaskScale;
	Settings.EdgeWidth = Material.EdgeWidth;
	Settings.EdgeEmission = Material.EdgeEmission;
	Settings.EdgeColor = Material.EdgeColor;
	Settings.bDissolve = Material.bDissolve != 0;
	Settings.bDepthTest = Material.DepthTest != 0;
	Settings.bDepthWrite = Material.DepthWrite != 0;
	if ( Distortion != nullptr )
	{
		Settings.DistortionScale[0] = Distortion->UVScale[0];
		Settings.DistortionScale[1] = Distortion->UVScale[1];
	}
	return DrawVertices( Settings, Vertices, Bright, Alpha, bAlphaBlend );
}

bool CMaterialEffectCommandExecutor::DrawVertices( const FEffectSettings& Settings, const TArray<FDrawShaderVertex3D>& Vertices, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	const FGeometryDrawState& Current = Geometry.GetState();
	if ( !bAlphaBlend || ( Current.Blend != EGeometryBlend::Alpha && Current.Blend != EGeometryBlend::Add ) )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "Material effect blend mode", "Material effects are drawn only with Alpha and Add blending (the same as DxLib)" );
		return false;
	}
	if ( Vertices.IsEmpty() )
	{
		return true;
	}
	FEffectConstants Constants;
	Constants.A[0] = Settings.UVScale[0];
	Constants.A[1] = Settings.UVScale[1];
	Constants.A[2] = Settings.MaskScale;
	Constants.A[3] = Settings.MaskTexture >= 0 ? 1.0f : 0.0f;
	Constants.B[0] = Settings.bDissolve ? 1.0f : 0.0f;
	Constants.B[1] = static_cast<float32>( NMath::Clamp( Alpha, 0, 255 ) ) / 255.0f;
	Constants.B[2] = Settings.DistortionTexture >= 0 ? 1.0f : 0.0f;
	Constants.B[3] = Settings.EdgeWidth;
	const float32 EdgeAlpha = static_cast<float32>( Settings.EdgeColor.A ) / 255.0f * Settings.EdgeEmission;
	Constants.Edge[0] = static_cast<float32>( Settings.EdgeColor.R ) / 255.0f * EdgeAlpha;
	Constants.Edge[1] = static_cast<float32>( Settings.EdgeColor.G ) / 255.0f * EdgeAlpha;
	Constants.Edge[2] = static_cast<float32>( Settings.EdgeColor.B ) / 255.0f * EdgeAlpha;
	Constants.Bright[0] = static_cast<float32>( Bright.R ) / 255.0f;
	Constants.Bright[1] = static_cast<float32>( Bright.G ) / 255.0f;
	Constants.Bright[2] = static_cast<float32>( Bright.B ) / 255.0f;
	Constants.Bright[3] = 1.0f;
	Constants.Distortion[0] = Settings.DistortionScale[0];
	Constants.Distortion[1] = Settings.DistortionScale[1];
	FShaderConstantData ConstantView;
	ConstantView.Stage = static_cast<int32>( EShaderStage::Pixel );
	ConstantView.Slot = 7;
	ConstantView.Bytes = TArrayView<const uint8>( reinterpret_cast<const uint8*>( &Constants ), sizeof( Constants ) );
	FShaderDrawRequest Request;
	Request.Builtin = EBuiltInVertexShader::EffectMaterial;
	Request.BuiltinPixel = EBuiltInPixelShader::EffectMaterial;
	Request.State = Current;
	Request.State.bAlphaBlend = true;
	Request.State.bLinearFilter = true; // DxLib の材質描画は、いつも線形補間（折り返しは、シェーダーが行う）。
	Request.State.bTestDepth = true; // 深度を比べないときは、いつも通す（書くだけ）。
	Request.State.Compare = Settings.bDepthTest ? EGeometryCompare::LessEqual : EGeometryCompare::Always;
	Request.State.bWriteDepth = Settings.bDepthWrite;
	Request.Topology = EGeometryTopology::Triangles;
	Request.bUseDepth = true;
	Request.Vertices = reinterpret_cast<const uint8*>( Vertices.GetData() );
	Request.NumVertices = Vertices.Num();
	Request.Constants = TArrayView<const FShaderConstantData>( &ConstantView, 1 );
	Request.Textures[0] = Settings.SourceTexture;
	Request.Textures[1] = Settings.MaskTexture;
	Request.Textures[2] = Settings.DistortionTexture;
	return Shader.DrawBuiltIn( Request, true );
}
