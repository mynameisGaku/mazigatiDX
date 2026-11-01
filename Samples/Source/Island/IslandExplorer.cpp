#include "IslandExplorer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Source/Island/IslandTerrain.h"

namespace
{
	// 歩く・走る速さ（ワールドの単位 / 秒）と、向きを変える速さ（ラジアン / 秒）
	constexpr float32 WalkSpeed = 6.0f;
	constexpr float32 RunSpeed = 11.0f;
	constexpr float32 TurnSpeed = 10.0f;

	// 目的地に着いたとみなす距離
	constexpr float32 ArriveDistance = 0.6f;

	// マウス 1 ピクセルの回転と、距離の範囲・ホイールでの変わり方
	constexpr float32 MouseRadiansPerPixel = 0.008f;
	constexpr float32 MinDistance = 6.0f;
	constexpr float32 MaxDistance = 40.0f;
	constexpr float32 WheelZoomRate = 0.9f;
	constexpr float32 MinPitch = 0.08f;
	constexpr float32 MaxPitch = 1.35f;

	// カメラ: 見る点の高さ（足元から）・縦の視野角・地面からの最低の高さ
	constexpr float32 LookHeight = 1.6f;
	constexpr float32 FovY = 0.8f;
	constexpr float32 CameraClearance = 1.0f;

	// カメラが映す画面の中心を、3D を見せる範囲の中心へずらす量（ピクセル）。画面全体（1280 x 720）の中心から、見せる範囲の中心 (640, 400) へ
	const FVector2 ViewCenterOffset( 0.0f, 40.0f );

	// 角度 From から To へ、最大 MaxStep だけ近づける（-π〜π の差で、近いほうへ回る）
	float32 TurnToward( float32 From, float32 To, float32 MaxStep )
	{
		float32 Delta = NMath::Fmod( To - From + NMath::PI, NMath::TWO_PI );
		if ( Delta < 0.0f )
		{
			Delta += NMath::TWO_PI;
		}
		Delta -= NMath::PI;
		return From + NMath::Clamp( Delta, -MaxStep, MaxStep );
	}
}

CIslandExplorer::CIslandExplorer( const TSharedPtr<FIslandState>& InState )
	: State( InState )
{
	UpdateCamera();
}

FVector3 CIslandExplorer::DecideDirection( bool& bOutRun )
{
	bOutRun = false;
	const IInput* Input = GetAppSubsystem<IInput>();
	FVector3 Wanted( 0.0f, 0.0f, 0.0f );
	if ( Input != nullptr )
	{
		// カメラから見た前と右（水平）
		const FVector3 Forward( -NMath::Sin( State->CameraYaw ), 0.0f, NMath::Cos( State->CameraYaw ) );
		const FVector3 Right( NMath::Cos( State->CameraYaw ), 0.0f, NMath::Sin( State->CameraYaw ) );
		float32 Ahead = 0.0f;
		float32 Side = 0.0f;
		Ahead += ( Input->IsKeyDown( EKey::W ) || Input->IsKeyDown( EKey::Up ) ) ? 1.0f : 0.0f;
		Ahead -= ( Input->IsKeyDown( EKey::S ) || Input->IsKeyDown( EKey::Down ) ) ? 1.0f : 0.0f;
		Side += ( Input->IsKeyDown( EKey::D ) || Input->IsKeyDown( EKey::Right ) ) ? 1.0f : 0.0f;
		Side -= ( Input->IsKeyDown( EKey::A ) || Input->IsKeyDown( EKey::Left ) ) ? 1.0f : 0.0f;
		bOutRun = Input->IsKeyDown( EKey::LeftShift );
		if ( Ahead != 0.0f || Side != 0.0f )
		{
			// キーで歩くと、ひとりで歩くのも、目的地もやめる
			State->bAutoWalk = false;
			State->bHasTarget = false;
			return ( Forward * Ahead + Right * Side ).Normalized();
		}
		if ( Input->IsKeyPressed( EKey::R ) )
		{
			State->bAutoWalk = true;
			State->bHasTarget = false;
		}

		// 右クリック: 画面の点から光線を出して、地面に当たった所を目的地にする（見せる範囲の中だけ）
		const FMouseState& Mouse = Input->GetMouseState();
		if ( Input->IsMouseButtonPressed( EMouseButton::Right ) && Mouse.Y >= FIslandState::ViewTop )
		{
			FCameraRay Ray;
			FVector3 Hit;
			if ( State->Camera.ScreenToRay( State->GetScreenViewport(), FVector2( static_cast<float32>( Mouse.X ), static_cast<float32>( Mouse.Y ) ), Ray ) && NIslandTerrain::Raycast( *State, Ray, Hit ) && NIslandTerrain::IsWalkable( *State, Hit.x, Hit.z ) )
			{
				State->Target = Hit;
				State->bHasTarget = true;
				State->bAutoWalk = false;
			}
		}
	}

	// 目的地か、ひとりで歩くときは、いちばん近いまだ拾っていない結晶へ
	FVector3 Goal;
	bool bHasGoal = false;
	if ( State->bHasTarget )
	{
		Goal = State->Target;
		bHasGoal = true;
	}
	else if ( State->bAutoWalk )
	{
		float32 Nearest = 0.0f;
		for ( const FIslandState::FCrystal& Crystal : State->Crystals )
		{
			const FVector3 Delta = Crystal.Position - State->ExplorerPosition;
			const float32 Distance = Delta.x * Delta.x + Delta.z * Delta.z;
			if ( !Crystal.bTaken && ( !bHasGoal || Distance < Nearest ) )
			{
				Goal = Crystal.Position;
				Nearest = Distance;
				bHasGoal = true;
			}
		}
	}
	if ( !bHasGoal )
	{
		return Wanted;
	}
	FVector3 ToGoal = Goal - State->ExplorerPosition;
	ToGoal.y = 0.0f;
	if ( ToGoal.Length() < ArriveDistance )
	{
		State->bHasTarget = false;
		return Wanted;
	}
	return ToGoal.Normalized();
}

