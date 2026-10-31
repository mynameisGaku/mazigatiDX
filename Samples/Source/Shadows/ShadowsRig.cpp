#include "ShadowsRig.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 注視点（中庭の中心。地面から少し上）
	const FVector3 OrbitTarget( 0.0f, 2.0f, 0.0f );

	// ひとりで回る速さ（ラジアン / 秒）と、マウス 1 ピクセルの回転
	constexpr float32 AutoOrbitSpeed = 0.05f;
	constexpr float32 MouseRadiansPerPixel = 0.008f;

	// 太陽が動く速さ（ラジアン / 秒）。約 42 秒で一周する
	constexpr float32 SunSpeed = 0.15f;

	// 距離の範囲と、ホイールでの変わり方
	constexpr float32 MinDistance = 12.0f;
	constexpr float32 MaxDistance = 70.0f;
	constexpr float32 WheelZoomRate = 0.9f;

	// 縦の回転の範囲（地面の下・真上の手前で止める）
	constexpr float32 MinPitch = 0.08f;
	constexpr float32 MaxPitch = 1.4f;

	// カメラが映す画面の中心を、3D を見せる範囲の中心へずらす量（ピクセル）。画面全体（1280 x 720）の中心 (640, 360) から、見せる範囲の中心 (640, 400) へ
	const FVector2 ViewCenterOffset( 0.0f, 40.0f );

	// 縦の視野角（ラジアン）
	constexpr float32 FovY = 0.8f;
}

CShadowsRig::CShadowsRig( const TSharedPtr<FShadowsDemoState>& InState )
	: State( InState )
{
	UpdateCamera();
}

void CShadowsRig::UpdateCamera()
{
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector3 Direction( NMath::Sin( Yaw ) * NMath::Cos( Pitch ), NMath::Sin( Pitch ), -NMath::Cos( Yaw ) * NMath::Cos( Pitch ) );
	FCamera Camera = FCamera::MakePerspective( OrbitTarget + Direction * Distance, OrbitTarget, FovY );
	Camera.Near = 0.5f;
	Camera.Far = 400.0f;
	Camera.ScreenOffset = ViewCenterOffset;
	State->Camera = Camera;
}

void CShadowsRig::HandleKeys()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::Num1 ) )
	{
		State->bShadows = !State->bShadows;
	}
	if ( Input->IsKeyPressed( EKey::Num2 ) )
	{
		State->SizeLevel = ( State->SizeLevel + 1 ) % FShadowsDemoState::NumSizeLevels;
	}
	if ( Input->IsKeyPressed( EKey::Num3 ) )
	{
		State->AreaMode = ( State->AreaMode + 1 ) % FShadowsDemoState::NumAreaModes;
	}
	if ( Input->IsKeyPressed( EKey::Num4 ) )
	{
		State->bSunMoving = !State->bSunMoving;
	}
	if ( Input->IsKeyPressed( EKey::Num5 ) )
	{
		State->DepthMode = ( State->DepthMode + 1 ) % FShadowsDemoState::NumDepthModes;
	}
	if ( Input->IsKeyPressed( EKey::Num6 ) )
	{
		State->bPreview = !State->bPreview;
	}
	if ( Input->IsKeyPressed( EKey::Num7 ) )
	{
		State->bLantern = !State->bLantern;
	}
}

void CShadowsRig::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	State->Time += DeltaTime;
	if ( State->bSunMoving )
	{
		State->SunAzimuth += SunSpeed * DeltaTime;
	}

	const IInput* Input = GetAppSubsystem<IInput>();
	bool bTouched = false;
	if ( Input != nullptr )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		if ( Input->IsMouseButtonDown( EMouseButton::Left ) && ( Mouse.DeltaX != 0 || Mouse.DeltaY != 0 ) )
		{
			Yaw += static_cast<float32>( Mouse.DeltaX ) * MouseRadiansPerPixel;
			Pitch = NMath::Clamp( Pitch + static_cast<float32>( Mouse.DeltaY ) * MouseRadiansPerPixel, MinPitch, MaxPitch );
			bTouched = true;
		}
		if ( Mouse.WheelY != 0.0f )
		{
			Distance = NMath::Clamp( Distance * NMath::Pow( WheelZoomRate, Mouse.WheelY ), MinDistance, MaxDistance );
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

	HandleKeys();
	UpdateCamera();
}
