#include "PostEffectsRig.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 注視点（広場の中心。地面から少し上）
	const FVector3 OrbitTarget( 0.0f, 2.0f, 0.0f );

	// ひとりで回る速さ（ラジアン / 秒）と、マウス 1 ピクセルの回転
	constexpr float32 AutoOrbitSpeed = 0.06f;
	constexpr float32 MouseRadiansPerPixel = 0.008f;

	// 距離の範囲と、ホイールでの変わり方
	constexpr float32 MinDistance = 8.0f;
	constexpr float32 MaxDistance = 70.0f;
	constexpr float32 WheelZoomRate = 0.9f;

	// 縦の回転の範囲（地面の下・真上の手前で止める）
	constexpr float32 MinPitch = 0.05f;
	constexpr float32 MaxPitch = 1.4f;

	// 縦の視野角（ラジアン）と、映す奥行きの範囲
	constexpr float32 FovY = 0.8f;
	constexpr float32 NearDistance = 0.5f;
	constexpr float32 FarDistance = 200.0f;
}

CPostEffectsRig::CPostEffectsRig( const TSharedPtr<FPostEffectsDemoState>& InState )
	: State( InState )
{
	UpdateCamera();
}

void CPostEffectsRig::UpdateCamera()
{
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector3 Direction( NMath::Sin( Yaw ) * NMath::Cos( Pitch ), NMath::Sin( Pitch ), -NMath::Cos( Yaw ) * NMath::Cos( Pitch ) );
	FCamera Camera = FCamera::MakePerspective( OrbitTarget + Direction * Distance, OrbitTarget, FovY );
	Camera.Near = NearDistance;
	Camera.Far = FarDistance;
	State->Camera = Camera;
}

void CPostEffectsRig::HandleKeys()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::Num1 ) )
	{
		State->ViewMode = ( State->ViewMode + 1 ) % FPostEffectsDemoState::NumViewModes;
	}
	if ( Input->IsKeyPressed( EKey::Num2 ) )
	{
		State->ToneMap = ( State->ToneMap + 1 ) % FPostEffectsDemoState::NumToneMaps;
	}
	if ( Input->IsKeyPressed( EKey::Num3 ) )
	{
		State->bBloom = !State->bBloom;
	}
	if ( Input->IsKeyPressed( EKey::Num4 ) )
	{
		State->bFog = !State->bFog;
	}
	if ( Input->IsKeyPressed( EKey::Num5 ) )
	{
		State->bTimeRunning = !State->bTimeRunning;
	}
	if ( Input->IsKeyPressed( EKey::Q ) )
	{
		State->Exposure = NMath::Max( State->Exposure / FPostEffectsDemoState::ExposureStep, FPostEffectsDemoState::MinExposure );
	}
	if ( Input->IsKeyPressed( EKey::E ) )
	{
		State->Exposure = NMath::Min( State->Exposure * FPostEffectsDemoState::ExposureStep, FPostEffectsDemoState::MaxExposure );
	}
}

void CPostEffectsRig::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	if ( State->bTimeRunning )
	{
		State->Time += DeltaTime;
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
