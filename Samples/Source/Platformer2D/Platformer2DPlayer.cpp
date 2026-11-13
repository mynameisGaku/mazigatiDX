#include "Platformer2DPlayer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer2D/Platformer2DCoinPop.h"
#include "Source/Platformer2D/Platformer2DEnemy.h"
#include "Source/Platformer2D/Platformer2DFireball.h"
#include "Source/Platformer2D/Platformer2DPowerUp.h"
#include "Source/Platformer2D/Platformer2DState.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	constexpr float32 HalfWidth = 13.0f;
	constexpr float32 SmallHeight = 30.0f;
	constexpr float32 BigHeight = 58.0f;

	// 走り方（ピクセル / 秒）
	constexpr float32 WalkSpeed = 200.0f;
	constexpr float32 RunSpeed = 330.0f;
	constexpr float32 GroundAccel = 1500.0f;
	constexpr float32 AirAccel = 1000.0f;
	constexpr float32 GroundBrake = 1900.0f;
	constexpr float32 AirBrake = 260.0f;

	// ジャンプ。ボタンを押している間は、重力が小さい（高く跳べる）。離すと、すぐに落ち始める
	constexpr float32 JumpSpeed = 780.0f;
	constexpr float32 HoldGravity = 1900.0f;
	constexpr float32 FallGravity = 3100.0f;
	constexpr float32 MaxFallSpeed = 900.0f;
	// 手応え: 敵を踏んだとき・ダメージを受けたときに、時間を止める長さ（秒）
	constexpr float32 StompHitStop = 0.07f;
	constexpr float32 HurtHitStop = 0.14f;
	constexpr float32 StompBounce = 420.0f;
	constexpr float32 StompBounceHeld = 700.0f;

	constexpr int32 MaxFireballs = 2;

	float32 MoveToward( float32 Value, float32 Target, float32 MaxDelta )
	{
		if ( Value < Target )
		{
			return NMath::Min( Value + MaxDelta, Target );
		}
		return NMath::Max( Value - MaxDelta, Target );
	}
}

CPlatformer2DPlayer::CPlatformer2DPlayer( const TSharedPtr<FPlatformer2DState>& InState )
	: State( InState )
{}

void CPlatformer2DPlayer::OnSpawn()
{
	Position = State->Level.PlayerStart;
	PreviousFeetY = Position.y;

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
		Pilot = AddComponent<CBehaviorTreeComponent>( "Platformer/Autopilot2D.gxbt" );
		Pilot->Load();
	}

	// 状態のステートマシン（階層）。すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Machine = AddComponent<CStateMachineComponent>( "Platformer/Player.gxstate" );
	Machine->Load();
}

float32 CPlatformer2DPlayer::GetHeight() const
{
	return State->Status.IsBig() ? BigHeight : SmallHeight;
}

void CPlatformer2DPlayer::Tick( float32 DeltaTime )
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

CPlatformer2DPlayer::FControls CPlatformer2DPlayer::ReadControls( float32 DeltaTime )
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
	if ( Input->IsKeyDown( EKey::Left ) || Input->IsKeyDown( EKey::A ) )
	{
		Controls.Move -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Right ) || Input->IsKeyDown( EKey::D ) )
	{
		Controls.Move += 1.0f;
	}
	Controls.bRun = Input->IsKeyDown( EKey::LeftShift ) || Input->IsKeyDown( EKey::RightShift );
	Controls.bJumpPressed = Input->IsKeyPressed( EKey::Space ) || Input->IsKeyPressed( EKey::Z );
	Controls.bJumpHeld = Input->IsKeyDown( EKey::Space ) || Input->IsKeyDown( EKey::Z );
	Controls.bFirePressed = Input->IsKeyPressed( EKey::X ) || Input->IsKeyPressed( EKey::J );
	return Controls;
}

