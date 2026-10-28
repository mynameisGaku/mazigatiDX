#include "HelloSample.h"

#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Hello/BouncingBox.h"
#include "Source/Hello/ShapeGallery.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CHelloSample> Registrar( FSampleInfo { "Hello", "Hello: drawing basics", "Shapes and text with FImmediateDraw, and boxes that move with DeltaTime.", 10 } );

	// 跳ね返る四角形が動ける範囲（図形の並びの下、画面の端まで）
	const FVector2 AreaMin( 0.0f, 220.0f );
	const FVector2 AreaMax( 1280.0f, 720.0f );
}

void CHelloSample::OnEnter()
{
	SpawnObject<CShapeGallery>( FVector2( 80.0f, 90.0f ) );

	// 速さ・大きさ・色を変えた 3 つ。同じクラスのオブジェクトを、引数を変えて何個でも置ける
	SpawnObject<CBouncingBox>( FVector2( 200.0f, 400.0f ), FVector2( 260.0f, 190.0f ), 24, FColor32::Yellow, AreaMin, AreaMax );
	SpawnObject<CBouncingBox>( FVector2( 640.0f, 300.0f ), FVector2( -340.0f, 150.0f ), 16, FColor32::Cyan, AreaMin, AreaMax );
	SpawnObject<CBouncingBox>( FVector2( 900.0f, 600.0f ), FVector2( 120.0f, -280.0f ), 32, FColor32::Magenta, AreaMin, AreaMax );
}

void CHelloSample::OnSampleDraw( FImmediateDraw& Screen )
{
	// 動きの範囲を、枠で示す
	Screen.Box( static_cast<int32>( AreaMin.x ), static_cast<int32>( AreaMin.y ), static_cast<int32>( AreaMax.x ) - 1, static_cast<int32>( AreaMax.y ) - 1, FColor32::Gray, false );
	Screen.TextF( 16, 56, FColor32::Gray, "Scene time: %.1f s   Objects: %d", GetElapsedTime(), GetNumObjects() );
}
