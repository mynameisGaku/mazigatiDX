// SPDX-License-Identifier: MIT
#include "DxLibAssetBackend.h"

#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibTypes.h"
#include "Library/Core/Types/Numeric.h"

//=============================================================================
// CDxLibAssetBackend のうち、モデルの構造（フレーム・メッシュ・材質・テクスチャ）と、モデル 1 体ごとの見た目（MV1 の関数）
//=============================================================================
namespace
{
	FString ToString( const char* Text )
	{
		return Text != nullptr ? FString( Text ) : FString();
	}

	bool IsFrameIndex( int32 Model, int32 Frame )
	{
		return Model >= 0 && Frame >= 0 && Frame < MV1GetFrameNum( Model );
	}

	bool IsMeshIndex( int32 Model, int32 Mesh )
	{
		return Model >= 0 && Mesh >= 0 && Mesh < MV1GetMeshNum( Model );
	}

	bool IsMaterialIndex( int32 Model, int32 Material )
	{
		return Model >= 0 && Material >= 0 && Material < MV1GetMaterialNum( Model );
	}

	// DxLibの行列をFrameworkへ返す前にfinite affine形式を確認する。
	bool IsFiniteAffine( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		constexpr float32 Tolerance = 1.0e-4f;
		return NMath::Abs( Matrix.m[0][3] ) <= Tolerance && NMath::Abs( Matrix.m[1][3] ) <= Tolerance && NMath::Abs( Matrix.m[2][3] ) <= Tolerance && NMath::Abs( Matrix.m[3][3] - 1.0f ) <= Tolerance;
	}

	FModelMaterialColors ReadMaterialColors( int32 Model, int32 Material )
	{
		FModelMaterialColors Colors;
		Colors.Diffuse = NDxLibTypes::FromDxLib( MV1GetMaterialDifColor( Model, Material ) );
		Colors.Ambient = NDxLibTypes::FromDxLib( MV1GetMaterialAmbColor( Model, Material ) );
		Colors.Specular = NDxLibTypes::FromDxLib( MV1GetMaterialSpcColor( Model, Material ) );
		Colors.SpecularPower = MV1GetMaterialSpcPower( Model, Material );
		Colors.Emissive = NDxLibTypes::FromDxLib( MV1GetMaterialEmiColor( Model, Material ) );
		return Colors;
	}
}