CPlatformer2DPlayer::FControls CPlatformer2DPlayer::ReadPilotControls()
{
	// 自動操作: ビヘイビアツリー（Autopilot2D.gxbt）が、ブラックボードへ書いた操作を読む（人のキー入力と同じ入口）
	FControls Controls;
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	if ( Blackboard == nullptr )
	{
		return Controls;
	}
	Controls.Move = Blackboard->GetFloat( Blackboard->FindKey( "MoveX" ) );
	Controls.bRun = Blackboard->GetBool( Blackboard->FindKey( "Run" ) );
	Controls.bJumpPressed = Blackboard->GetBool( Blackboard->FindKey( "JumpPressed" ) );
	Controls.bJumpHeld = Blackboard->GetBool( Blackboard->FindKey( "JumpHeld" ) );
	Controls.bFirePressed = Blackboard->GetBool( Blackboard->FindKey( "FirePressed" ) );
	return Controls;
}

void CPlatformer2DPlayer::Sense( CBlackboard& Blackboard )
{
	// 自動操作のセンサー: 壁・穴・敵・頭上のブロックが近ければ、跳ぶべき。近くにアイテムがあれば、取りに行く
	const FPlatformer2DLevel& Level = State->Level;
	const float32 Ahead = Position.x + HalfWidth + 26.0f;
	const bool bWall = Level.IsSolid( FPlatformer2DLevel::ToTile( Ahead ), FPlatformer2DLevel::ToTile( Position.y - 8.0f ) );
	const float32 GroundProbe = Position.x + HalfWidth + 20.0f;
	const bool bPit = bOnGround && !Level.IsSolid( FPlatformer2DLevel::ToTile( GroundProbe ), FPlatformer2DLevel::ToTile( Position.y + 4.0f ) );
	bool bEnemy = false;
	for ( const TSharedPtr<CPlatformer2DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer2DEnemy>() )
	{
		const float32 DX = Enemy->GetPosition().x - Position.x;
		if ( Enemy->IsAlive() && DX > -10.0f && DX < 170.0f && NMath::Abs( Enemy->GetPosition().y - Position.y ) < 40.0f )
		{
			bEnemy = true;
		}
	}
	const char Above = Level.Get( FPlatformer2DLevel::ToTile( Position.x + 20.0f ), 15 );
	const bool bBlockAbove = Above == '?' || Above == 'M' || Above == '*';
	Blackboard.SetBool( Blackboard.FindKey( "ShouldJump" ), bOnGround && ( bWall || bPit || bEnemy || bBlockAbove ) );
	Blackboard.SetBool( Blackboard.FindKey( "CanShoot" ), State->Status.CanShoot() );

	bool bItemNear = false;
	float32 ItemMove = 0.0f;
	for ( const TSharedPtr<CPlatformer2DPowerUp>& PowerUp : GetScene()->FindObjects<CPlatformer2DPowerUp>() )
	{
		const float32 DX = PowerUp->GetPosition().x - Position.x;
		if ( !PowerUp->IsEmerging() && NMath::Abs( DX ) < 260.0f )
		{
			bItemNear = true;
			ItemMove = DX >= 0.0f ? 1.0f : -1.0f;
		}
	}
	// 敵が近いときは、アイテムより、よけるのが先（取りに行かない）
	Blackboard.SetBool( Blackboard.FindKey( "ItemNear" ), bItemNear && !bEnemy );
	Blackboard.SetFloat( Blackboard.FindKey( "ItemMoveX" ), ItemMove );
}

