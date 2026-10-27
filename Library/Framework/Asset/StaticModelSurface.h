// SPDX-License-Identifier: MIT
#ifndef LIBRARY_FRAMEWORK_ASSET_STATICMODELSURFACE_H
#define LIBRARY_FRAMEWORK_ASSET_STATICMODELSURFACE_H

#include "Library/Core/Interfaces/Asset/StaticModelSurfaceData.h"
#include "Library/Core/Memory/SharedPtr.h"

class CModel;

// 静的モデル面の不変な値写しと、借用画像handleを有効に保つ元モデルを共有所有する。
class FStaticModelSurface
{
public:
	// 成功したbackend取得値と、その画像handleを所有する元モデルを保持する。
	FStaticModelSurface( const TSharedPtr<const CModel>& InSource, FStaticModelSurfaceData&& InData );

	// 変更できない面データを返す。
	FORCEINLINE const FStaticModelSurfaceData& GetData() const { return Data; }
	// 借用画像handleの寿命を支える元モデルを返す。
	FORCEINLINE const TSharedPtr<const CModel>& GetSource() const { return Source; }

private:
	TSharedPtr<const CModel> Source;  // Data内の借用画像handleを生かす元モデル
	FStaticModelSurfaceData Data;     // 読み込み後に変更しない面データ
};

#endif
