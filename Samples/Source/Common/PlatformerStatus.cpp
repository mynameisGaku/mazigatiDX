#include "PlatformerStatus.h"

void FPlatformerStatus::Tick( float32 DeltaTime )
{
	if ( StarTime > 0.0f )
	{
		StarTime = StarTime > DeltaTime ? StarTime - DeltaTime : 0.0f;
	}
	if ( HurtTime > 0.0f )
	{
		HurtTime = HurtTime > DeltaTime ? HurtTime - DeltaTime : 0.0f;
	}
}

void FPlatformerStatus::Collect( EPlatformerItem Item )
{
	switch ( Item )
	{
	case EPlatformerItem::Coin:
		++Coins;
		Score += 200;
		if ( Coins >= 100 )
		{
			Coins -= 100;
			++Lives;
		}
		break;
	case EPlatformerItem::Mushroom:
	case EPlatformerItem::Flower:
		Score += 1000;
		break;
	case EPlatformerItem::Star:
		StarTime = StarSeconds;
		Score += 1000;
		break;
	}
}

void FPlatformerStatus::Die()
{
	if ( Lives > 0 )
	{
		--Lives;
	}
}

void FPlatformerStatus::Respawn()
{
	StarTime = 0.0f;
	HurtTime = 0.0f;
}

void FPlatformerStatus::Reset()
{
	*this = FPlatformerStatus();
}
