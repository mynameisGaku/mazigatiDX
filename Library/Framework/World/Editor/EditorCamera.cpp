#include "EditorCamera.h"


#include "Library/Core/Types/Numeric.h"

FVector3 FEditorCamera::GetEye() const
{
	return Target - GetForward() * Distance;
}

FVector3 FEditorCamera::GetForward() const
{
	const float32 CosPitch = NMath::Cos( Pitch );
	return FVector3( -CosPitch * NMath::Sin( Yaw ), -NMath::Sin( Pitch ), CosPitch * NMath::Cos( Yaw ) );
}

FVector3 FEditorCamera::GetRight() const
{
	// 左手系: 右 = 上 x 前。Pitch は MaxPitch までなので、上と前が平行にならない
	return FVector3::Cross( FVector3::Up, GetForward() ).Normalized();
}

FVector3 FEditorCamera::GetUp() const
{
	const FVector3 Forward = GetForward();
	return FVector3::Cross( Forward, GetRight() ).Normalized();
}

void FEditorCamera::Orbit( float32 DeltaX, float32 DeltaY, float32 RadiansPerPixel )
{
	Yaw -= DeltaX * RadiansPerPixel;
	Pitch = NMath::Clamp( Pitch + DeltaY * RadiansPerPixel, -MaxPitch, MaxPitch );
}

void FEditorCamera::LookAround( float32 DeltaX, float32 DeltaY, float32 RadiansPerPixel )
{
	const FVector3 Eye = GetEye();
	Yaw -= DeltaX * RadiansPerPixel;
	Pitch = NMath::Clamp( Pitch + DeltaY * RadiansPerPixel, -MaxPitch, MaxPitch );
	Target = Eye + GetForward() * Distance;   // 視点を動かさない
}

void FEditorCamera::Pan( const FEditorViewport& Viewport, float32 DeltaX, float32 DeltaY )
{
	const float32 PerPixel = GetPixelSize( Viewport, Target );
	Target = Target - GetRight() * ( DeltaX * PerPixel ) + GetUp() * ( DeltaY * PerPixel );
}

void FEditorCamera::Zoom( float32 WheelSteps, float32 Rate )
{
	Distance = NMath::Clamp( Distance * NMath::Pow( 1.0f - Rate, WheelSteps ), MinDistance, MaxDistance );
}

void FEditorCamera::MoveLocal( const FVector3& Move )
{
	Target = Target + GetRight() * Move.x + GetUp() * Move.y + GetForward() * Move.z;
}

void FEditorCamera::Focus( const FVector3& Center, float32 Radius, const FEditorViewport& Viewport )
{
	// 縦と横の、狭いほうの視野に収める
	const float32 HalfFovY = Viewport.FovY * 0.5f;
	const float32 HalfFovX = NMath::Atan( NMath::Tan( HalfFovY ) * Viewport.GetAspect() );
	const float32 HalfFov = NMath::Min( HalfFovY, HalfFovX );
	const float32 Fit = NMath::Max( Radius, 0.01f ) / NMath::Sin( NMath::Max( HalfFov, 0.01f ) ) * 1.15f;
	Target = Center;
	Distance = NMath::Clamp( Fit, MinDistance, MaxDistance );
}

FMatrix4x4 FEditorCamera::GetViewMatrix() const
{
	return FMatrix4x4::LookAt( GetEye(), Target, FVector3::Up );
}

FMatrix4x4 FEditorCamera::GetProjectionMatrix( const FEditorViewport& Viewport ) const
{
	return FMatrix4x4::Perspective( Viewport.FovY, Viewport.GetAspect(), Viewport.Near, Viewport.Far );
}

bool FEditorCamera::WorldToScreen( const FEditorViewport& Viewport, const FVector3& Point, FVector2& OutPixel, float32* OutDepth ) const
{
	if ( !Viewport.IsValid() ) return false;
	const FVector3 Offset = Point - GetEye();
	const float32 Depth = FVector3::Dot( Offset, GetForward() );
	if ( Depth <= Viewport.Near ) return false;

	const float32 TanHalf = NMath::Tan( Viewport.FovY * 0.5f );
	const float32 NdcX = FVector3::Dot( Offset, GetRight() ) / ( Depth * TanHalf * Viewport.GetAspect() );
	const float32 NdcY = FVector3::Dot( Offset, GetUp() ) / ( Depth * TanHalf );
	OutPixel = FVector2( ( NdcX * 0.5f + 0.5f ) * static_cast<float32>( Viewport.Width ), ( 0.5f - NdcY * 0.5f ) * static_cast<float32>( Viewport.Height ) );
	if ( OutDepth != nullptr ) *OutDepth = Depth;
	return true;
}

bool FEditorCamera::ProjectSegment( const FEditorViewport& Viewport, const FVector3& A, const FVector3& B, FVector2& OutPixelA, FVector2& OutPixelB ) const
{
	if ( !Viewport.IsValid() ) return false;
	const FVector3 Eye = GetEye();
	const FVector3 Forward = GetForward();
	float32 DepthA = FVector3::Dot( A - Eye, Forward );
	float32 DepthB = FVector3::Dot( B - Eye, Forward );
	const float32 Near = Viewport.Near * 1.01f;   // 近い面ぎりぎりの点は、投影が不安定なので少し奥で切る
	if ( DepthA <= Near && DepthB <= Near ) return false;

	FVector3 ClippedA = A;
	FVector3 ClippedB = B;
	if ( DepthA <= Near ) ClippedA = A + ( B - A ) * ( ( Near - DepthA ) / ( DepthB - DepthA ) );
	else if ( DepthB <= Near ) ClippedB = B + ( A - B ) * ( ( Near - DepthB ) / ( DepthA - DepthB ) );
	return WorldToScreen( Viewport, ClippedA, OutPixelA ) && WorldToScreen( Viewport, ClippedB, OutPixelB );
}

FEditorRay FEditorCamera::ScreenToRay( const FEditorViewport& Viewport, float32 PixelX, float32 PixelY ) const
{
	const float32 TanHalf = NMath::Tan( Viewport.FovY * 0.5f );
	const float32 NdcX = ( PixelX / static_cast<float32>( NMath::Max( Viewport.Width, 1 ) ) ) * 2.0f - 1.0f;
	const float32 NdcY = 1.0f - ( PixelY / static_cast<float32>( NMath::Max( Viewport.Height, 1 ) ) ) * 2.0f;

	FEditorRay Ray;
	Ray.Origin = GetEye();
	Ray.Direction = ( GetForward() + GetRight() * ( NdcX * TanHalf * Viewport.GetAspect() ) + GetUp() * ( NdcY * TanHalf ) ).Normalized();
	return Ray;
}

float32 FEditorCamera::GetPixelSize( const FEditorViewport& Viewport, const FVector3& Point ) const
{
	const float32 Depth = NMath::Max( FVector3::Dot( Point - GetEye(), GetForward() ), Viewport.Near );
	return 2.0f * Depth * NMath::Tan( Viewport.FovY * 0.5f ) / static_cast<float32>( NMath::Max( Viewport.Height, 1 ) );
}
