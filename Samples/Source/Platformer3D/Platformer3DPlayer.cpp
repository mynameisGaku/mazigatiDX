#include "Platformer3DPlayer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer3D/Platformer3DCoinPop.h"
#include "Source/Platformer3D/Platformer3DEnemy.h"
#include "Source/Platformer3D/Platformer3DFireball.h"
#include "Source/Platformer3D/Platformer3DPowerUp.h"
#include "Source/Platformer3D/Platformer3DState.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	constexpr float32 HalfWidth = 0.4f;
	constexpr float32 SmallHeight = 1.0f;
	constexpr float32 BigHeight = 1.8f;

	// 走り方（メートル / 秒）
	constexpr float32 WalkSpeed = 4.5f;
	constexpr float32 RunSpeed = 8.0f;
	constexpr float32 GroundAccel = 45.0f;
	constexpr float32 AirAccel = 24.0f;
	constexpr float32 GroundBrake = 55.0f;

	// ジャンプ。ボタンを押している間は、重力が小さい（高く跳べる）。離すと、すぐに落ち始める
	constexpr float32 JumpSpeed = 11.5f;
	constexpr float32 HoldGravity = 28.0f;
	constexpr float32 FallGravity = 48.0f;
	constexpr float32 MaxFallSpeed = 30.0f;
	constexpr float32 StompBounce = 6.0f;
	constexpr float32 StompBounceHeld = 10.0f;

	constexpr int32 MaxFireballs = 2;
	constexpr float32 DeathSeconds = 2.2f;

	// カメラ: 主人公の後ろ（水平に CameraDistance、上に CameraHeight）から、少し上を見る
	constexpr float32 CameraDistance = 7.5f;
	constexpr float32 CameraHeight = 4.2f;
	constexpr float32 CameraTurnSpeed = 1.8f;

	constexpr float32 GoalRadius = 1.8f;
}

CPlatformer3DPlayer::CPlatformer3DPlayer( const TSharedPtr<FPlatformer3DState>& InState )
	: State( InState )
{}

void CPlatformer3DPlayer::OnSpawn()
{
	Position = State->Level.PlayerStart;
	PreviousFeetY = Position.y;
	State->PlayerPosition = Position;
	UpdateCamera();
}

float32 CPlatformer3DPlayer::GetHeight() const
{
	return State->Status.IsBig() ? BigHeight : SmallHeight;
}

CPlatformer3DPlayer::FControls CPlatformer3DPlayer::ReadControls( float32 DeltaTime )
{
	if ( State->bAutoPlay )
	{
		return ThinkAutoPlay( DeltaTime );
	}
	FControls Controls;
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return Controls;
	}
	if ( Input->IsKeyDown( EKey::A ) )
	{
		Controls.Move.x -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::D ) )
	{
		Controls.Move.x += 1.0f;
	}
	if ( Input->IsKeyDown( EKey::W ) )
	{
		Controls.Move.y += 1.0f;
	}
	if ( Input->IsKeyDown( EKey::S ) )
	{
		Controls.Move.y -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Left ) || Input->IsKeyDown( EKey::Q ) )
	{
		Controls.Turn -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Right ) || Input->IsKeyDown( EKey::E ) )
	{
		Controls.Turn += 1.0f;
	}
	Controls.bRun = Input->IsKeyDown( EKey::LeftShift ) || Input->IsKeyDown( EKey::RightShift );
	Controls.bJumpPressed = Input->IsKeyPressed( EKey::Space );
	Controls.bJumpHeld = Input->IsKeyDown( EKey::Space );
	Controls.bFirePressed = Input->IsKeyPressed( EKey::X ) || Input->IsKeyPressed( EKey::J );
	return Controls;
}

