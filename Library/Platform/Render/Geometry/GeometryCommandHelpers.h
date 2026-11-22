// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"

// 3D の命令の実行が共有する、値の検査・変換・フォグの見え方。
namespace NGeometryCommand
{
	// 値の列に、非有限値がないか。
	bool AreFinite( const float32* Values, int32 Count );
	// ワールド座標や行列に非有限値がないか。
	template<usize Count>
	FORCEINLINE bool Finite( const float32 ( &Values )[Count] )
	{
		return AreFinite( Values, static_cast<int32>( Count ) );
	}
	// 共通の12要素形式からアフィン行列へ戻す。
	FMatrix4x4 ToMatrix( const FDrawMatrixData& Data );
	// 図形の座標と色を共通頂点へ移す。
	FDrawVertex3D Vertex( const float32 ( &Position )[3], FDrawRgb Color );
	// 図形の鏡面色を、全頂点へ付ける（光の計算で使う）。
	void SetSpecular( TArray<FDrawVertex3D>& Vertices, FDrawRgb Specular );
	// 3成分の色の命令の値を、色にする。
	FColor ToColor( const float32 ( &Values )[3] );
	// 距離 Depth（カメラから見た奥行き）での、フォグに隠れない割合（1 = 隠れない、0 = 完全にフォグの色）。
	float32 GetFogVisibility( const FDrawFogData& Fog, float32 Depth );
	// 高さ Height（ワールドの Y）での、フォグに隠れない割合。Exponential / ExponentialSquared は、Start より下だけ、低いほど濃い。Linear は、Start で 0、End で 1 の濃さ。
	float32 GetHeightFogVisibility( const FDrawFogData& Fog, float32 Height );
	// 0..1 の色の成分を、0..255 に丸める（範囲の外は端へ）。
	int32 ToByte( float32 Value );
}
