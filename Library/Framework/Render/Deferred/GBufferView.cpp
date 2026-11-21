#include "GBufferView.h"

bool FGBufferView::Make( const FCamera& Camera, const FCameraViewport& Viewport, FGBufferView& OutView )
{
	if ( !Camera.IsValid() || !Viewport.IsValid() || Camera.Projection != ECameraProjection::Perspective )
	{
		return false;
	}
	const float32 Width = static_cast<float32>( Viewport.Width );
	const float32 Height = static_cast<float32>( Viewport.Height );

	// Far の面（奥行き 1）の、画面の端の位置。射影行列（FCamera::GetProjectionMatrix）と同じ規則で、逆行列を使わずに求める
	//   縦の半分 = Far x tan( 視野角 / 2 )、横の半分 = 縦の半分 x 縦横比（縦横比 = 映す先の縦横比 / 横の拡大）。
	//   画面の座標 p は、正規化した座標 (p.x - 中心.x) / (Width / 2)、-(p.y - 中心.y) / (Height / 2) になる（中心 = 画面の中心 + レンズのずらし）。
	const float32 HalfHeight = Camera.Far * NMath::Tan( Camera.FovY * 0.5f );
	const float32 HalfWidth = HalfHeight * Viewport.GetAspect() / Camera.HorizontalStretch;
	const float32 NdcLeft = -( 1.0f + 2.0f * Camera.ScreenOffset.x / Width );
	const float32 NdcTop = 1.0f + 2.0f * Camera.ScreenOffset.y / Height;
	const FVector3 Forward = Camera.GetForward();
	const FVector3 Right = Camera.GetRight();
	const FVector3 Up = Camera.GetUpVector();

	OutView.CameraPosition = FVector4( Camera.Position, Camera.Far );
	OutView.RayOrigin = FVector4( Forward * Camera.Far + Right * ( NdcLeft * HalfWidth ) + Up * ( NdcTop * HalfHeight ), 0.0f );
	OutView.RayU = FVector4( Right * ( 2.0f * HalfWidth ), 0.0f );
	OutView.RayV = FVector4( Up * ( -2.0f * HalfHeight ), 0.0f );
	return true;
}

float32 FGBufferView::ToNormalizedDepth( const FCamera& Camera, const FVector3& World )
{
	if ( !Camera.IsValid() )
	{
		return 0.0f;
	}
	return FVector3::Dot( World - Camera.Position, Camera.GetForward() ) / Camera.Far;
}

FVector3 FGBufferView::GetRayToFarPlane( const FVector2& Uv ) const
{
	return RayOrigin.XYZ() + RayU.XYZ() * Uv.x + RayV.XYZ() * Uv.y;
}

FVector3 FGBufferView::ReconstructWorldPosition( const FVector2& Uv, float32 NormalizedDepth ) const
{
	return CameraPosition.XYZ() + GetRayToFarPlane( Uv ) * NormalizedDepth;
}