CPlatformer3DPlayer::FControls CPlatformer3DPlayer::ThinkAutoPlay( float32 DeltaTime )
{
	// +Z（奥）へ走り続けて、壁・穴・敵が近づいたら跳ぶ（画面の確認と、検査のための、かんたんな自動操作）
	FControls Controls;
	Controls.Move = FVector2( 0.0f, 1.0f );
	Controls.bRun = true;
	const FPlatformer3DLevel& Level = State->Level;
	const FVector3 Forward( 0.0f, 0.0f, 1.0f );
	const bool bWall = Level.Overlaps( Position + Forward * 1.0f + FVector3( 0.0f, 0.2f, 0.0f ), 0.35f, 0.4f );
	const bool bPit = bOnGround && !Level.HasGroundBelow( Position.x, Position.z + 1.5f, Position.y, 1.0f );
	bool bEnemy = false;
	for ( const TSharedPtr<CPlatformer3DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer3DEnemy>() )
	{
		const FVector3 Delta = Enemy->GetPosition() - Position;
		if ( Enemy->IsAlive() && Delta.z > 0.0f && Delta.z < 3.5f && NMath::Abs( Delta.x ) < 1.5f && NMath::Abs( Delta.y ) < 1.0f )
		{
			bEnemy = true;
		}
	}
	// ブロックが、すぐ前の頭の上にあれば、跳んで叩く
	bool bBlockAbove = false;
	for ( const FBlock3D& Block : Level.Blocks )
	{
		const bool bHittable = Block.IsActive && ( Block.Kind == EBlock3DKind::Question || Block.Kind == EBlock3DKind::PowerUp || Block.Kind == EBlock3DKind::StarBlock );
		const float32 CenterZ = ( Block.Min.z + Block.Max.z ) * 0.5f;
		const float32 CenterX = ( Block.Min.x + Block.Max.x ) * 0.5f;
		if ( bHittable && NMath::Abs( CenterX - Position.x ) < 0.6f && CenterZ - Position.z > 0.2f && CenterZ - Position.z < 1.1f && Block.Min.y > Position.y + 1.0f && Block.Min.y < Position.y + 3.0f )
		{
			bBlockAbove = true;
		}
	}
	// 出てきたアイテムが近くにあれば、取りに行く
	for ( const TSharedPtr<CPlatformer3DPowerUp>& PowerUp : GetScene()->FindObjects<CPlatformer3DPowerUp>() )
	{
		const FVector3 Delta = PowerUp->GetPosition() - Position;
		const float32 Flat = NMath::Sqrt( Delta.x * Delta.x + Delta.z * Delta.z );
		if ( !PowerUp->IsEmerging() && Flat < 7.0f && Flat > 0.1f && NMath::Abs( Delta.y ) < 3.0f )
		{
			Controls.Move = FVector2( Delta.x / Flat, Delta.z / Flat );
			Controls.bRun = false;
		}
	}
	Controls.bJumpPressed = bOnGround && ( bWall || bPit || bEnemy || bBlockAbove );
	Controls.bJumpHeld = Controls.bJumpPressed || ( Velocity.y > 0.0f && AirTime < 0.45f );
	AutoFireTimer -= DeltaTime;
	if ( State->Status.CanShoot() && AutoFireTimer <= 0.0f )
	{
		Controls.bFirePressed = true;
		AutoFireTimer = 0.9f;
	}
	return Controls;
}

void CPlatformer3DPlayer::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	State->Status.Tick( DeltaTime );
	if ( State->bGoalReached )
	{
		TickGoal( DeltaTime );
		return;
	}
	if ( bDying )
	{
		TickDeath( DeltaTime );
		return;
	}

	const FControls Controls = ReadControls( DeltaTime );
	State->CameraYaw += Controls.Turn * CameraTurnSpeed * DeltaTime;

	// 水平の動き: カメラから見た向きを、世界の向きにして、目標の速さへ加速（止まるときは、ブレーキ）
	const float32 Yaw = State->CameraYaw;
	const FVector3 CameraForward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	const FVector3 CameraRight( NMath::Cos( Yaw ), 0.0f, -NMath::Sin( Yaw ) );
	FVector3 Wanted = CameraRight * Controls.Move.x + CameraForward * Controls.Move.y;
	if ( Wanted.LengthSquared() > 1.0f )
	{
		Wanted.Normalize();
	}
	const float32 MaxSpeed = Controls.bRun ? RunSpeed : WalkSpeed;
	const FVector3 TargetVelocity = Wanted * MaxSpeed;
	FVector3 Delta( TargetVelocity.x - Velocity.x, 0.0f, TargetVelocity.z - Velocity.z );
	const float32 Rate = Wanted.LengthSquared() < 0.0001f ? GroundBrake : ( bOnGround ? GroundAccel : AirAccel );
	const float32 MaxDelta = Rate * DeltaTime;
	const float32 DeltaLength = Delta.Length();
	if ( DeltaLength > MaxDelta )
	{
		Delta = Delta * ( MaxDelta / DeltaLength );
	}
	Velocity.x += Delta.x;
	Velocity.z += Delta.z;
	if ( Wanted.LengthSquared() > 0.0001f )
	{
		FacingYaw = NMath::Atan2( Wanted.x, Wanted.z );
	}

	// ジャンプ（速く走っているほど、少し高く跳べる）と、重力
	if ( Controls.bJumpPressed && bOnGround )
	{
		const float32 Flat = NMath::Sqrt( Velocity.x * Velocity.x + Velocity.z * Velocity.z );
		Velocity.y = JumpSpeed + Flat * 0.12f;
		bOnGround = false;
	}
	const float32 Gravity = ( Velocity.y > 0.0f && Controls.bJumpHeld ) ? HoldGravity : FallGravity;
	Velocity.y = NMath::Max( Velocity.y - Gravity * DeltaTime, -MaxFallSpeed );
	AirTime = bOnGround ? 0.0f : AirTime + DeltaTime;

	PreviousFeetY = Position.y;
	const FBlock3DMoveResult Result = State->Level.Move( Position, HalfWidth, GetHeight(), Velocity, DeltaTime );
	bOnGround = Result.bOnGround;
	if ( Result.bHitCeiling )
	{
		HitBlock( Result.CeilingBlock );
	}

	CollectTouching();
	HandleEnemies( Controls.bJumpHeld );
	if ( bDying )
	{
		return;
	}
	if ( Controls.bFirePressed && State->Status.CanShoot() )
	{
		ShootFireball();
	}

	// 穴に落ちた（姿にも、無敵にも関係なく、やられる）
	if ( Position.y < FPlatformer3DLevel::KillHeight )
	{
		State->Status.Die();
		StartDeath();
		return;
	}
	// ゴール: ポールの近くへ来た
	const float32 GoalDX = Position.x - State->Level.GoalPosition.x;
	const float32 GoalDZ = Position.z - State->Level.GoalPosition.z;
	if ( GoalDX * GoalDX + GoalDZ * GoalDZ < GoalRadius * GoalRadius && Position.y < State->Level.GoalPosition.y + 6.0f )
	{
		State->bGoalReached = true;
		State->Status.AddScore( 5000 );
		Velocity = FVector3::Zero;
		return;
	}
	State->PlayerPosition = Position;
	UpdateCamera();
}

