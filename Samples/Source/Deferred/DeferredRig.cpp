#include "DeferredRig.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	// 注視点（広場の中心。地面から少し上）
	const FVector3 OrbitTarget( 0.0f, 2.0f, 0.0f );

	// ひとりで回る速さ（ラジアン / 秒）と、マウス 1 ピクセルの回転
	constexpr float32 AutoOrbitSpeed = 0.06f;
	constexpr float32 MouseRadiansPerPixel = 0.008f;

	// 距離の範囲と、ホイールでの変わり方
	constexpr float32 MinDistance = 8.0f;
	constexpr float32 MaxDistance = 60.0f;
	constexpr float32 WheelZoomRate = 0.9f;

	// 縦の回転の範囲（地面の下・真上の手前で止める）
	constexpr float32 MinPitch = 0.05f;
	constexpr float32 MaxPitch = 1.4f;

	// 縦の視野角（ラジアン）と、映す奥行きの範囲
	constexpr float32 FovY = 0.8f;
	constexpr float32 NearDistance = 0.5f;
	constexpr float32 FarDistance = 120.0f;
}

CDeferredRig::CDeferredRig( const TSharedPtr<FDeferredDemoState>& InState )
	: State( InState )
{
	if ( State.IsValid() )
	{
		// 検査用に、設定を固定する
		const TOptional<FString> View = NCommandLine::GetValue( "deferred-view" );
		if ( View.IsSet() )
		{
			State->ViewMode = NMath::Clamp( static_cast<int32>( View.GetValue().ToFloat( 0.0 ) ), 0, FDeferredDemoState::NumViewModes - 1 );
		}
		if ( NCommandLine::HasFlag( "deferred-forward" ) )
		{
			State->bDeferred = false;
		}
		if ( NCommandLine::HasFlag( "deferred-no-tiles" ) )
		{
			State->bTiles = false;
		}
		if ( NCommandLine::HasFlag( "deferred-cpu-lights" ) )
		{
			State->bGpuLights = false;
		}
		if ( NCommandLine::HasFlag( "deferred-no-spots" ) )
		{
			State->bSpotLights = false;
		}
		if ( NCommandLine::HasFlag( "deferred-tile-stats" ) )
		{
			State->bLogTileStats = true;
		}
		const TOptional<FString> Capacity = NCommandLine::GetValue( "deferred-tile-capacity" );
		if ( Capacity.IsSet() )
		{
			// 3 の倍数（画素 1 つに光 3 つ）に切り下げて、3〜96 に収める
			const int32 Requested = static_cast<int32>( Capacity.GetValue().ToFloat( 36.0 ) );
			State->TileCapacity = NMath::Clamp( Requested / 3 * 3, 3, 96 );
		}
		const TOptional<FString> Slices = NCommandLine::GetValue( "deferred-slices" );
		if ( Slices.IsSet() )
		{
			// 奥行きの層の数（1 は、画面だけを分けるタイル）
			State->NumSlices = NMath::Clamp( static_cast<int32>( Slices.GetValue().ToFloat( 16.0 ) ), 1, CTileLightGrid::MaxDepthSlices );
		}
		const TOptional<FString> FixedTime = NCommandLine::GetValue( "deferred-time" );
		if ( FixedTime.IsSet() )
		{
			State->Time = static_cast<float32>( FixedTime.GetValue().ToFloat( 0.0 ) );
			State->bTimeRunning = false;
			bAutoOrbit = false;
		}
	}
	UpdateCamera();
}

void CDeferredRig::UpdateCamera()
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

void CDeferredRig::HandleKeys()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	if ( Input->IsKeyPressed( EKey::Num1 ) )
	{
		State->ViewMode = ( State->ViewMode + 1 ) % FDeferredDemoState::NumViewModes;
	}
	if ( Input->IsKeyPressed( EKey::Num2 ) )
	{
		State->bDeferred = !State->bDeferred;
	}
	if ( Input->IsKeyPressed( EKey::Num3 ) )
	{
		State->bTimeRunning = !State->bTimeRunning;
	}
	if ( Input->IsKeyPressed( EKey::Num4 ) )
	{
		State->bTiles = !State->bTiles;
	}
	if ( Input->IsKeyPressed( EKey::Q ) )
	{
		State->Exposure = NMath::Max( State->Exposure / FDeferredDemoState::ExposureStep, FDeferredDemoState::MinExposure );
	}
	if ( Input->IsKeyPressed( EKey::E ) )
	{
		State->Exposure = NMath::Min( State->Exposure * FDeferredDemoState::ExposureStep, FDeferredDemoState::MaxExposure );
	}
}

void CDeferredRig::Tick( float32 DeltaTime )
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