//-----------------------------------------------------------------------------
// ステートの動き: 生きている間
//-----------------------------------------------------------------------------
void CPlatformer2DPlayer::StepAlive( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	const FControls Controls = ReadControls( DeltaTime );
	if ( Controls.Move != 0.0f )
	{
		Facing = Controls.Move;
	}

	// 横の動き: 目標の速さへ、加速（向きを変えるときは、ブレーキ）
	const float32 MaxSpeed = Controls.bRun ? RunSpeed : WalkSpeed;
	const float32 Target = Controls.Move * MaxSpeed;
	float32 Rate = bOnGround ? GroundAccel : AirAccel;
	if ( Controls.Move == 0.0f || ( Controls.Move * Velocity.x < 0.0f ) )
	{
		Rate = bOnGround ? GroundBrake : AirBrake;
	}
	Velocity.x = MoveToward( Velocity.x, Target, Rate * DeltaTime );

	// ジャンプ（速く走っているほど、少し高く跳べる）と、重力
	if ( Controls.bJumpPressed && bOnGround )
	{
		Velocity.y = -( JumpSpeed + NMath::Abs( Velocity.x ) * 0.12f );
		bOnGround = false;
	}
	const float32 Gravity = ( Velocity.y < 0.0f && Controls.bJumpHeld ) ? HoldGravity : FallGravity;
	Velocity.y = NMath::Min( Velocity.y + Gravity * DeltaTime, MaxFallSpeed );
	AirTime = bOnGround ? 0.0f : AirTime + DeltaTime;

	PreviousFeetY = Position.y;
	const FTile2DMoveResult Result = State->Level.Move( Position, HalfWidth, GetHeight(), Velocity, DeltaTime );
	bOnGround = Result.bOnGround;
	if ( Result.bHitCeiling )
	{
		HitBlock( Result.CeilingTileX, Result.CeilingTileY );
	}
	// 面の左端より後ろへは戻れない（カメラの左端も、壁になる）
	if ( Position.x < State->CameraX + HalfWidth )
	{
		Position.x = State->CameraX + HalfWidth;
		Velocity.x = NMath::Max( Velocity.x, 0.0f );
	}

	CollectTouching();
	HandleEnemies( Controls.bJumpHeld );
	if ( Controls.bFirePressed && State->Status.CanShoot() )
	{
		ShootFireball();
	}

	// 穴に落ちた: やられる（姿にも、無敵にも関係なく）
	if ( Position.y > static_cast<float32>( ( State->Level.Height + 2 ) * FPlatformer2DLevel::TileSize ) )
	{
		PostEvent( "Died" );
		return;
	}
	// ゴール
	if ( Position.x >= State->Level.GoalX - 20.0f )
	{
		PostEvent( "Goal" );
		return;
	}
	UpdateCamera();
}

void CPlatformer2DPlayer::UpdateCamera()
{
	// 主人公が画面の左から 40% より右へ行ったら、カメラが追う。戻らない。面の端で止まる
	const float32 Wanted = Position.x - static_cast<float32>( FPlatformer2DState::ScreenWidth ) * 0.4f;
	const float32 Limit = static_cast<float32>( State->Level.Width * FPlatformer2DLevel::TileSize - FPlatformer2DState::ScreenWidth );
	State->CameraX = NMath::Clamp( NMath::Max( State->CameraX, Wanted ), 0.0f, Limit );
}

void CPlatformer2DPlayer::HitBlock( int32 TileX, int32 TileY )
{
	const bool bBig = State->Status.IsBig();
	const ETile2DHit Hit = State->Level.Hit( TileX, TileY, bBig );
	const float32 Size = static_cast<float32>( FPlatformer2DLevel::TileSize );
	const FVector2 BlockTop( ( static_cast<float32>( TileX ) + 0.5f ) * Size, static_cast<float32>( TileY ) * Size );
	switch ( Hit )
	{
	case ETile2DHit::Coin:
		State->Status.Collect( EPlatformerItem::Coin );
		GetScene()->SpawnObject<CPlatformer2DCoinPop>( State, BlockTop )->SetDrawOrder( 15 );
		break;
	case ETile2DHit::PowerUp:
		// ちいさいときはキノコ、おおきいときは花
		GetScene()->SpawnObject<CPlatformer2DPowerUp>( State, bBig ? EPlatformerItem::Flower : EPlatformerItem::Mushroom, BlockTop )->SetDrawOrder( 5 );
		break;
	case ETile2DHit::Star:
		GetScene()->SpawnObject<CPlatformer2DPowerUp>( State, EPlatformerItem::Star, BlockTop )->SetDrawOrder( 5 );
		break;
	case ETile2DHit::Broke:
		State->Status.AddScore( 50 );
		break;
	default:
		break;
	}
	if ( Hit == ETile2DHit::None )
	{
		return;
	}
	// 叩いたブロックの上に乗っていた敵は、はね飛ばされる
	for ( const TSharedPtr<CPlatformer2DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer2DEnemy>() )
	{
		const FVector2& At = Enemy->GetPosition();
		if ( Enemy->IsAlive() && NMath::Abs( At.x - BlockTop.x ) < Size * 0.75f && NMath::Abs( At.y - BlockTop.y ) < 6.0f )
		{
			Enemy->KnockOff( At.x >= Position.x ? 1.0f : -1.0f );
			State->Status.AddScore( 100 );
		}
	}
}

