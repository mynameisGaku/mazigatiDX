// SPDX-License-Identifier: MIT
#include "ShadowMapView.h"

FShadowMapView FShadowMapView::MakeForCameraView( const FVector3& InLightDirection, const FCamera& Camera, const FCameraViewport& Viewport, float32 ShadowDistance, float32 MinHeight, float32 MaxHeight, float32 Margin )
{
	const FShadowMapView Fallback = MakeFitToCamera( InLightDirection );
	// カメラが使えない・画面の大きさが 0 のときは、下の ScreenToRay が false を返すので、ここでは見ない
	if ( !NMath::IsFinite( ShadowDistance ) || !NMath::IsFinite( MinHeight ) || !NMath::IsFinite( MaxHeight ) || !NMath::IsFinite( Margin ) || ShadowDistance <= Camera.Near || MinHeight >= MaxHeight || Margin < 0.0f )
	{
		return Fallback;
	}
	const float32 FarDistance = NMath::Min( ShadowDistance, Camera.Far );
	const FVector3 Forward = Camera.GetForward();

	// 画面の 4 隅を通る光線の上で、手前（Near）と奥（FarDistance）の点を求めて、横と奥行きの範囲を囲む（奥行きは、見る向きに沿った距離）
	const float32 Width = static_cast<float32>( Viewport.Width );
	const float32 Height = static_cast<float32>( Viewport.Height );
	const FVector2 Corners[4] = { FVector2( 0.0f, 0.0f ), FVector2( Width, 0.0f ), FVector2( 0.0f, Height ), FVector2( Width, Height ) };
	FVector3 Min( 0.0f, 0.0f, 0.0f );
	FVector3 Max( 0.0f, 0.0f, 0.0f );
	bool bFirst = true;
	for ( const FVector2& Corner : Corners )
	{
		FCameraRay Ray;
		if ( !Camera.ScreenToRay( Viewport, Corner, Ray ) )
		{
			return Fallback;
		}
		// 使えるカメラの光線は、いつも見る向きの側を向く（Along > 0）
		const float32 Along = FVector3::Dot( Ray.Direction, Forward );
		const FVector3 Points[2] = { Ray.Origin, Ray.Origin + Ray.Direction * ( ( FarDistance - Camera.Near ) / Along ) };
		for ( const FVector3& Point : Points )
		{
			if ( bFirst )
			{
				Min = Point;
				Max = Point;
				bFirst = false;
				continue;
			}
			Min = FVector3( NMath::Min( Min.x, Point.x ), NMath::Min( Min.y, Point.y ), NMath::Min( Min.z, Point.z ) );
			Max = FVector3( NMath::Max( Max.x, Point.x ), NMath::Max( Max.y, Point.y ), NMath::Max( Max.z, Point.z ) );
		}
	}
	return MakeForArea( InLightDirection, FVector3( Min.x - Margin, MinHeight, Min.z - Margin ), FVector3( Max.x + Margin, MaxHeight, Max.z + Margin ) );
}