void CIslandExplorer::Move( const FVector3& Direction, float32 Speed, float32 DeltaTime )
{
	const FVector3 Step = Direction * ( Speed * DeltaTime );
	FVector3 Next = State->ExplorerPosition + Step;
	if ( !NIslandTerrain::IsWalkable( *State, Next.x, Next.z ) )
	{
		// 入れない向きは、壁に沿って滑る（x だけ・z だけを試す）
		const FVector3 AlongX = State->ExplorerPosition + FVector3( Step.x, 0.0f, 0.0f );
		const FVector3 AlongZ = State->ExplorerPosition + FVector3( 0.0f, 0.0f, Step.z );
		if ( NIslandTerrain::IsWalkable( *State, AlongX.x, AlongX.z ) )
		{
			Next = AlongX;
		}
		else if ( NIslandTerrain::IsWalkable( *State, AlongZ.x, AlongZ.z ) )
		{
			Next = AlongZ;
		}
		else
		{
			State->ExplorerSpeed = 0.0f;
			return;
		}
	}
	Next.y = NIslandTerrain::GetHeight( *State, Next.x, Next.z );
	State->ExplorerPosition = Next;
	State->ExplorerSpeed = Speed;
}

void CIslandExplorer::HandleCameraInput()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	if ( Input->IsMouseButtonDown( EMouseButton::Left ) && ( Mouse.DeltaX != 0 || Mouse.DeltaY != 0 ) )
	{
		State->CameraYaw += static_cast<float32>( Mouse.DeltaX ) * MouseRadiansPerPixel;
		State->CameraPitch = NMath::Clamp( State->CameraPitch + static_cast<float32>( Mouse.DeltaY ) * MouseRadiansPerPixel, MinPitch, MaxPitch );
	}
	if ( Mouse.WheelY != 0.0f )
	{
		State->CameraDistance = NMath::Clamp( State->CameraDistance * NMath::Pow( WheelZoomRate, Mouse.WheelY ), MinDistance, MaxDistance );
	}
}

void CIslandExplorer::UpdateCamera()
{
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector3 Look = State->ExplorerPosition + FVector3( 0.0f, LookHeight, 0.0f );
	const FVector3 Offset( NMath::Sin( State->CameraYaw ) * NMath::Cos( State->CameraPitch ), NMath::Sin( State->CameraPitch ), -NMath::Cos( State->CameraYaw ) * NMath::Cos( State->CameraPitch ) );
	FVector3 Position = Look + Offset * State->CameraDistance;
	// 丘の向こうから見るときは、地面に潜らないよう、上げる
	const float32 Ground = NMath::Max( NIslandTerrain::GetHeight( *State, Position.x, Position.z ), FIslandState::SeaLevel );
	Position.y = NMath::Max( Position.y, Ground + CameraClearance );
	FCamera Camera = FCamera::MakePerspective( Position, Look, FovY );
	Camera.Near = 0.3f;
	Camera.Far = 400.0f;
	Camera.ScreenOffset = ViewCenterOffset;
	State->Camera = Camera;
}

void CIslandExplorer::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	HandleCameraInput();
	bool bRun = false;
	const FVector3 Direction = DecideDirection( bRun );
	if ( Direction.Length() > 0.0f )
	{
		State->ExplorerYaw = TurnToward( State->ExplorerYaw, NMath::Atan2( Direction.x, Direction.z ), TurnSpeed * DeltaTime );
		Move( Direction, bRun ? RunSpeed : WalkSpeed, DeltaTime );
	}
	else
	{
		State->ExplorerSpeed = 0.0f;
	}
	UpdateCamera();
}
