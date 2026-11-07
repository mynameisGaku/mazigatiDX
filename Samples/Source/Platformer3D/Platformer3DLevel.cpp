#include "Platformer3DLevel.h"

namespace
{
	constexpr float32 Epsilon = 0.0005f;

	bool OverlapsBlock( const FBlock3D& Block, const FVector3& Position, float32 HalfWidth, float32 Height )
	{
		return Block.IsActive
			&& Position.x + HalfWidth > Block.Min.x + Epsilon && Position.x - HalfWidth < Block.Max.x - Epsilon
			&& Position.z + HalfWidth > Block.Min.z + Epsilon && Position.z - HalfWidth < Block.Max.z - Epsilon
			&& Position.y + Height > Block.Min.y + Epsilon && Position.y < Block.Max.y - Epsilon;
	}
}

void FPlatformer3DLevel::Add( const FVector3& Min, const FVector3& Max, EBlock3DKind Kind )
{
	FBlock3D Block;
	Block.Min = Min;
	Block.Max = Max;
	Block.Kind = Kind;
	Blocks.Add( Block );
}

bool FPlatformer3DLevel::Overlaps( const FVector3& Position, float32 HalfWidth, float32 Height ) const
{
	for ( const FBlock3D& Block : Blocks )
	{
		if ( OverlapsBlock( Block, Position, HalfWidth, Height ) )
		{
			return true;
		}
	}
	return false;
}

FBlock3DMoveResult FPlatformer3DLevel::Move( FVector3& Position, float32 HalfWidth, float32 Height, FVector3& Velocity, float32 DeltaTime ) const
{
	FBlock3DMoveResult Result;

	// X
	Position.x += Velocity.x * DeltaTime;
	for ( const FBlock3D& Block : Blocks )
	{
		if ( OverlapsBlock( Block, Position, HalfWidth, Height ) )
		{
			Position.x = Velocity.x > 0.0f ? Block.Min.x - HalfWidth : Block.Max.x + HalfWidth;
			Velocity.x = 0.0f;
			Result.bHitWall = true;
		}
	}
	// Z
	Position.z += Velocity.z * DeltaTime;
	for ( const FBlock3D& Block : Blocks )
	{
		if ( OverlapsBlock( Block, Position, HalfWidth, Height ) )
		{
			Position.z = Velocity.z > 0.0f ? Block.Min.z - HalfWidth : Block.Max.z + HalfWidth;
			Velocity.z = 0.0f;
			Result.bHitWall = true;
		}
	}
	// Y
	Position.y += Velocity.y * DeltaTime;
	for ( int32 Index = 0; Index < Blocks.Num(); ++Index )
	{
		const FBlock3D& Block = Blocks[Index];
		if ( !OverlapsBlock( Block, Position, HalfWidth, Height ) )
		{
			continue;
		}
		if ( Velocity.y <= 0.0f )
		{
			Position.y = Block.Max.y;
			Result.bOnGround = true;
		}
		else
		{
			Position.y = Block.Min.y - Height;
			Result.bHitCeiling = true;
			Result.CeilingBlock = Index;
		}
		Velocity.y = 0.0f;
	}
	return Result;
}

float32 FPlatformer3DLevel::GetGroundHeight( float32 X, float32 Z, float32 From ) const
{
	float32 Best = KillHeight - 100.0f;
	for ( const FBlock3D& Block : Blocks )
	{
		if ( Block.IsActive && X >= Block.Min.x && X <= Block.Max.x && Z >= Block.Min.z && Z <= Block.Max.z && Block.Max.y <= From + 0.3f && Block.Max.y > Best )
		{
			Best = Block.Max.y;
		}
	}
	return Best;
}

bool FPlatformer3DLevel::HasGroundBelow( float32 X, float32 Z, float32 From, float32 Depth ) const
{
	return GetGroundHeight( X, Z, From ) >= From - Depth;
}