void CPlatformer3DPlayer::UpdateCamera()
{
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	const FVector3 Look = Position + FVector3( 0.0f, 1.0f, 0.0f );
	FCamera Camera = FCamera::MakePerspective( Look - Forward * CameraDistance + FVector3( 0.0f, CameraHeight, 0.0f ), Look, NMath::PI / 3.0f );
	Camera.Near = 0.3f;
	Camera.Far = 400.0f;
	State->Camera = Camera;
}

void CPlatformer3DPlayer::HitBlock( int32 BlockIndex )
{
	const bool bBig = State->Status.IsBig();
	const EBlock3DHit Hit = State->Level.Hit( BlockIndex, bBig );
	const FBlock3D& Block = State->Level.Blocks[BlockIndex];
	const FVector3 BlockTop( ( Block.Min.x + Block.Max.x ) * 0.5f, Block.Max.y, ( Block.Min.z + Block.Max.z ) * 0.5f );
	switch ( Hit )
	{
	case EBlock3DHit::Coin:
		State->Status.Collect( EPlatformerItem::Coin );
		GetScene()->SpawnObject<CPlatformer3DCoinPop>( State, BlockTop );
		break;
	case EBlock3DHit::PowerUp:
		// ちいさいときはキノコ、おおきいときは花
		GetScene()->SpawnObject<CPlatformer3DPowerUp>( State, bBig ? EPlatformerItem::Flower : EPlatformerItem::Mushroom, BlockTop );
		break;
	case EBlock3DHit::Star:
		GetScene()->SpawnObject<CPlatformer3DPowerUp>( State, EPlatformerItem::Star, BlockTop );
		break;
	case EBlock3DHit::Broke:
		State->Status.AddScore( 50 );
		break;
	default:
		break;
	}
	if ( Hit == EBlock3DHit::None )
	{
		return;
	}
	// 叩いたブロックの上に乗っていた敵は、はね飛ばされる
	for ( const TSharedPtr<CPlatformer3DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer3DEnemy>() )
	{
		const FVector3& At = Enemy->GetPosition();
		if ( Enemy->IsAlive() && At.x > Block.Min.x - 0.3f && At.x < Block.Max.x + 0.3f && At.z > Block.Min.z - 0.3f && At.z < Block.Max.z + 0.3f && NMath::Abs( At.y - Block.Max.y ) < 0.3f )
		{
			Enemy->KnockOff( FVector3( At.x - Position.x, 0.0f, At.z - Position.z ).Normalized() );
			State->Status.AddScore( 100 );
		}
	}
}

