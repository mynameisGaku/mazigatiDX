#include "Platformer3DPlayer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
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
	// 手応え: 敵を踏んだとき・ダメージを受けたときに、時間を止める長さ（秒）と、画面の揺れ（画素）をワールドの長さに直す倍率
	constexpr float32 StompHitStop = 0.07f;
	constexpr float32 HurtHitStop = 0.14f;
	constexpr float32 ShakeWorldScale = 0.012f;
	// カメラが見る高さが、主人公の高さを追う速さ（1 秒あたりの割合。地面の上では速く、空中では遅く）
	constexpr float32 FocusFollowGround = 8.0f;
	constexpr float32 FocusFollowAir = 1.5f;
	constexpr float32 StompBounce = 6.0f;
	constexpr float32 StompBounceHeld = 10.0f;

	constexpr int32 MaxFireballs = 2;

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

	// 最初の姿（--form=big / fire）は、ステートマシンを進めて作る（キノコ・花のイベントを、1 フレームに 1 つ送る）
	const EPlatformerForm StartForm = State->Status.Form;
	State->Status.Form = EPlatformerForm::Small;
	if ( StartForm == EPlatformerForm::Big )
	{
		StartEvents.Add( "Mushroom" );
	}
	else if ( StartForm == EPlatformerForm::Fire )
	{
		StartEvents.Add( "Flower" );
		StartEvents.Add( "Flower" );
	}

	// 自動操作（--autoplay）: ビヘイビアツリーを、先に付けて読む（ブラックボードを作る側。ステートマシンは、それを共有する。
	// トリガー（JumpPressed など）は、あとから読んだ側 = ステートマシンが、動いたあとに戻す。ツリーが書いた操作を、ステートマシンが読める）
	if ( State->bAutoPlay )
	{
		Pilot = AddComponent<CBehaviorTreeComponent>( "Platformer/Autopilot3D.gxbt" );
		Pilot->Load();
	}

	// 状態のステートマシン（階層。2D と同じファイル）。すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Machine = AddComponent<CStateMachineComponent>( "Platformer/Player.gxstate" );
	Machine->Load();
}

void CPlatformer3DPlayer::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	State->Status.Tick( DeltaTime );

	// センサー: ステートマシンの遷移の条件に使う値を、ブラックボードへ書く
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	if ( Blackboard != nullptr )
	{
		Blackboard->SetInt( Blackboard->FindKey( "Lives" ), State->Status.Lives );
		Blackboard->SetBool( Blackboard->FindKey( "IsBig" ), State->Status.IsBig() );
	}
	// ためておいたイベントを、ステートマシンへ送る（次のフレームの、遷移の条件になる）
	for ( const FString& Event : OutgoingEvents )
	{
		Machine->SendEvent( Event );
	}
	OutgoingEvents.Reset();
	if ( !StartEvents.IsEmpty() )
	{
		Machine->SendEvent( StartEvents[0] );
		StartEvents.RemoveAt( 0 );
	}
}

float32 CPlatformer3DPlayer::GetHeight() const
{
	return State->Status.IsBig() ? BigHeight : SmallHeight;
}

