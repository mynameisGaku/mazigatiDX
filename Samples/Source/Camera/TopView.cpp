#include "TopView.h"

#include "Source/Camera/Scenery.h"

namespace
{
	// 上から見る図のカメラ: 真上（高さ 60）から、原点を見下ろす正射影。映す範囲は、縦 80
	constexpr float32 TopHeight = 60.0f;
	constexpr float32 TopOrthoHeight = 80.0f;

	// 光線が地面にあたらない（空を向いている）ときの、地面の点として使う、カメラからの距離
	constexpr float32 MaxFootprintDistance = 70.0f;

	// 地面（y = 0）にあたる点。あたらなければ、水平方向へ MaxFootprintDistance だけ進んだ点
	FVector3 IntersectGround( const FCameraRay& Ray )
	{
		if ( Ray.Direction.y < -1.0e-4f )
		{
			const float32 Distance = -Ray.Origin.y / Ray.Direction.y;
			const FVector3 Hit = Ray.Origin + Ray.Direction * Distance;
			// 遠すぎる点（水平線の近く）は、MaxFootprintDistance で切る
			const FVector3 Flat( Hit.x - Ray.Origin.x, 0.0f, Hit.z - Ray.Origin.z );
			if ( Flat.Length() <= MaxFootprintDistance )
			{
				return FVector3( Hit.x, 0.05f, Hit.z );
			}
		}
		const FVector3 Horizontal = FVector3( Ray.Direction.x, 0.0f, Ray.Direction.z ).Normalized();
		return FVector3( Ray.Origin.x + Horizontal.x * MaxFootprintDistance, 0.05f, Ray.Origin.z + Horizontal.z * MaxFootprintDistance );
	}
}

CTopView::CTopView( const TSharedPtr<FCameraDemoState>& InState, const TSharedPtr<CRenderTarget>& InTarget )
	: State( InState )
	, Target( InTarget )
{
}

void CTopView::RecordFootprint( FDrawContext& CommandList ) const
{
	// メインの 3D を見せる範囲の四隅（画面の座標）
	const FVector2 Corners[4] = {
		FVector2( static_cast<float32>( FCameraDemoState::ViewLeft ), static_cast<float32>( FCameraDemoState::ViewTop ) ),
		FVector2( static_cast<float32>( FCameraDemoState::ViewLeft + FCameraDemoState::ViewWidth ), static_cast<float32>( FCameraDemoState::ViewTop ) ),
		FVector2( static_cast<float32>( FCameraDemoState::ViewLeft + FCameraDemoState::ViewWidth ), static_cast<float32>( FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight ) ),
		FVector2( static_cast<float32>( FCameraDemoState::ViewLeft ), static_cast<float32>( FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight ) ),
	};
	const FColor32 Yellow( 255, 224, 80 );
	FVector3 Points[4];
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		FCameraRay Ray;
		State->Camera.ScreenToRay( State->GetScreenViewport(), Corners[Index], Ray );
		Points[Index] = IntersectGround( Ray );
	}
	const FVector3 CameraGround( State->Camera.Position.x, 0.05f, State->Camera.Position.z );
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		CommandList.Line3D( Points[Index], Points[( Index + 1 ) % 4], Yellow );
		CommandList.Line3D( CameraGround, Points[Index], FColor32( 160, 140, 60 ) );
	}
}

void CTopView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Target.IsValid() )
	{
		return;
	}
	const FCameraViewport TopViewport { FCameraDemoState::TopViewSize, FCameraDemoState::TopViewSize };
	FCamera Top = FCamera::MakeOrthographic( FVector3( 0.0f, TopHeight, 0.0f ), FVector3::Zero, TopOrthoHeight );
	// 画面の上が +Z、右が +X（地図の向き）になるように、上の向きを +Z にする
	Top.Up = FVector3::Forward;
	Top.Near = 1.0f;
	Top.Far = 200.0f;

	// 別の描画先へ描く（描画先の大きさで、カメラの光軸が通る点が決まる）
	{
		FTargetScope TargetScope( CommandList, Target );
		CommandList.Clear( FColor32( 16, 20, 34 ) );
		{
			FCameraScope CameraScope( CommandList, Top );
			NScenery::RecordGround( CommandList );
			NScenery::RecordObjects( CommandList, State->Objects, nullptr, State->HoveredIndex );
			RecordFootprint( CommandList );
		}
		// 物の印（2D）。メインのカメラの視錐台に入っていれば緑、出ていれば赤
		for ( int32 Index = 0; Index < State->Objects.Num(); ++Index )
		{
			FVector2 Pixel;
			if ( Top.WorldToScreen( TopViewport, State->Objects[Index].Position, Pixel ) )
			{
				const bool bVisible = State->Visible.IsValidIndex( Index ) && State->Visible[Index];
				CommandList.Circle( NMath::RoundToInt( Pixel.x ), NMath::RoundToInt( Pixel.y ), 5, bVisible ? FColor32( 80, 230, 110 ) : FColor32( 235, 80, 80 ), false, 2 );
			}
		}
	}

	// ふつうの画像として、右下に貼る
	const int32 Left = FCameraDemoState::TopViewLeft;
	const int32 Top2D = FCameraDemoState::TopViewTop;
	CommandList.Image( static_cast<float32>( Left ), static_cast<float32>( Top2D ), Target );
	CommandList.Box( Left - 1, Top2D - 1, Left + FCameraDemoState::TopViewSize + 1, Top2D + FCameraDemoState::TopViewSize + 1, FColor32( 200, 204, 220 ), false );
	CommandList.Text( Left, Top2D - 20, "Top view (2nd camera)", FColor32::Gray );
}