EBlock3DHit FPlatformer3DLevel::Hit( int32 Index, bool bBig )
{
	if ( Index < 0 || Index >= Blocks.Num() )
	{
		return EBlock3DHit::None;
	}
	FBlock3D& Block = Blocks[Index];
	EBlock3DHit Result = EBlock3DHit::None;
	switch ( Block.Kind )
	{
	case EBlock3DKind::Question:
		Block.Kind = EBlock3DKind::Used;
		Result = EBlock3DHit::Coin;
		break;
	case EBlock3DKind::PowerUp:
		Block.Kind = EBlock3DKind::Used;
		Result = EBlock3DHit::PowerUp;
		break;
	case EBlock3DKind::StarBlock:
		Block.Kind = EBlock3DKind::Used;
		Result = EBlock3DHit::Star;
		break;
	case EBlock3DKind::Brick:
		if ( bBig )
		{
			Block.IsActive = false;
			return EBlock3DHit::Broke;
		}
		Result = EBlock3DHit::Bumped;
		break;
	default:
		return EBlock3DHit::None;
	}
	Block.BumpTime = 0.0001f;
	return Result;
}

void FPlatformer3DLevel::Tick( float32 DeltaTime )
{
	for ( FBlock3D& Block : Blocks )
	{
		if ( Block.BumpTime > 0.0f )
		{
			Block.BumpTime += DeltaTime;
			if ( Block.BumpTime >= 0.2f )
			{
				Block.BumpTime = 0.0f;
			}
		}
	}
}

