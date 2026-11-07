#include "PanBall.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Audio/ToneMaker.h"

namespace
{
	constexpr int32 Radius = 22;

	// 1 往復にかかる秒
	constexpr float32 Period = 4.0f;
}

CPanBall::CPanBall( const FVector2& InCenter, float32 InHalfWidth )
	: Center( InCenter )
	, HalfWidth( InHalfWidth )
{
	CurrentX = Center.x;
}

void CPanBall::OnSpawn()
{
	// 220 Hz のサイン波（1 秒でちょうど波が閉じるので、ループしても途切れない）。小さめの音量
	Tone = NToneMaker::MakeSound( "PanBallTone", NToneMaker::MakeLoopTone( 220.0f, 0.25f ) );
}

void CPanBall::Toggle()
{
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Audio == nullptr )
	{
		return;
	}
	if ( Handle.IsValid() && Audio->IsPlaying( Handle ) )
	{
		// 0.3 秒かけて小さくしながら止める（急に止めると、プチッと鳴る）
		Audio->Stop( Handle, 0.3f );
		Handle = FSoundHandle();
		return;
	}
	if ( Tone == nullptr )
	{
		return;
	}
	FSoundPlayParams Params;
	Params.bLoop = true;
	Params.FadeInTime = 0.2f;
	Handle = Audio->Play( Tone, Params );
}

void CPanBall::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
	// 左右になめらかに行き来する（サイン波の位置）
	const float32 Phase = NMath::Sin( NMath::TWO_PI * Time / Period );
	CurrentX = Center.x + HalfWidth * Phase;

	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Audio == nullptr || Input == nullptr )
	{
		return;
	}
	bool bClickedBall = false;
	if ( Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		const float32 DX = static_cast<float32>( Mouse.X ) - CurrentX;
		const float32 DY = static_cast<float32>( Mouse.Y ) - Center.y;
		bClickedBall = DX * DX + DY * DY <= static_cast<float32>( Radius * Radius );
	}
	if ( Input->IsKeyPressed( EKey::Enter ) || bClickedBall )
	{
		Toggle();
	}
	if ( Handle.IsValid() )
	{
		// 位置を、-1（左）..1（右）にして、パンに渡す
		Audio->SetPan( Handle, Phase );
	}
}

void CPanBall::Draw( FDrawContext& CommandList )
{
	const int32 Left = static_cast<int32>( Center.x - HalfWidth ) - Radius;
	const int32 Right = static_cast<int32>( Center.x + HalfWidth ) + Radius;
	const int32 Y = static_cast<int32>( Center.y );
	CommandList.Line( Left, Y, Right, Y, FColor32( 70, 76, 92 ), 2 );
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	const bool bSounding = Audio != nullptr && Handle.IsValid() && Audio->IsPlaying( Handle );
	const FColor32 Color = bSounding ? FColor32( 90, 200, 140 ) : FColor32( 120, 126, 140 );
	CommandList.Circle( static_cast<int32>( CurrentX ), Y, Radius, Color, true );
	CommandList.Text( Left, Y + 34, bSounding ? "Pan ball: sounding (Enter / click: stop)" : "Pan ball: silent (Enter / click: start)", FColor32::Gray );
}

void CPanBall::OnDestroy()
{
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Audio != nullptr && Handle.IsValid() )
	{
		Audio->Stop( Handle );
	}
	Handle = FSoundHandle();
}