bool CDxLibAssetBackend::GetModelStructure( int32 Model, FModelStructure& Out )
{
	if ( Model < 0 || CheckHandleASyncLoad( Model ) != FALSE )
	{
		return false;
	}
	const int32 FrameCount = MV1GetFrameNum( Model );
	const int32 MeshCount = MV1GetMeshNum( Model );
	const int32 MaterialCount = MV1GetMaterialNum( Model );
	const int32 TextureCount = MV1GetTextureNum( Model );
	if ( FrameCount < 0 || MeshCount < 0 || MaterialCount < 0 || TextureCount < 0 )
	{
		return false;
	}

	FModelStructure Structure;
	for ( int32 Frame = 0; Frame < FrameCount; ++Frame )
	{
		FModelFrameInfo Info;
		Info.Name = ToString( MV1GetFrameName( Model, Frame ) );
		// DxLib は、親が無いとき -2 を返す
		const int32 Parent = MV1GetFrameParent( Model, Frame );
		Info.Parent = Parent >= 0 ? Parent : -1;
		const int32 ChildCount = MV1GetFrameChildNum( Model, Frame );
		for ( int32 Child = 0; Child < ChildCount; ++Child )
		{
			Info.Children.Add( MV1GetFrameChild( Model, Frame, Child ) );
		}
		const int32 FrameMeshCount = MV1GetFrameMeshNum( Model, Frame );
		for ( int32 Mesh = 0; Mesh < FrameMeshCount; ++Mesh )
		{
			Info.Meshes.Add( MV1GetFrameMesh( Model, Frame, Mesh ) );
		}
		Structure.Frames.Add( Info );
	}
	for ( int32 Mesh = 0; Mesh < MeshCount; ++Mesh )
	{
		FModelMeshInfo Info;
		Info.Material = MV1GetMeshMaterial( Model, Mesh );
		Info.VertexCount = MV1GetMeshVertexNum( Model, Mesh );
		Info.TriangleCount = MV1GetMeshTriangleNum( Model, Mesh );
		Info.BoundsMin = NDxLibTypes::FromDxLib( MV1GetMeshMinPosition( Model, Mesh ) );
		Info.BoundsMax = NDxLibTypes::FromDxLib( MV1GetMeshMaxPosition( Model, Mesh ) );
		Structure.Meshes.Add( Info );
	}
	// メッシュの持ち主のフレームは、フレームの側の一覧から逆に引く
	for ( int32 Frame = 0; Frame < Structure.Frames.Num(); ++Frame )
	{
		for ( int32 Mesh : Structure.Frames[Frame].Meshes )
		{
			if ( Mesh >= 0 && Mesh < Structure.Meshes.Num() )
			{
				Structure.Meshes[Mesh].Frame = Frame;
			}
		}
	}
	for ( int32 Material = 0; Material < MaterialCount; ++Material )
	{
		FModelMaterialInfo Info;
		Info.Name = ToString( MV1GetMaterialName( Model, Material ) );
		Info.Colors = ReadMaterialColors( Model, Material );
		const int32 Texture = MV1GetMaterialDifMapTexture( Model, Material );
		Info.DiffuseTexture = Texture >= 0 && Texture < TextureCount ? Texture : -1;
		Structure.Materials.Add( Info );
	}
	for ( int32 Texture = 0; Texture < TextureCount; ++Texture )
	{
		FModelTextureInfo Info;
		Info.Name = ToString( MV1GetTextureName( Model, Texture ) );
		Info.ColorFilePath = ToString( MV1GetTextureColorFilePath( Model, Texture ) );
		Info.Width = MV1GetTextureWidth( Model, Texture );
		Info.Height = MV1GetTextureHeight( Model, Texture );
		Structure.Textures.Add( Info );
	}
	const int32 ShapeCount = MV1GetShapeNum( Model );
	for ( int32 Shape = 0; Shape < ShapeCount; ++Shape )
	{
		Structure.Shapes.Add( ToString( MV1GetShapeName( Model, Shape ) ) );
	}
	Out = MoveTemp( Structure );
	return true;
}

bool CDxLibAssetBackend::GetModelMaterialColors( int32 Model, int32 Material, FModelMaterialColors& Out )
{
	if ( !IsMaterialIndex( Model, Material ) )
	{
		return false;
	}
	Out = ReadMaterialColors( Model, Material );
	return true;
}

bool CDxLibAssetBackend::SetModelMaterialColors( int32 Model, int32 Material, const FModelMaterialColors& Colors )
{
	if ( !IsMaterialIndex( Model, Material ) )
	{
		return false;
	}
	const bool bDiffuse = MV1SetMaterialDifColor( Model, Material, NDxLibTypes::ToDxLib( Colors.Diffuse ) ) >= 0;
	const bool bAmbient = MV1SetMaterialAmbColor( Model, Material, NDxLibTypes::ToDxLib( Colors.Ambient ) ) >= 0;
	const bool bSpecular = MV1SetMaterialSpcColor( Model, Material, NDxLibTypes::ToDxLib( Colors.Specular ) ) >= 0;
	const bool bPower = MV1SetMaterialSpcPower( Model, Material, Colors.SpecularPower ) >= 0;
	const bool bEmissive = MV1SetMaterialEmiColor( Model, Material, NDxLibTypes::ToDxLib( Colors.Emissive ) ) >= 0;
	return bDiffuse && bAmbient && bSpecular && bPower && bEmissive;
}

bool CDxLibAssetBackend::GetModelMeshVisible( int32 Model, int32 Mesh, bool& bOut )
{
	if ( !IsMeshIndex( Model, Mesh ) )
	{
		return false;
	}
	const int32 Visible = MV1GetMeshVisible( Model, Mesh );
	if ( Visible < 0 )
	{
		return false;
	}
	bOut = Visible != FALSE;
	return true;
}