void CPlatformer3DPlayer::CollectTouching()
{
	// コイン（見えているものだけ）
	const FVector3 Center = Position + FVector3( 0.0f, GetHeight() * 0.5f, 0.0f );
	for ( int32 Index = State->Level.Coins.Num() - 1; Index >= 0; --Index )
	{
		const FVector3 Delta = State->Level.Coins[Index] - Center;
		if ( Delta.LengthSquared() < 0.9f * 0.9f )
		{
			State->Level.Coins.RemoveAt( Index );
			State->Status.Collect( EPlatformerItem::Coin );
		}
	}
	// 出てきたアイテム
	for ( const TSharedPtr<CPlatformer3DPowerUp>& PowerUp : GetScene()->FindObjects<CPlatformer3DPowerUp>() )
	{
		if ( PowerUp->IsEmerging() )
		{
			continue;
		}
		const FVector3& At = PowerUp->GetPosition();
		const bool bTouch = NMath::Abs( At.x - Position.x ) < HalfWidth + CPlatformer3DPowerUp::HalfWidth
			&& NMath::Abs( At.z - Position.z ) < HalfWidth + CPlatformer3DPowerUp::HalfWidth
			&& At.y < Position.y + GetHeight() && At.y + CPlatformer3DPowerUp::Height > Position.y;
		if ( bTouch )
		{
			State->Status.Collect( PowerUp->GetItem() );
			PowerUp->Destroy();
		}
	}
}

void CPlatformer3DPlayer::HandleEnemies( bool bJumpHeld )
{
	FPlatformerStatus& Status = State->Status;
	for ( const TSharedPtr<CPlatformer3DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer3DEnemy>() )
	{
		if ( !Enemy->IsAlive() || !Enemy->Overlaps( Position, HalfWidth, GetHeight() ) )
		{
			continue;
		}
		FVector3 Away( Enemy->GetPosition().x - Position.x, 0.0f, Enemy->GetPosition().z - Position.z );
		Away = Away.LengthSquared() < 0.0001f ? FVector3( 0.0f, 0.0f, 1.0f ) : Away.Normalized();
		if ( Status.HasStar() )
		{
			// スターのあいだは、ふれるだけで倒せる
			Enemy->KnockOff( Away );
			Status.AddScore( 200 );
			continue;
		}
		// 踏んだ: 落ちてきて、足が敵の上のほうにある
		const bool bStomp = Velocity.y < 0.0f && PreviousFeetY >= Enemy->GetTopY() - 0.3f;
		if ( bStomp )
		{
			const bool bWasStill = Enemy->IsStill();
			Enemy->OnStomped( Position );
			Velocity.y = bJumpHeld ? StompBounceHeld : StompBounce;
			Status.AddScore( bWasStill ? 400 : 100 );
			continue;
		}
		if ( Enemy->IsStill() )
		{
			// 止まった甲羅に、横からふれた: 蹴り出す
			Enemy->OnKicked( Position );
			Status.AddScore( 400 );
			continue;
		}
		if ( Enemy->IsDeadly() )
		{
			Hurt();
			if ( bDying )
			{
				return;
			}
		}
	}
}

void CPlatformer3DPlayer::Hurt()
{
	if ( State->Status.TakeHit() == EPlatformerHit::Died )
	{
		StartDeath();
	}
}

void CPlatformer3DPlayer::StartDeath()
{
	if ( bDying )
	{
		return;
	}
	LOG( LogSamples, Log, "Platformer3D: died at (%.1f, %.1f, %.1f) lives=%d", Position.x, Position.y, Position.z, State->Status.Lives );
	bDying = true;
	DeathTime = 0.0f;
	Velocity = FVector3( 0.0f, 11.0f, 0.0f );
}

void CPlatformer3DPlayer::TickDeath( float32 DeltaTime )
{
	// ぴょんと跳ねてから、落ちていく（箱には当たらない）
	DeathTime += DeltaTime;
	if ( DeathTime > 0.3f )
	{
		Velocity.y = NMath::Max( Velocity.y - FallGravity * DeltaTime, -MaxFallSpeed );
	}
	Position.y += Velocity.y * DeltaTime;
	if ( DeathTime >= DeathSeconds )
	{
		if ( State->Status.Lives <= 0 )
		{
			State->bGameOver = true;
			return;
		}
		StartAgain();
	}
}

void CPlatformer3DPlayer::StartAgain()
{
	bDying = false;
	State->Status.Respawn();
	Position = State->Level.PlayerStart;
	Velocity = FVector3::Zero;
	PreviousFeetY = Position.y;
	State->CameraYaw = 0.0f;
	State->PlayerPosition = Position;
	bOnGround = false;
	UpdateCamera();
}

void CPlatformer3DPlayer::TickGoal( float32 DeltaTime )
{
	// ゴール: その場で、くるくる回る（カメラは、そのまま）
	FacingYaw += 6.0f * DeltaTime;
	UpdateCamera();
}