CPlatformer3DPlayer::FControls CPlatformer3DPlayer::ReadControls( float32 DeltaTime )
{
	if ( State->bAutoPlay )
	{
		return ReadPilotControls();
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

CPlatformer3DPlayer::FControls CPlatformer3DPlayer::ReadPilotControls()
{
	// 自動操作: ビヘイビアツリー（Autopilot3D.gxbt）が、ブラックボードへ書いた操作を読む（人のキー入力と同じ入口）
	FControls Controls;
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	if ( Blackboard == nullptr )
	{
		return Controls;
	}
	Controls.Move = FVector2( Blackboard->GetFloat( Blackboard->FindKey( "MoveX" ) ), Blackboard->GetFloat( Blackboard->FindKey( "MoveY" ) ) );
	Controls.bRun = Blackboard->GetBool( Blackboard->FindKey( "Run" ) );
	Controls.bJumpPressed = Blackboard->GetBool( Blackboard->FindKey( "JumpPressed" ) );
	Controls.bJumpHeld = Blackboard->GetBool( Blackboard->FindKey( "JumpHeld" ) );
	Controls.bFirePressed = Blackboard->GetBool( Blackboard->FindKey( "FirePressed" ) );
	return Controls;
}

void CPlatformer3DPlayer::Sense( CBlackboard& Blackboard )
{
	// 自動操作のセンサー: 壁・穴・敵・頭上のブロックが近ければ、跳ぶべき。近くにアイテムがあれば、取りに行く
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
	Blackboard.SetBool( Blackboard.FindKey( "ShouldJump" ), bOnGround && ( bWall || bPit || bEnemy || bBlockAbove ) );
	Blackboard.SetBool( Blackboard.FindKey( "CanShoot" ), State->Status.CanShoot() );

	bool bItemNear = false;
	FVector2 ItemMove;
	for ( const TSharedPtr<CPlatformer3DPowerUp>& PowerUp : GetScene()->FindObjects<CPlatformer3DPowerUp>() )
	{
		const FVector3 Delta = PowerUp->GetPosition() - Position;
		const float32 Flat = NMath::Sqrt( Delta.x * Delta.x + Delta.z * Delta.z );
		if ( !PowerUp->IsEmerging() && Flat < 7.0f && Flat > 0.1f && NMath::Abs( Delta.y ) < 3.0f )
		{
			bItemNear = true;
			ItemMove = FVector2( Delta.x / Flat, Delta.z / Flat );
		}
	}
	Blackboard.SetBool( Blackboard.FindKey( "ItemNear" ), bItemNear );
	Blackboard.SetFloat( Blackboard.FindKey( "ItemMoveX" ), ItemMove.x );
	Blackboard.SetFloat( Blackboard.FindKey( "ItemMoveY" ), ItemMove.y );
}

void CPlatformer3DPlayer::StepAlive( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
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
	if ( Controls.bFirePressed && State->Status.CanShoot() )
	{
		ShootFireball();
	}

	// 穴に落ちた: やられる（姿にも、無敵にも関係なく）
	if ( Position.y < FPlatformer3DLevel::KillHeight )
	{
		PostEvent( "Died" );
		return;
	}
	// ゴール: ポールの近くへ来た
	const float32 GoalDX = Position.x - State->Level.GoalPosition.x;
	const float32 GoalDZ = Position.z - State->Level.GoalPosition.z;
	if ( GoalDX * GoalDX + GoalDZ * GoalDZ < GoalRadius * GoalRadius && Position.y < State->Level.GoalPosition.y + 6.0f )
	{
		PostEvent( "Goal" );
		return;
	}
	State->PlayerPosition = Position;
	UpdateCamera();
}

void CPlatformer3DPlayer::UpdateCamera()
{
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	// カメラが見る高さは、主人公にくっつけない: 空中では、ゆっくり追う（ジャンプの高さが、画面の中の位置で分かる）。地面に立つと、すぐ追いつく
	const float32 FrameTime = GetScene() != nullptr ? GetScene()->GetUnscaledDeltaTime() : 0.0f;
	if ( State->CameraFocusY < 0.0f )
	{
		State->CameraFocusY = Position.y;
	}
	State->CameraFocusY += ( Position.y - State->CameraFocusY ) * NMath::Min( 1.0f, ( bOnGround ? FocusFollowGround : FocusFollowAir ) * FrameTime );
	// 画面の揺れ（画素）を、ワールドの長さに直して、カメラの位置と注視点の両方に足す
	const FVector2 ShakeOffset = State->Shake.GetOffset();
	const FVector3 Right( NMath::Cos( Yaw ), 0.0f, -NMath::Sin( Yaw ) );
	const FVector3 Shake = Right * ( ShakeOffset.x * ShakeWorldScale ) + FVector3( 0.0f, ShakeOffset.y * ShakeWorldScale, 0.0f );
	const FVector3 Look = FVector3( Position.x, State->CameraFocusY + 1.0f, Position.z ) + Shake;
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
			// 点数などは Status へ、姿の変化は、ステートマシンへ（イベント）
			State->Status.Collect( PowerUp->GetItem() );
			if ( PowerUp->GetItem() == EPlatformerItem::Mushroom )
			{
				PostEvent( "Mushroom" );
			}
			else if ( PowerUp->GetItem() == EPlatformerItem::Flower )
			{
				PostEvent( "Flower" );
			}
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
			// 手応え: 踏んだ瞬間に、時間を一瞬止めて、画面を小さく揺らす
			GetScene()->StartHitStop( StompHitStop );
			State->Shake.Start( EScreenShakeStrength::Small );
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
		if ( Enemy->IsDeadly() && !Status.IsInvincible() )
		{
			// ダメージ: 小さくなるか、やられるかは、ステートマシン（いまの姿）が決める
			PostEvent( "Hurt" );
			GetScene()->StartHitStop( HurtHitStop );
			State->Shake.Start( EScreenShakeStrength::Medium );
		}
	}
}

//-----------------------------------------------------------------------------
// ステートの動き: 姿・やられた・ゴール・ゲームオーバー
//-----------------------------------------------------------------------------
void CPlatformer3DPlayer::OnFormEntered( EPlatformerForm Form )
{
	State->Status.Form = Form;
}

void CPlatformer3DPlayer::BeginHurtBreather()
{
	State->Status.GrantBreather();
}

void CPlatformer3DPlayer::EnterAlive()
{
	if ( !bRespawnPending )
	{
		return;
	}
	// やられたあと、続きから: 最初の位置へ戻る（姿は、Alive の最初のステート = ちいさい）
	bRespawnPending = false;
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

void CPlatformer3DPlayer::BeginDeath()
{
	LOG( LogSamples, Log, "Platformer3D: died at (%.1f, %.1f, %.1f) lives=%d", Position.x, Position.y, Position.z, State->Status.Lives );
	State->Status.Die();
	bDying = true;
	bRespawnPending = true;
	DeathTime = 0.0f;
	Velocity = FVector3( 0.0f, 11.0f, 0.0f );
}

void CPlatformer3DPlayer::StepDying( float32 DeltaTime )
{
	// ぴょんと跳ねてから、落ちていく（箱には当たらない）。続きから始める / ゲームオーバーは、ステートマシンの遷移
	DeathTime += DeltaTime;
	if ( DeathTime > 0.3f )
	{
		Velocity.y = NMath::Max( Velocity.y - FallGravity * DeltaTime, -MaxFallSpeed );
	}
	Position.y += Velocity.y * DeltaTime;
}

void CPlatformer3DPlayer::BeginGoal()
{
	State->bGoalReached = true;
	State->Status.AddScore( 5000 );
	Velocity = FVector3::Zero;
}

void CPlatformer3DPlayer::StepGoal( float32 DeltaTime )
{
	// ゴール: その場で、くるくる回る（カメラは、そのまま）
	FacingYaw += 6.0f * DeltaTime;
	UpdateCamera();
}

void CPlatformer3DPlayer::BeginGameOver()
{
	State->bGameOver = true;
}

void CPlatformer3DPlayer::PostEvent( const char* Name )
{
	OutgoingEvents.Add( Name );
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