bool CDxLibAssetBackend::SetModelMeshVisible( int32 Model, int32 Mesh, bool bVisible )
{
	return IsMeshIndex( Model, Mesh ) && MV1SetMeshVisible( Model, Mesh, bVisible ? TRUE : FALSE ) >= 0;
}

bool CDxLibAssetBackend::GetModelFrameVisible( int32 Model, int32 Frame, bool& bOut )
{
	if ( !IsFrameIndex( Model, Frame ) )
	{
		return false;
	}
	const int32 Visible = MV1GetFrameVisible( Model, Frame );
	if ( Visible < 0 )
	{
		return false;
	}
	bOut = Visible != FALSE;
	return true;
}

bool CDxLibAssetBackend::SetModelFrameVisible( int32 Model, int32 Frame, bool bVisible )
{
	return IsFrameIndex( Model, Frame ) && MV1SetFrameVisible( Model, Frame, bVisible ? TRUE : FALSE ) >= 0;
}

bool CDxLibAssetBackend::GetModelOpacity( int32 Model, float32& Out )
{
	if ( Model < 0 || CheckHandleASyncLoad( Model ) != FALSE )
	{
		return false;
	}
	const float32 Opacity = MV1GetOpacityRate( Model );
	if ( Opacity < 0.0f )
	{
		return false;
	}
	Out = Opacity;
	return true;
}

bool CDxLibAssetBackend::SetModelOpacity( int32 Model, float32 Opacity )
{
	return Model >= 0 && CheckHandleASyncLoad( Model ) == FALSE && MV1SetOpacityRate( Model, Opacity ) >= 0;
}

bool CDxLibAssetBackend::GetModelFrameMatrix( int32 Model, int32 Frame, EModelFrameSpace Space, FMatrix4x4& Out )
{
	if ( !IsFrameIndex( Model, Frame ) )
	{
		return false;
	}
	Out = NDxLibTypes::FromDxLib( Space == EModelFrameSpace::World ? MV1GetFrameLocalWorldMatrix( Model, Frame ) : MV1GetFrameLocalMatrix( Model, Frame ) );
	return true;
}

bool CDxLibAssetBackend::SampleModelAnimFrameLocalMatrix( int32 Model, int32 AttachIndex, int32 Frame, float32 Time, FMatrix4x4& Out )
{
	if ( !IsFrameIndex( Model, Frame ) || AttachIndex < 0 || !NMath::IsFinite( Time ) )
	{
		return false;
	}
	const float32 Length = MV1GetAttachAnimTotalTime( Model, AttachIndex );
	if ( !NMath::IsFinite( Length ) || Length <= 0.0f || Time < 0.0f || Time > Length || MV1SetAttachAnimTime( Model, AttachIndex, Time ) < 0 || MV1SetAttachAnimBlendRate( Model, AttachIndex, 1.0f ) < 0 )
	{
		return false;
	}
	const FMatrix4x4 Sampled = NDxLibTypes::FromDxLib( MV1GetFrameLocalMatrix( Model, Frame ) );
	if ( !IsFiniteAffine( Sampled ) )
	{
		return false;
	}
	Out = Sampled;
	return true;
}

bool CDxLibAssetBackend::GetModelShapeRate( int32 Model, int32 Shape, float32& Out )
{
	if ( Model < 0 || Shape < 0 || Shape >= MV1GetShapeNum( Model ) )
	{
		return false;
	}
	Out = MV1GetShapeRate( Model, Shape );
	return true;
}

bool CDxLibAssetBackend::SetModelShapeRate( int32 Model, int32 Shape, float32 Rate )
{
	return Model >= 0 && Shape >= 0 && Shape < MV1GetShapeNum( Model ) && MV1SetShapeRate( Model, Shape, Rate, DX_MV1_SHAPERATE_OVERWRITE ) >= 0;
}
