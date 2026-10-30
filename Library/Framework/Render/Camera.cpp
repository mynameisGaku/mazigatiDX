#include "Camera.h"

namespace
{
	// 4 つの数が、すべて有限か
	FORCEINLINE bool AllFinite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}

	// 長さ 1 の面にする（法線の長さが 0 なら、そのまま）
	FFrustumPlane MakePlane( float32 A, float32 B, float32 C, float32 D )
	{
		const float32 Length = NMath::Sqrt( A * A + B * B + C * C );
		FFrustumPlane Plane;
		if ( Length > 0.0f )
		{
			Plane.Normal = FVector3( A / Length, B / Length, C / Length );
			Plane.Distance = D / Length;
		}
		return Plane;
	}

	// 行列 M の要素（行ベクトルの規約。同次座標 (x, y, z, 1) に掛けると、列ごとに、クリップ座標の X・Y・Z・W になる）
	FORCEINLINE float32 Element( const FMatrix4x4& M, int32 Row, int32 Column )
	{
		return M.m[Row][Column];
	}
}

bool FFrustum::ContainsPoint( const FVector3& Point ) const
{
	for ( const FFrustumPlane& Plane : Planes )
	{
		if ( Plane.SignedDistance( Point ) < 0.0f )
		{
			return false;
		}
	}
	return true;
}

bool FFrustum::IntersectsSphere( const FVector3& Center, float32 Radius ) const
{
	for ( const FFrustumPlane& Plane : Planes )
	{
		if ( Plane.SignedDistance( Center ) < -Radius )
		{
			return false;
		}
	}
	return true;
}

bool FFrustum::IntersectsBox( const FVector3& Min, const FVector3& Max ) const
{
	for ( const FFrustumPlane& Plane : Planes )
	{
		// 面の法線の向きに、いちばん奥の角。これが面の外なら、箱の全体が、面の外
		const FVector3 Corner( Plane.Normal.x >= 0.0f ? Max.x : Min.x, Plane.Normal.y >= 0.0f ? Max.y : Min.y, Plane.Normal.z >= 0.0f ? Max.z : Min.z );
		if ( Plane.SignedDistance( Corner ) < 0.0f )
		{
			return false;
		}
	}
	return true;
}

FCamera FCamera::MakePerspective( const FVector3& InPosition, const FVector3& InTarget, float32 InFovY )
{
	FCamera Camera;
	Camera.Position = InPosition;
	Camera.Target = InTarget;
	Camera.Projection = ECameraProjection::Perspective;
	Camera.FovY = InFovY;
	return Camera;
}

FCamera FCamera::MakeOrthographic( const FVector3& InPosition, const FVector3& InTarget, float32 InOrthoHeight )
{
	FCamera Camera;
	Camera.Position = InPosition;
	Camera.Target = InTarget;
	Camera.Projection = ECameraProjection::Orthographic;
	Camera.OrthoHeight = InOrthoHeight;
	return Camera;
}

bool FCamera::IsValid() const
{
	if ( !AllFinite( Position ) || !AllFinite( Target ) || !AllFinite( Up ) || !NMath::IsFinite( FovY ) || !NMath::IsFinite( OrthoHeight ) || !NMath::IsFinite( Near ) || !NMath::IsFinite( Far ) || !NMath::IsFinite( HorizontalStretch ) || !NMath::IsFinite( ScreenOffset.x ) || !NMath::IsFinite( ScreenOffset.y ) )
	{
		return false;
	}
	if ( Near <= 0.0f || Far <= Near || HorizontalStretch <= 0.0f )
	{
		return false;
	}
	if ( Projection == ECameraProjection::Perspective )
	{
		// 視野角が π に近いと、射影が定まらない
		if ( FovY <= 0.0f || FovY >= NMath::PI - 0.001f )
		{
			return false;
		}
	}
	else if ( OrthoHeight <= 0.0f )
	{
		return false;
	}
	const FVector3 Forward = Target - Position;
	if ( Forward.LengthSquared() <= 1.0e-12f || Up.LengthSquared() <= 1.0e-12f )
	{
		return false;
	}
	// 上の向きが、見る向きと（ほとんど）平行だと、右の向きが定まらない
	return FVector3::Cross( Up.Normalized(), Forward.Normalized() ).LengthSquared() > 1.0e-8f;
}

FVector3 FCamera::GetForward() const
{
	return ( Target - Position ).Normalized();
}

FVector3 FCamera::GetRight() const
{
	return FVector3::Cross( Up, GetForward() ).Normalized();
}

