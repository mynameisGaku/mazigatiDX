#include "ShapeGallery.h"

#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// 図形 1 つぶんの幅（ピクセル）。ラベルが重ならない間隔
	constexpr int32 CellWidth = 150;
	// 図形の大きさ（ピクセル）
	constexpr int32 ShapeSize = 64;
}

CShapeGallery::CShapeGallery( const FVector2& InOrigin )
	: Origin( InOrigin )
{}

void CShapeGallery::Draw( FDrawContext& CommandList )
{
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	const int32 LabelY = Y + ShapeSize + 8;

	// 四角（塗りつぶし / 枠だけ）
	CommandList.Box( X, Y, X + ShapeSize, Y + ShapeSize, FColor32::Red );
	CommandList.Text( X, LabelY, "Box (fill)", FColor32::White );
	CommandList.Box( X + CellWidth, Y, X + CellWidth + ShapeSize, Y + ShapeSize, FColor32::Orange, false, 4 );
	CommandList.Text( X + CellWidth, LabelY, "Box (frame)", FColor32::White );

	// 円（塗りつぶし / 枠だけ）
	CommandList.Circle( X + CellWidth * 2 + ShapeSize / 2, Y + ShapeSize / 2, ShapeSize / 2, FColor32::Green );
	CommandList.Text( X + CellWidth * 2, LabelY, "Circle (fill)", FColor32::White );
	CommandList.Circle( X + CellWidth * 3 + ShapeSize / 2, Y + ShapeSize / 2, ShapeSize / 2, FColor32::Cyan, false, 3 );
	CommandList.Text( X + CellWidth * 3, LabelY, "Circle (frame)", FColor32::White );

	// 線と、文字
	CommandList.Line( X + CellWidth * 4, Y, X + CellWidth * 4 + ShapeSize, Y + ShapeSize, FColor32::Magenta, 5 );
	CommandList.Text( X + CellWidth * 4, LabelY, "Line", FColor32::White );
	CommandList.Text( X + CellWidth * 5, Y + ShapeSize / 2, "Text", FColor32::Yellow );
	CommandList.Text( X + CellWidth * 5, LabelY, "Text", FColor32::White );
}
