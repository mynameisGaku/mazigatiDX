#include "BouncingBox.h"

#include "Library/Framework/Render/ImmediateDraw.h"
#include "Library/Framework/Scene/Scene.h"

CBouncingBox::CBouncingBox( const FVector2& InPosition, const FVector2& InVelocity, int32 InHalfSize, const FColor32& InColor, const FVector2& InAreaMin, const FVector2& InAreaMax )
	: Position( InPosition )
	, Velocity( InVelocity )
	, HalfSize( InHalfSize )
	, Color( InColor )
	, AreaMin( InAreaMin )
	, AreaMax( InAreaMax )
{}

void CBouncingBox::Tick( float32 DeltaTime )
{
	Position = Position + Velocity * DeltaTime;

	// 四角形の端が範囲の端に着いたら、その軸の向きを反対にして、範囲の中へ戻す
	const float32 Half = static_cast<float32>( HalfSize );
	if ( Position.x < AreaMin.x + Half )
	{
		Position.x = AreaMin.x + Half;
		Velocity.x = -Velocity.x;
	}
	else if ( Position.x > AreaMax.x - Half )
	{
		Position.x = AreaMax.x - Half;
		Velocity.x = -Velocity.x;
	}
	if ( Position.y < AreaMin.y + Half )
	{
		Position.y = AreaMin.y + Half;
		Velocity.y = -Velocity.y;
	}
	else if ( Position.y > AreaMax.y - Half )
	{
		Position.y = AreaMax.y - Half;
		Velocity.y = -Velocity.y;
	}
}

void CBouncingBox::Draw()
{
	FImmediateDraw Screen( GetScene()->GetDrawExecutor() );
	const int32 X = static_cast<int32>( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	Screen.Box( X - HalfSize, Y - HalfSize, X + HalfSize, Y + HalfSize, Color );
}