FVector3 FCamera::GetUpVector() const
{
	return FVector3::Cross( GetForward(), GetRight() );
}

FMatrix4x4 FCamera::GetViewMatrix() const
{
	return FMatrix4x4::LookAt( Position, Target, Up );
}

FMatrix4x4 FCamera::GetProjectionMatrix( const FCameraViewport& Viewport ) const
{
	// 横の拡大率が大きいほど、横の拡大（射影行列の m00）が大きくなる。縦横比で割るので、縦横比を小さくするのと同じ
	const float32 Aspect = Viewport.GetAspect() / HorizontalStretch;
	if ( Projection == ECameraProjection::Perspective )
	{
		return FMatrix4x4::Perspective( FovY, Aspect, Near, Far );
	}
	return FMatrix4x4::Orthographic( OrthoHeight * Aspect, OrthoHeight, Near, Far );
}

bool FCamera::WorldToScreen( const FCameraViewport& Viewport, const FVector3& World, FVector2& OutPixel, float32* OutDepth ) const
{
	if ( !IsValid() || !Viewport.IsValid() || !AllFinite( World ) )
	{
		return false;
	}
	const FMatrix4x4 ViewProjection = GetViewMatrix() * GetProjectionMatrix( Viewport );
	const float32 ClipX = World.x * Element( ViewProjection, 0, 0 ) + World.y * Element( ViewProjection, 1, 0 ) + World.z * Element( ViewProjection, 2, 0 ) + Element( ViewProjection, 3, 0 );
	const float32 ClipY = World.x * Element( ViewProjection, 0, 1 ) + World.y * Element( ViewProjection, 1, 1 ) + World.z * Element( ViewProjection, 2, 1 ) + Element( ViewProjection, 3, 1 );
	const float32 ClipZ = World.x * Element( ViewProjection, 0, 2 ) + World.y * Element( ViewProjection, 1, 2 ) + World.z * Element( ViewProjection, 2, 2 ) + Element( ViewProjection, 3, 2 );
	const float32 ClipW = World.x * Element( ViewProjection, 0, 3 ) + World.y * Element( ViewProjection, 1, 3 ) + World.z * Element( ViewProjection, 2, 3 ) + Element( ViewProjection, 3, 3 );
	if ( ClipW <= 0.0f )
	{
		return false;
	}
	const float32 Depth = ClipZ / ClipW;
	if ( Depth < 0.0f || Depth > 1.0f )
	{
		return false;
	}
	const float32 NdcX = ClipX / ClipW;
	const float32 NdcY = ClipY / ClipW;
	const float32 CenterX = static_cast<float32>( Viewport.Width ) * 0.5f + ScreenOffset.x;
	const float32 CenterY = static_cast<float32>( Viewport.Height ) * 0.5f + ScreenOffset.y;
	OutPixel = FVector2( CenterX + NdcX * static_cast<float32>( Viewport.Width ) * 0.5f, CenterY - NdcY * static_cast<float32>( Viewport.Height ) * 0.5f );
	if ( OutDepth != nullptr )
	{
		*OutDepth = Depth;
	}
	return true;
}

bool FCamera::ScreenToWorld( const FCameraViewport& Viewport, const FVector2& Pixel, float32 Depth, FVector3& OutWorld ) const
{
	if ( !IsValid() || !Viewport.IsValid() || !NMath::IsFinite( Pixel.x ) || !NMath::IsFinite( Pixel.y ) || !NMath::IsFinite( Depth ) )
	{
		return false;
	}
	FMatrix4x4 InverseViewProjection;
	if ( !( GetViewMatrix() * GetProjectionMatrix( Viewport ) ).TryInverse( InverseViewProjection ) )
	{
		return false;
	}
	const float32 CenterX = static_cast<float32>( Viewport.Width ) * 0.5f + ScreenOffset.x;
	const float32 CenterY = static_cast<float32>( Viewport.Height ) * 0.5f + ScreenOffset.y;
	const float32 NdcX = ( Pixel.x - CenterX ) / ( static_cast<float32>( Viewport.Width ) * 0.5f );
	const float32 NdcY = -( Pixel.y - CenterY ) / ( static_cast<float32>( Viewport.Height ) * 0.5f );
	// (NdcX, NdcY, Depth, 1) を、逆行列で、世界へ戻す（同次座標。最後に W で割る）
	const FMatrix4x4& M = InverseViewProjection;
	const float32 X = NdcX * M.m[0][0] + NdcY * M.m[1][0] + Depth * M.m[2][0] + M.m[3][0];
	const float32 Y = NdcX * M.m[0][1] + NdcY * M.m[1][1] + Depth * M.m[2][1] + M.m[3][1];
	const float32 Z = NdcX * M.m[0][2] + NdcY * M.m[1][2] + Depth * M.m[2][2] + M.m[3][2];
	const float32 W = NdcX * M.m[0][3] + NdcY * M.m[1][3] + Depth * M.m[2][3] + M.m[3][3];
	if ( NMath::Abs( W ) <= 1.0e-12f )
	{
		return false;
	}
	OutWorld = FVector3( X / W, Y / W, Z / W );
	return true;
}

