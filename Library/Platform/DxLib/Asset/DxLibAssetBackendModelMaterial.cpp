// SPDX-License-Identifier: MIT
#include "DxLibAssetBackendSubsystem.h"
#include "Library/Framework/Render/Mesh/ModelMaterialGeometry.h"


#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

void CDxLibAssetBackendSubsystem::Deinitialize()
{
	ModelMaterialGeometries.Empty();
	NextModelMaterialGeometry = 1;
	CAppSubsystem::Deinitialize();
}

int32 CDxLibAssetBackendSubsystem::CreateModelMaterialGeometry( const FStaticModelSurfaceData& Surface, int32 MaterialIndex )
{
	// 登録前に完成させ、失敗時はbackend mapを変更しないimmutable geometry。
	TSharedPtr<FDxLibModelMaterialGeometry> MutableGeometry = MakeShared<FDxLibModelMaterialGeometry>();
	if ( NextModelMaterialGeometry <= 0 || !NModelMaterialGeometry::Build( Surface, MaterialIndex, *MutableGeometry ) )
	{
		return -1;
	}
	// map登録後に変更しない共有参照。
	TSharedPtr<const FDxLibModelMaterialGeometry> Geometry = MutableGeometry;
	// 登録済みhandleとの衝突を避ける番号。
	const int32 Handle = NextModelMaterialGeometry;
	if ( ModelMaterialGeometries.Contains( Handle ) )
	{
		return -1;
	}
	ModelMaterialGeometries.Add( Handle, MoveTemp( Geometry ) );
	NextModelMaterialGeometry = Handle == TNumericLimits<int32>::Max() ? -1 : Handle + 1;
	return Handle;
}

void CDxLibAssetBackendSubsystem::DeleteModelMaterialGeometry( int32 Geometry )
{
	if ( Geometry >= 0 )
	{
		ModelMaterialGeometries.Remove( Geometry );
	}
}

const FDxLibModelMaterialGeometry* CDxLibAssetBackendSubsystem::FindModelMaterialGeometry( int32 Geometry ) const
{
	// handleから読み取る不変geometry共有参照。
	const TSharedPtr<const FDxLibModelMaterialGeometry>* Found = ModelMaterialGeometries.Find( Geometry );
	return Found != nullptr ? Found->Get() : nullptr;
}

int32 CDxLibAssetBackendSubsystem::GetNumModelMaterialGeometries() const
{
	return ModelMaterialGeometries.Num();
}