void CPlatformer3DPlayer::ShootFireball()
{
	if ( GetScene()->FindObjects<CPlatformer3DFireball>().Num() >= MaxFireballs )
	{
		return;
	}
	const FVector3 Direction( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	const FVector3 Start = Position + Direction * 0.6f + FVector3( 0.0f, GetHeight() * 0.5f, 0.0f );
	GetScene()->SpawnObject<CPlatformer3DFireball>( State, Start, Direction );
}

void CPlatformer3DPlayer::Record3D( FDrawContext& CommandList )
{
	const FPlatformerStatus& Status = State->Status;
	// やられたあと、小さくなったあとのひと息は、点滅する
	if ( Status.HurtTime > 0.0f && ( static_cast<int32>( Status.HurtTime * 14.0f ) % 2 ) == 0 )
	{
		return;
	}
	const float32 Height = bDying ? SmallHeight : GetHeight();
	if ( !bDying )
	{
		RecordBlobShadow( CommandList, State->Level, Position, 0.45f );
	}

	// 服の色: ちいさい / おおきい = 赤、ファイア = 白と赤、スター = 虹のように変わる
	FColor32 Body( 214, 52, 44 );
	FColor32 Overall( 52, 92, 214 );
	if ( Status.Form == EPlatformerForm::Fire )
	{
		Body = FColor32( 250, 250, 250 );
		Overall = FColor32( 214, 52, 44 );
	}
	if ( Status.HasStar() )
	{
		Body = FColor::FromHSV( NMath::Frac( State->Time * 6.0f ), 0.6f, 1.0f ).ToColor32();
	}

	const FVector3 Facing( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	const FVector3 Right( Facing.z, 0.0f, -Facing.x );
	const bool bWalking = bOnGround && ( Velocity.x * Velocity.x + Velocity.z * Velocity.z ) > 1.0f;
	const float32 Stride = bWalking ? 0.18f * NMath::Sin( State->Time * 14.0f ) : 0.0f;

	// 脚（歩くと、前後に振れる）
	CommandList.Sphere3D( Position + Right * 0.15f + Facing * Stride + FVector3( 0.0f, 0.12f, 0.0f ), 0.14f, 8, FColor32( 120, 60, 24 ) );
	CommandList.Sphere3D( Position - Right * 0.15f - Facing * Stride + FVector3( 0.0f, 0.12f, 0.0f ), 0.14f, 8, FColor32( 120, 60, 24 ) );
	// 体（下半分がサロペット、上半分が上着）
	const float32 HeadRadius = Height < 1.4f ? 0.2f : 0.26f;
	const float32 BodyTop = Height - HeadRadius * 1.6f;
	const float32 BodyMid = 0.25f + ( BodyTop - 0.25f ) * 0.5f;
	CommandList.Capsule3D( Position + FVector3( 0.0f, 0.25f, 0.0f ), Position + FVector3( 0.0f, BodyMid, 0.0f ), 0.26f, 8, Overall );
	CommandList.Capsule3D( Position + FVector3( 0.0f, BodyMid, 0.0f ), Position + FVector3( 0.0f, BodyTop, 0.0f ), 0.26f, 8, Body );
	// 頭・ぼうし・鼻と目（向きが分かるように）
	const FVector3 Head = Position + FVector3( 0.0f, Height - HeadRadius, 0.0f );
	CommandList.Sphere3D( Head, HeadRadius, 12, FColor32( 250, 208, 160 ) );
	CommandList.Cone3D( Head + FVector3( 0.0f, HeadRadius * 1.1f, 0.0f ), Head + FVector3( 0.0f, HeadRadius * 0.25f, 0.0f ), HeadRadius * 1.05f, 12, Body );
	CommandList.Sphere3D( Head + Facing * ( HeadRadius * 0.95f ) + FVector3( 0.0f, -HeadRadius * 0.05f, 0.0f ), HeadRadius * 0.25f, 6, FColor32( 232, 170, 128 ) );
	CommandList.Sphere3D( Head + Facing * ( HeadRadius * 0.8f ) + Right * ( HeadRadius * 0.4f ) + FVector3( 0.0f, HeadRadius * 0.2f, 0.0f ), HeadRadius * 0.12f, 6, FColor32::Black );
	CommandList.Sphere3D( Head + Facing * ( HeadRadius * 0.8f ) - Right * ( HeadRadius * 0.4f ) + FVector3( 0.0f, HeadRadius * 0.2f, 0.0f ), HeadRadius * 0.12f, 6, FColor32::Black );
}
