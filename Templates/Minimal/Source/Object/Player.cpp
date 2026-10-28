#include "Player.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// 画面（1280 x 720）の外へ出ないようにする範囲
	constexpr float32 ScreenWidth = 1280.0f;
	constexpr float32 ScreenHeight = 720.0f;
}

CPlayer::CPlayer( const FVector2& InPosition )
	: Position( InPosition )
{}

void CPlayer::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}

	// 押しているキーから、動く向きを決める（斜めのときは、そのぶん速くならないように揃える）
	FVector2 Direction( 0.0f, 0.0f );
	if ( Input->IsKeyDown( EKey::Left ) || Input->IsKeyDown( EKey::A ) )
	{
		Direction.x -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Right ) || Input->IsKeyDown( EKey::D ) )
	{
		Direction.x += 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Up ) || Input->IsKeyDown( EKey::W ) )
	{
		Direction.y -= 1.0f;
	}
	if ( Input->IsKeyDown( EKey::Down ) || Input->IsKeyDown( EKey::S ) )
	{
		Direction.y += 1.0f;
	}
	if ( Direction.x != 0.0f && Direction.y != 0.0f )
	{
		Direction = Direction * 0.70710678f;
	}

	Position = Position + Direction * ( Speed * DeltaTime );
	Position.x = NMath::Clamp( Position.x, static_cast<float32>( HalfSize ), ScreenWidth - static_cast<float32>( HalfSize ) );
	Position.y = NMath::Clamp( Position.y, static_cast<float32>( HalfSize ), ScreenHeight - static_cast<float32>( HalfSize ) );
}

void CPlayer::Draw( FDrawContext& CommandList )
{
	const int32 X = static_cast<int32>( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	CommandList.Box( X - HalfSize, Y - HalfSize, X + HalfSize, Y + HalfSize, FColor32::Yellow );
}
