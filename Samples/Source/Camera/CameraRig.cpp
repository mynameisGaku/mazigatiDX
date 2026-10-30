#include "CameraRig.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 注視点（地面から少し上）
	const FVector3 OrbitTarget( 0.0f, 1.5f, 0.0f );

	// ひとりで回る速さ（ラジアン / 秒）、キーで回る速さ、マウス 1 ピクセルの回転
	constexpr float32 AutoOrbitSpeed = 0.3f;
	constexpr float32 KeyOrbitSpeed = 1.2f;
	constexpr float32 MouseRadiansPerPixel = 0.008f;

	// 距離の範囲と、キー・ホイールでの変わり方
	constexpr float32 MinDistance = 8.0f;
	constexpr float32 MaxDistance = 60.0f;
	constexpr float32 KeyZoomSpeed = 12.0f;
	constexpr float32 WheelZoomRate = 0.9f;

	// 縦の回転の範囲（真上・真下の手前で止める）と、視野角の範囲・変わる速さ
	constexpr float32 MaxPitch = 1.35f;
	constexpr float32 MinFov = 0.3f;
	constexpr float32 MaxFov = 1.8f;
	constexpr float32 FovSpeed = 0.8f;

	// 正射影で映す縦の長さは、距離に比例させる（近づくと、拡大される）
	constexpr float32 OrthoHeightPerDistance = 0.9f;

	// カメラが映す画面の中心を、3D を見せる範囲の中心へずらす量（ピクセル）。画面全体（1280 x 720）の中心 (640, 360) から、見せる範囲の中心 (640, 400) へ
	const FVector2 ViewCenterOffset( 0.0f, 40.0f );

	// 光線と球のあたり。あたるなら、光線の始まりからの距離（あたらなければ -1）
	float32 IntersectRaySphere( const FCameraRay& Ray, const FVector3& Center, float32 Radius )
	{
		const FVector3 ToOrigin = Ray.Origin - Center;
		const float32 B = FVector3::Dot( ToOrigin, Ray.Direction );
		const float32 C = FVector3::Dot( ToOrigin, ToOrigin ) - Radius * Radius;
		const float32 Discriminant = B * B - C;
		if ( Discriminant < 0.0f )
		{
			return -1.0f;
		}
		const float32 Root = NMath::Sqrt( Discriminant );
		const float32 Near = -B - Root;
		return Near >= 0.0f ? Near : ( -B + Root >= 0.0f ? -B + Root : -1.0f );
	}
}

CCameraRig::CCameraRig( const TSharedPtr<FCameraDemoState>& InState )
	: State( InState )
{
	UpdateCamera();
	UpdateVisibility();
}

void CCameraRig::UpdateCamera()
{
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector3 Direction( NMath::Sin( Yaw ) * NMath::Cos( Pitch ), NMath::Sin( Pitch ), -NMath::Cos( Yaw ) * NMath::Cos( Pitch ) );
	const FVector3 Position = OrbitTarget + Direction * Distance;
	FCamera Camera = bOrthographic ? FCamera::MakeOrthographic( Position, OrbitTarget, Distance * OrthoHeightPerDistance ) : FCamera::MakePerspective( Position, OrbitTarget, FovY );
	Camera.Near = 0.5f;
	Camera.Far = 300.0f;
	Camera.ScreenOffset = ViewCenterOffset;
	State->Camera = Camera;
}

void CCameraRig::UpdateVisibility()
{
	if ( !State.IsValid() )
	{
		return;
	}
	const FFrustum Frustum = State->Camera.GetFrustum( State->GetScreenViewport() );
	State->Visible.SetNum( State->Objects.Num(), false );
	for ( int32 Index = 0; Index < State->Objects.Num(); ++Index )
	{
		State->Visible[Index] = Frustum.IntersectsSphere( State->Objects[Index].Position, State->Objects[Index].GetBoundingRadius() );
	}
}

void CCameraRig::UpdateHover()
{
	State->HoveredIndex = -1;
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	const bool bInside = Mouse.X >= FCameraDemoState::ViewLeft && Mouse.X < FCameraDemoState::ViewLeft + FCameraDemoState::ViewWidth && Mouse.Y >= FCameraDemoState::ViewTop && Mouse.Y < FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight;
	if ( !bInside )
	{
		return;
	}
	// マウスの位置（画面の座標）を通る光線を、世界の中へ伸ばす
	FCameraRay Ray;
	if ( !State->Camera.ScreenToRay( State->GetScreenViewport(), FVector2( static_cast<float32>( Mouse.X ), static_cast<float32>( Mouse.Y ) ), Ray ) )
	{
		return;
	}
	float32 Nearest = -1.0f;
	for ( int32 Index = 0; Index < State->Objects.Num(); ++Index )
	{
		if ( !State->Visible[Index] )
		{
			continue;
		}
		const float32 Distance = IntersectRaySphere( Ray, State->Objects[Index].Position, State->Objects[Index].GetBoundingRadius() );
		if ( Distance >= 0.0f && ( Nearest < 0.0f || Distance < Nearest ) )
		{
			Nearest = Distance;
			State->HoveredIndex = Index;
		}
	}
}

void CCameraRig::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	State->Time += DeltaTime;

	const IInput* Input = GetAppSubsystem<IInput>();
	bool bTouched = false;
	if ( Input != nullptr )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		if ( Input->IsMouseButtonDown( EMouseButton::Left ) && ( Mouse.DeltaX != 0 || Mouse.DeltaY != 0 ) )
		{
			Yaw += static_cast<float32>( Mouse.DeltaX ) * MouseRadiansPerPixel;
			Pitch = NMath::Clamp( Pitch + static_cast<float32>( Mouse.DeltaY ) * MouseRadiansPerPixel, -MaxPitch, MaxPitch );
			bTouched = true;
		}
		if ( Mouse.WheelY != 0.0f )
		{
			Distance = NMath::Clamp( Distance * NMath::Pow( WheelZoomRate, Mouse.WheelY ), MinDistance, MaxDistance );
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::A ) || Input->IsKeyDown( EKey::Left ) )
		{
			Yaw -= KeyOrbitSpeed * DeltaTime;
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::D ) || Input->IsKeyDown( EKey::Right ) )
		{
			Yaw += KeyOrbitSpeed * DeltaTime;
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::W ) || Input->IsKeyDown( EKey::Up ) )
		{
			Distance = NMath::Max( MinDistance, Distance - KeyZoomSpeed * DeltaTime );
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::S ) || Input->IsKeyDown( EKey::Down ) )
		{
			Distance = NMath::Min( MaxDistance, Distance + KeyZoomSpeed * DeltaTime );
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::Q ) )
		{
			FovY = NMath::Max( MinFov, FovY - FovSpeed * DeltaTime );
			bTouched = true;
		}
		if ( Input->IsKeyDown( EKey::E ) )
		{
			FovY = NMath::Min( MaxFov, FovY + FovSpeed * DeltaTime );
			bTouched = true;
		}
		if ( Input->IsKeyPressed( EKey::P ) )
		{
			bOrthographic = !bOrthographic;
			bTouched = true;
		}
		if ( Input->IsKeyPressed( EKey::R ) )
		{
			bAutoOrbit = true;
			bTouched = false;
		}
	}
	if ( bTouched )
	{
		bAutoOrbit = false;
	}
	if ( bAutoOrbit )
	{
		Yaw += AutoOrbitSpeed * DeltaTime;
	}

	UpdateCamera();
	UpdateVisibility();
	UpdateHover();
}
