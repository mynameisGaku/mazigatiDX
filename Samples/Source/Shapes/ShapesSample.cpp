#include "ShapesSample.h"

#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Shapes/ShapeShowcase.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CShapesSample> Registrar( FSampleInfo { "Shapes", "Shapes: plain and smooth", "2D shapes with integer coordinates, and the smooth (anti-aliased) ones with float coordinates.", 15 } );

	// 左端の図形の中心。上の列が整数の座標の図形、下の列がなめらかな図形
	const FVector2 PlainOrigin( 110.0f, 190.0f );
	const FVector2 SmoothOrigin( 110.0f, 450.0f );
}

void CShapesSample::OnEnter()
{
	// 同じクラスのオブジェクトを、なめらかかどうかだけ変えて 2 つ置く。動きは同じ
	SpawnObject<CShapeShowcase>( PlainOrigin, false );
	SpawnObject<CShapeShowcase>( SmoothOrigin, true );
}

void CShapesSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Plain: integer coordinates. The edges are rough, and a slow motion jumps pixel by pixel.", FColor32::Gray );
	Screen.Text( 16, 316, "Smooth: float coordinates + anti-aliasing (the names end with AA). The edges and the motion are smooth.", FColor32::Gray );
}