FPlatformer3DLevel FPlatformer3DLevel::MakeDefault()
{
	FPlatformer3DLevel Level;
	const float32 Wide = 9.0f;

	// 地面（3 つ。間は穴）。上面が y = 0
	Level.Add( FVector3( -Wide, -4.0f, -9.0f ), FVector3( Wide, 0.0f, 34.0f ), EBlock3DKind::Ground );
	Level.Add( FVector3( -Wide, -4.0f, 38.5f ), FVector3( Wide, 0.0f, 80.0f ), EBlock3DKind::Ground );
	Level.Add( FVector3( -Wide, -4.0f, 84.5f ), FVector3( Wide, 0.0f, 128.0f ), EBlock3DKind::Ground );

	// 最初の広場: ブロックの列（コイン・レンガ・パワーアップ・レンガ・コイン）。下の面が y = 2.2
	const float32 BlockZ = 11.0f;
	const EBlock3DKind Row[5] = { EBlock3DKind::Question, EBlock3DKind::Brick, EBlock3DKind::PowerUp, EBlock3DKind::Brick, EBlock3DKind::Question };
	for ( int32 Index = 0; Index < 5; ++Index )
	{
		const float32 X = static_cast<float32>( Index - 2 ) * 1.0f;
		Level.Add( FVector3( X - 0.5f, 2.2f, BlockZ - 0.5f ), FVector3( X + 0.5f, 3.2f, BlockZ + 0.5f ), Row[Index] );
	}

	// 土管（高さ 1.4 と 2.0）
	Level.Add( FVector3( -5.0f, 0.0f, 20.0f ), FVector3( -3.0f, 1.4f, 22.0f ), EBlock3DKind::Pipe );
	Level.Add( FVector3( 3.0f, 0.0f, 27.0f ), FVector3( 5.0f, 2.0f, 29.0f ), EBlock3DKind::Pipe );

	// 2 つ目の地面の上: 階段（上って、下りる）
	for ( int32 Step = 0; Step < 4; ++Step )
	{
		const float32 Z = 48.0f + static_cast<float32>( Step ) * 2.0f;
		Level.Add( FVector3( -4.0f, 0.0f, Z ), FVector3( 4.0f, 0.5f * static_cast<float32>( Step + 1 ), Z + 2.0f ), EBlock3DKind::Wall );
	}
	for ( int32 Step = 0; Step < 4; ++Step )
	{
		const float32 Z = 56.0f + static_cast<float32>( Step ) * 2.0f;
		Level.Add( FVector3( -4.0f, 0.0f, Z ), FVector3( 4.0f, 2.0f - 0.5f * static_cast<float32>( Step ), Z + 2.0f ), EBlock3DKind::Wall );
	}
	// スターのブロックと、その横のコインのブロック
	Level.Add( FVector3( -0.5f, 2.2f, 69.5f ), FVector3( 0.5f, 3.2f, 70.5f ), EBlock3DKind::StarBlock );
	Level.Add( FVector3( -2.5f, 2.2f, 69.5f ), FVector3( -1.5f, 3.2f, 70.5f ), EBlock3DKind::Question );
	Level.Add( FVector3( 1.5f, 2.2f, 69.5f ), FVector3( 2.5f, 3.2f, 70.5f ), EBlock3DKind::Question );

	// 3 つ目の地面: 浮いた足場（上へ登る道）と、パワーアップのブロック
	Level.Add( FVector3( -1.5f, 0.0f, 92.0f ), FVector3( 1.5f, 1.0f, 95.0f ), EBlock3DKind::Wall );
	Level.Add( FVector3( -1.5f, 1.0f, 97.0f ), FVector3( 1.5f, 1.8f, 100.0f ), EBlock3DKind::Wall );
	Level.Add( FVector3( 3.5f, 2.2f, 104.0f ), FVector3( 4.5f, 3.2f, 105.0f ), EBlock3DKind::PowerUp );
	Level.Add( FVector3( -1.5f, 0.0f, 108.0f ), FVector3( 1.5f, 1.2f, 111.0f ), EBlock3DKind::Wall );

	// ゴールの土台
	Level.Add( FVector3( -1.5f, 0.0f, 120.0f ), FVector3( 1.5f, 0.6f, 123.0f ), EBlock3DKind::Wall );
	Level.GoalPosition = FVector3( 0.0f, 0.6f, 121.5f );

	// コイン: 道に沿って、並べる（弧と、列）
	for ( int32 Index = 0; Index < 6; ++Index )
	{
		Level.Coins.Add( FVector3( 0.0f, 0.8f + 0.6f * NMath::Sin( NMath::PI * static_cast<float32>( Index ) / 5.0f ), 4.0f + static_cast<float32>( Index ) * 1.1f ) );
	}
	for ( int32 Index = 0; Index < 5; ++Index )
	{
		Level.Coins.Add( FVector3( 2.0f - static_cast<float32>( Index ) * 1.0f, 4.0f, BlockZ ) );
	}
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Level.Coins.Add( FVector3( 0.0f, 1.6f + 0.5f * static_cast<float32>( Index % 2 ), 35.0f + static_cast<float32>( Index ) * 1.2f ) );
	}
	for ( int32 Index = 0; Index < 5; ++Index )
	{
		Level.Coins.Add( FVector3( 0.0f, 0.8f, 74.0f + static_cast<float32>( Index ) * 1.2f ) );
	}
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Level.Coins.Add( FVector3( 0.0f, 1.6f, 82.0f + static_cast<float32>( Index ) * 0.9f ) );
	}
	Level.Coins.Add( FVector3( 0.0f, 3.0f, 98.5f ) );
	Level.Coins.Add( FVector3( 0.0f, 2.4f, 109.5f ) );

	// 敵（足元の中心）。w = 追いかけてくる敵、s = 甲羅の敵
	const auto AddEnemy = [&Level]( char Kind, float32 X, float32 Z )
	{
		FBlock3DSpawn Spawn;
		Spawn.Kind = Kind;
		Spawn.Position = FVector3( X, 0.0f, Z );
		Level.Spawns.Add( Spawn );
	};
	AddEnemy( 'w', 2.0f, 24.0f );
	AddEnemy( 'w', -3.0f, 30.0f );
	AddEnemy( 's', 4.0f, 16.0f );
	AddEnemy( 'w', 2.5f, 43.0f );
	AddEnemy( 's', -3.0f, 66.0f );
	AddEnemy( 'w', 3.0f, 76.0f );
	AddEnemy( 'w', 0.0f, 89.0f );
	AddEnemy( 's', 4.0f, 114.0f );
	AddEnemy( 'w', -4.0f, 117.0f );

	Level.PlayerStart = FVector3( 0.0f, 0.0f, 0.0f );
	return Level;
}
