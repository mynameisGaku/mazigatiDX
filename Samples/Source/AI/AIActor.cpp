// SPDX-License-Identifier: MIT
#include "AIActor.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 AttackSeconds = 0.5f;
	constexpr float32 AttackHitTime = 0.2f;
	constexpr float32 HurtSeconds = 0.4f;
	constexpr int32 BodyRadius = 22;
}

const FVector2 CAIActor::ArenaMin( 40.0f, 200.0f );
const FVector2 CAIActor::ArenaMax( 1240.0f, 640.0f );

CAIActor::CAIActor( const FString& InName, const FVector2& InPosition, const FColor32& InColor )
	: ActorName( InName )
	, Position( InPosition )
	, Color( InColor )
	, Clip( "Idle" )
{
	// デバッガの一覧に、役の名前（Hero / Guard）が出るように
	SetName( InName );
}

void CAIActor::Tick( float32 DeltaTime )
{
	ClipTime += DeltaTime;
	FlashTime = NMath::Max( FlashTime - DeltaTime, 0.0f );
	if ( Clip == "Attack" && !bHitEventFired && ClipTime >= AttackHitTime )
	{
		bHitEventFired = true;
		bHitEventPending = true;
	}

	Position = Position + Velocity * DeltaTime;
	Position.x = NMath::Clamp( Position.x, ArenaMin.x, ArenaMax.x );
	Position.y = NMath::Clamp( Position.y, ArenaMin.y, ArenaMax.y );
}

bool CAIActor::PlayClip( const FString& InClip, bool bRestart )
{
	if ( !HasClip( InClip ) )
	{
		return false;
	}
	if ( InClip == Clip && !bRestart )
	{
		return true;
	}
	Clip = InClip;
	ClipTime = 0.0f;
	bClipLoops = InClip == "Idle" || InClip == "Run";
	bHitEventPending = false;
	bHitEventFired = false;
	return true;
}

bool CAIActor::IsClipFinished() const
{
	if ( bClipLoops )
	{
		return false;
	}
	return ClipTime >= ( Clip == "Attack" ? AttackSeconds : HurtSeconds );
}

bool CAIActor::ConsumeClipEvent( const FString& Event )
{
	if ( Event == "Hit" && bHitEventPending )
	{
		bHitEventPending = false;
		return true;
	}
	return false;
}

bool CAIActor::HasClip( const FString& InClip ) const
{
	return InClip == "Idle" || InClip == "Run" || InClip == "Attack" || InClip == "Hurt";
}

void CAIActor::SetVelocity( const FVector2& InVelocity )
{
	Velocity = InVelocity;
}

void CAIActor::FaceToward( float32 X )
{
	if ( NMath::Abs( X - Position.x ) > 1.0f )
	{
		Facing = X > Position.x ? 1.0f : -1.0f;
	}
}

void CAIActor::ApplyDamage( int32 Damage )
{
	Hp = NMath::Max( Hp - Damage, 0 );
	FlashTime = 0.25f;
	OnDamaged();
}

void CAIActor::OnDamaged()
{
}

void CAIActor::Draw( FDrawContext& CommandList )
{
	const int32 X = static_cast<int32>( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	const bool bFlash = FlashTime > 0.0f && static_cast<int32>( FlashTime * 24.0f ) % 2 == 0;
	const FColor32 Body = bFlash ? FColor32::White : Color;

	// クリップごとの見た目: Attack は体が前へ伸び、Run は上下に弾み、Hurt は縮む
	int32 Radius = BodyRadius;
	int32 Bounce = 0;
	if ( Clip == "Run" )
	{
		Bounce = static_cast<int32>( NMath::Abs( NMath::Sin( ClipTime * 14.0f ) ) * 5.0f );
	}
	else if ( Clip == "Hurt" )
	{
		Radius -= 5;
	}
	CommandList.Circle( X, Y - Bounce, Radius, Body, true );
	CommandList.Circle( X, Y - Bounce, Radius, FColor32::Black, false, 2 );
	const int32 FrontX = X + static_cast<int32>( Facing * static_cast<float32>( Radius ) );
	CommandList.Circle( FrontX, Y - Bounce - 6, 5, FColor32::Black, true );
	if ( Clip == "Attack" )
	{
		const float32 Reach = NMath::Min( ClipTime / AttackHitTime, 1.0f ) * 36.0f;
		CommandList.Line( FrontX, Y - Bounce, FrontX + static_cast<int32>( Facing * Reach ), Y - Bounce, FColor32::Yellow, 4 );
	}

	// 名前・クリップ・体力
	CommandList.Text( X - 30, Y - Radius - 38, *ActorName, FColor32::White );
	CommandList.TextF( X - 30, Y - Radius - 22, FColor32::Gray, "%s HP %d", *Clip, Hp );
}
