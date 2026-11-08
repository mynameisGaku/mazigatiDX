#include "MaterialsRig.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 注視点（庭の中心。地面から少し上）
	const FVector3 OrbitTarget( 0.0f, 2.0f, 0.0f );

	// ひとりで回る速さ（ラジアン / 秒）と、マウス 1 ピクセルの回転
	constexpr float32 AutoOrbitSpeed = 0.08f;
	constexpr float32 MouseRadiansPerPixel = 0.008f;

	// 距離の範囲と、ホイールでの変わり方
	constexpr float32 MinDistance = 10.0f;
	constexpr float32 MaxDistance = 60.0f;
	constexpr float32 WheelZoomRate = 0.9f;

	// 縦の回転の範囲（地面の下・真上の手前で止める）
	constexpr float32 MinPitch = 0.08f;
	constexpr float32 MaxPitch = 1.4f;

	// 縦の視野角（ラジアン）と、映す奥行きの範囲
	constexpr float32 FovY = 0.8f;
	constexpr float32 NearDistance = 0.5f;
	constexpr float32 FarDistance = 200.0f;

	bool IsInside( int32 X, int32 Y, int32 Left, int32 Top, int32 Width, int32 Height )
	{
		return X >= Left && X < Left + Width && Y >= Top && Y < Top + Height;
	}
}

CMaterialsRig::CMaterialsRig( const TSharedPtr<FMaterialsDemoState>& InState )
	: State( InState )
{
	UpdateCamera();
}

void CMaterialsRig::UpdateCamera()
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

void CMaterialsRig::HandleKeys()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const EKey PassKeys[FMaterialsDemoState::NumPostPasses] = { EKey::Num1, EKey::Num2, EKey::Num3 };
	for ( int32 Index = 0; Index < FMaterialsDemoState::NumPostPasses && Index < State->PostStack.Num(); ++Index )
	{
		if ( Input->IsKeyPressed( PassKeys[Index] ) )
		{
			State->PostStack.GetEntry( Index ).bEnabled = !State->PostStack.GetEntry( Index ).bEnabled;
		}
	}
	if ( Input->IsKeyPressed( EKey::Num4 ) )
	{
		State->bTimeRunning = !State->bTimeRunning;
	}
}

void CMaterialsRig::Tick( float32 DeltaTime )
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
	State->bButtonHovered = false;
	State->bButtonPressed = false;
	if ( Input != nullptr )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		// ボタンの上でのドラッグは、カメラを回さずに、ボタンを押す
		const bool bOverButton = IsInside( Mouse.X, Mouse.Y, FMaterialsDemoState::ButtonLeft, FMaterialsDemoState::ButtonTop, FMaterialsDemoState::ButtonWidth, FMaterialsDemoState::ButtonHeight );
		State->bButtonHovered = bOverButton;
		State->bButtonPressed = bOverButton && Input->IsMouseButtonDown( EMouseButton::Left );
		if ( !bOverButton && Input->IsMouseButtonDown( EMouseButton::Left ) && ( Mouse.DeltaX != 0 || Mouse.DeltaY != 0 ) )
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