bool FCamera::ScreenToRay( const FCameraViewport& Viewport, const FVector2& Pixel, FCameraRay& OutRay ) const
{
	FVector3 NearPoint;
	FVector3 FarPoint;
	if ( !ScreenToWorld( Viewport, Pixel, 0.0f, NearPoint ) || !ScreenToWorld( Viewport, Pixel, 1.0f, FarPoint ) )
	{
		OutRay.Origin = Position;
		OutRay.Direction = ( Target - Position ).Normalized();
		return false;
	}
	OutRay.Origin = NearPoint;
	OutRay.Direction = ( FarPoint - NearPoint ).Normalized();
	return true;
}

FFrustum FCamera::GetFrustum( const FCameraViewport& Viewport ) const
{
	FFrustum Frustum;
	if ( !IsValid() || !Viewport.IsValid() )
	{
		// 空の範囲（どの点も、どの球・箱も、重ならない）
		for ( FFrustumPlane& Plane : Frustum.Planes )
		{
			Plane.Normal = FVector3::Forward;
			Plane.Distance = -1.0e30f;
		}
		return Frustum;
	}
	// ビュー射影行列の列から、6 枚の面を作る。画面に見える範囲は、クリップ座標の X が Left0 * W 以上 Right0 * W 以下、Y が Bottom0 * W 以上 Top0 * W 以下、Z が 0 以上 W 以下
	// （光軸が通る点をずらす ScreenOffset の分だけ、左右・上下の範囲が、片側へ寄る。ずらさなければ、-1 と 1）
	const float32 OffsetX = ScreenOffset.x / ( static_cast<float32>( Viewport.Width ) * 0.5f );
	const float32 OffsetY = ScreenOffset.y / ( static_cast<float32>( Viewport.Height ) * 0.5f );
	const float32 LeftBound = -1.0f - OffsetX;
	const float32 RightBound = 1.0f - OffsetX;
	const float32 BottomBound = -1.0f + OffsetY;
	const float32 TopBound = 1.0f + OffsetY;
	const FMatrix4x4 M = GetViewMatrix() * GetProjectionMatrix( Viewport );
	const auto Column = [&]( int32 Index, float32& OutA, float32& OutB, float32& OutC, float32& OutD )
	{
		OutA = Element( M, 0, Index );
		OutB = Element( M, 1, Index );
		OutC = Element( M, 2, Index );
		OutD = Element( M, 3, Index );
	};
	float32 X0, X1, X2, X3;
	float32 Y0, Y1, Y2, Y3;
	float32 Z0, Z1, Z2, Z3;
	float32 W0, W1, W2, W3;
	Column( 0, X0, X1, X2, X3 );
	Column( 1, Y0, Y1, Y2, Y3 );
	Column( 2, Z0, Z1, Z2, Z3 );
	Column( 3, W0, W1, W2, W3 );
	Frustum.Planes[FFrustum::Left] = MakePlane( X0 - LeftBound * W0, X1 - LeftBound * W1, X2 - LeftBound * W2, X3 - LeftBound * W3 );
	Frustum.Planes[FFrustum::Right] = MakePlane( RightBound * W0 - X0, RightBound * W1 - X1, RightBound * W2 - X2, RightBound * W3 - X3 );
	Frustum.Planes[FFrustum::Bottom] = MakePlane( Y0 - BottomBound * W0, Y1 - BottomBound * W1, Y2 - BottomBound * W2, Y3 - BottomBound * W3 );
	Frustum.Planes[FFrustum::Top] = MakePlane( TopBound * W0 - Y0, TopBound * W1 - Y1, TopBound * W2 - Y2, TopBound * W3 - Y3 );
	Frustum.Planes[FFrustum::Near] = MakePlane( Z0, Z1, Z2, Z3 );
	Frustum.Planes[FFrustum::Far] = MakePlane( W0 - Z0, W1 - Z1, W2 - Z2, W3 - Z3 );
	return Frustum;
}