void CPlatformer2DPlayer::CollectTouching()
{
	// 通れるコイン
	int32 TileX = 0;
	int32 TileY = 0;
	while ( State->Level.FindTileTouching( 'o', Position, HalfWidth, GetHeight(), TileX, TileY ) )
	{
		State->Level.Set( TileX, TileY, ' ' );
		State->Status.Collect( EPlatformerItem::Coin );
	}
	// 出てきたアイテム: 点数などは Status へ、姿の変化は、ステートマシンへ（イベント）
	for ( const TSharedPtr<CPlatformer2DPowerUp>& PowerUp : GetScene()->FindObjects<CPlatformer2DPowerUp>() )
	{
		if ( PowerUp->IsEmerging() )
		{
			continue;
		}
		const FVector2& At = PowerUp->GetPosition();
		const bool bTouch = NMath::Abs( At.x - Position.x ) < HalfWidth + CPlatformer2DPowerUp::HalfWidth && At.y > Position.y - GetHeight() && At.y - CPlatformer2DPowerUp::Height < Position.y;
		if ( bTouch )
		{
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

void CPlatformer2DPlayer::HandleEnemies( bool bJumpHeld )
{
	FPlatformerStatus& Status = State->Status;
	for ( const TSharedPtr<CPlatformer2DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer2DEnemy>() )
	{
		if ( !Enemy->IsAlive() || !Enemy->Overlaps( Position, HalfWidth, GetHeight() ) )
		{
			continue;
		}
		const float32 Away = Enemy->GetPosition().x >= Position.x ? 1.0f : -1.0f;
		if ( Status.HasStar() )
		{
			// スターのあいだは、ふれるだけで倒せる
			Enemy->KnockOff( Away );
			Status.AddScore( 200 );
			continue;
		}
		// 踏んだ: 落ちてきて、足が敵の上のほうにある
		const bool bStomp = Velocity.y > 0.0f && PreviousFeetY <= Enemy->GetTopY() + 14.0f;
		if ( bStomp )
		{
			const bool bWasStill = Enemy->IsStill();
			Enemy->OnStomped( Position.x );
			Velocity.y = -( bJumpHeld ? StompBounceHeld : StompBounce );
			// 手応え: 踏んだ瞬間に、時間を一瞬止める
			GetScene()->StartHitStop( StompHitStop );
			Status.AddScore( bWasStill ? 400 : 100 );
			continue;
		}
		if ( Enemy->IsStill() )
		{
			// 止まった甲羅に、横からふれた: 蹴り出す
			Enemy->OnKicked( Position.x );
			Status.AddScore( 400 );
			continue;
		}
		if ( Enemy->IsDeadly() && !Status.IsInvincible() )
		{
			// ダメージ: 小さくなるか、やられるかは、ステートマシン（いまの姿）が決める
			PostEvent( "Hurt" );
			GetScene()->StartHitStop( HurtHitStop );
		}
	}
}

//-----------------------------------------------------------------------------
// ステートの動き: 姿・やられた・ゴール・ゲームオーバー
//-----------------------------------------------------------------------------
void CPlatformer2DPlayer::OnFormEntered( EPlatformerForm Form )
{
	State->Status.Form = Form;
}

void CPlatformer2DPlayer::BeginHurtBreather()
{
	State->Status.GrantBreather();
}

void CPlatformer2DPlayer::EnterAlive()
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
	Velocity = FVector2::Zero;
	PreviousFeetY = Position.y;
	State->CameraX = 0.0f;
	bOnGround = false;
}

void CPlatformer2DPlayer::BeginDeath()
{
	LOG( LogSamples, Log, "Platformer2D: died at x=%.0f y=%.0f lives=%d", Position.x, Position.y, State->Status.Lives );
	State->Status.Die();
	bDying = true;
	bRespawnPending = true;
	DeathTime = 0.0f;
	Velocity = FVector2( 0.0f, -760.0f );
}

void CPlatformer2DPlayer::StepDying( float32 DeltaTime )
{
	// ぴょんと跳ねてから、落ちていく（タイルには当たらない）。続きから始める / ゲームオーバーは、ステートマシンの遷移
	DeathTime += DeltaTime;
	if ( DeathTime > 0.4f )
	{
		Velocity.y = NMath::Min( Velocity.y + FallGravity * DeltaTime, MaxFallSpeed );
		Position.y += Velocity.y * DeltaTime;
	}
}

void CPlatformer2DPlayer::BeginGoal()
{
	State->bGoalReached = true;
	State->Status.AddScore( 5000 );
	Velocity = FVector2::Zero;
}

void CPlatformer2DPlayer::StepGoal( float32 DeltaTime )
{
	// ポールをすべり降りて、地面に着いたら、止まる
	const float32 GroundY = static_cast<float32>( 19 * FPlatformer2DLevel::TileSize );
	Position.x = State->Level.GoalX - 16.0f;
	Position.y = NMath::Min( Position.y + 220.0f * DeltaTime, GroundY );
}

void CPlatformer2DPlayer::BeginGameOver()
{
	State->bGameOver = true;
}

void CPlatformer2DPlayer::PostEvent( const char* Name )
{
	OutgoingEvents.Add( Name );
}

void CPlatformer2DPlayer::ShootFireball()
{
	if ( GetScene()->FindObjects<CPlatformer2DFireball>().Num() >= MaxFireballs )
	{
		return;
	}
	const FVector2 Start( Position.x + Facing * 16.0f, Position.y - GetHeight() * 0.5f );
	GetScene()->SpawnObject<CPlatformer2DFireball>( State, Start, Facing )->SetDrawOrder( 25 );
}

void CPlatformer2DPlayer::Draw( FDrawContext& CommandList )
{
	const FPlatformerStatus& Status = State->Status;
	// やられたあと、小さくなったあとのひと息は、点滅する
	if ( Status.HurtTime > 0.0f && ( static_cast<int32>( Status.HurtTime * 14.0f ) % 2 ) == 0 )
	{
		return;
	}
	const int32 X = State->ToScreenX( Position.x );
	const int32 Feet = static_cast<int32>( Position.y );
	const int32 Height = static_cast<int32>( bDying ? SmallHeight : GetHeight() );
	const int32 Top = Feet - Height;

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
	const bool bWalking = bOnGround && NMath::Abs( Velocity.x ) > 20.0f;
	const int32 Stride = bWalking ? ( ( static_cast<int32>( State->Time * 12.0f ) % 2 ) == 0 ? 4 : -4 ) : 0;

	// 頭の大きさは、姿の高さに合わせる（ちいさい姿でも、体と脚が見えるように）
	const int32 HeadSize = Height < 40 ? 9 : 13;
	const int32 HeadY = Top + HeadSize;
	const int32 BodyTop = HeadY + HeadSize - 3;
	const int32 BodyBottom = Feet - ( Height < 40 ? 6 : 8 );
	const int32 Face = static_cast<int32>( Facing );
	// 脚
	CommandList.Box( X - 12 + Stride, BodyBottom, X - 1 + Stride, Feet, FColor32( 120, 60, 24 ), true );
	CommandList.Box( X + 1 - Stride, BodyBottom, X + 12 - Stride, Feet, FColor32( 120, 60, 24 ), true );
	// 体（上着・サロペット）
	CommandList.Box( X - 12, BodyTop, X + 12, BodyBottom, Body, true );
	CommandList.Box( X - 12, BodyTop + ( BodyBottom - BodyTop ) / 2, X + 12, BodyBottom, Overall, true );
	// 頭・ぼうし・目
	CommandList.Circle( X, HeadY, HeadSize, FColor32( 250, 208, 160 ), true );
	CommandList.Box( X - HeadSize, Top, X + HeadSize, HeadY - 4, Body, true );
	CommandList.Box( X + ( Face > 0 ? 2 : -HeadSize - 6 ), HeadY - 6, X + ( Face > 0 ? HeadSize + 6 : -2 ), HeadY - 3, Body, true );
	CommandList.Circle( X + Face * 6, HeadY, 2, FColor32::Black, true );
}
