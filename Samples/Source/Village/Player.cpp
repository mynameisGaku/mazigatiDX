#include "Player.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 歩く速さと、走る速さ（ピクセル / 秒）
	constexpr float32 WalkSpeed = 110.0f;
	constexpr float32 RunSpeed = 190.0f;

	// 走ると、体力が減る速さと、走らないとき戻る速さ（1 秒あたり。体力は 0〜1）
	constexpr float32 StaminaDrain = 0.4f;
	constexpr float32 StaminaRecovery = 0.3f;

	// 足元の矩形の半分の大きさ（ピクセル。足元の位置から、上へ向かって）
	constexpr float32 FootHalfWidth = 8.0f;
	constexpr float32 FootHalfHeight = 5.0f;

	// 足踏みの 1 コマの長さ（秒）
	constexpr float32 FrameDuration = 0.18f;

	// 道すじの点に着いたとみなす距離（ピクセル）
	constexpr float32 ArriveDistance = 4.0f;

	// カメラが主人公に追いつく速さ（大きいほど速い）
	constexpr float32 CameraFollowSpeed = 6.0f;

	// 画像の 1 コマの左上の x（キャラクターの画像は、下向き 2・上向き 2・横向き 2 のコマを横に並べる）
	constexpr int32 DownCell = 0;
	constexpr int32 UpCell = 2;
	constexpr int32 SideCell = 4;
}

CPlayer::CPlayer( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CTileMap>& InMap, const TSharedPtr<CTexture>& InSheet )
	: State( InState )
	, Map( InMap )
	, Sheet( InSheet )
{
	if ( Map.IsValid() )
	{
		Position = Map->GetStartPosition();
	}
	UpdateCamera( 0.0f, true );
}

bool CPlayer::IsBlockedAt( const FVector2& FootPosition ) const
{
	return Map.IsValid() && Map->IsAreaBlocked( FVector2( FootPosition.x, FootPosition.y - FootHalfHeight ), FootHalfWidth, FootHalfHeight );
}

FVector2 CPlayer::GetRouteDirection()
{
	if ( !Map.IsValid() || Map->GetRoute().Num() < 2 )
	{
		return FVector2();
	}
	const TArray<FVector2>& Route = Map->GetRoute();
	FVector2 Delta = Route[NextRouteIndex] - Position;
	if ( NMath::Abs( Delta.x ) + NMath::Abs( Delta.y ) < ArriveDistance )
	{
		// 着いたら、点の位置にそろえて、次の点へ向かう
		Position = Route[NextRouteIndex];
		NextRouteIndex = ( NextRouteIndex + 1 ) % Route.Num();
		Delta = Route[NextRouteIndex] - Position;
	}
	if ( NMath::Abs( Delta.x ) > NMath::Abs( Delta.y ) )
	{
		return FVector2( Delta.x > 0.0f ? 1.0f : -1.0f, 0.0f );
	}
	return FVector2( 0.0f, Delta.y > 0.0f ? 1.0f : -1.0f );
}

void CPlayer::UpdateCamera( float32 DeltaTime, bool bSnap )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const float32 MaxX = static_cast<float32>( CTileMap::Columns * CTileMap::TileSize - FVillageState::ViewWidth );
	const float32 MaxY = static_cast<float32>( CTileMap::Rows * CTileMap::TileSize - FVillageState::ViewHeight );
	const FVector2 Target( NMath::Clamp( Position.x - static_cast<float32>( FVillageState::ViewWidth ) * 0.5f, 0.0f, MaxX ), NMath::Clamp( Position.y - static_cast<float32>( FVillageState::ViewHeight ) * 0.5f, 0.0f, MaxY ) );
	if ( bSnap )
	{
		SmoothCamera = Target;
	}
	else
	{
		SmoothCamera += ( Target - SmoothCamera ) * NMath::Min( 1.0f, DeltaTime * CameraFollowSpeed );
	}
	// 整数にそろえる（タイルの継ぎ目がずれないようにする）
	State->CameraPosition = FVector2( NMath::Floor( SmoothCamera.x + 0.5f ), NMath::Floor( SmoothCamera.y + 0.5f ) );
	State->PlayerPosition = Position;
}

void CPlayer::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() || !Map.IsValid() )
	{
		return;
	}

	// キーの入力
	const IInput* Input = GetAppSubsystem<IInput>();
	FVector2 Direction;
	bool bWantsRun = false;
	if ( Input != nullptr )
	{
		Direction.x += ( Input->IsKeyDown( EKey::D ) || Input->IsKeyDown( EKey::Right ) ) ? 1.0f : 0.0f;
		Direction.x -= ( Input->IsKeyDown( EKey::A ) || Input->IsKeyDown( EKey::Left ) ) ? 1.0f : 0.0f;
		Direction.y += ( Input->IsKeyDown( EKey::S ) || Input->IsKeyDown( EKey::Down ) ) ? 1.0f : 0.0f;
		Direction.y -= ( Input->IsKeyDown( EKey::W ) || Input->IsKeyDown( EKey::Up ) ) ? 1.0f : 0.0f;
		bWantsRun = Input->IsKeyDown( EKey::LeftShift );
	}
	const bool bHasInput = Direction.x != 0.0f || Direction.y != 0.0f;
	if ( bHasInput )
	{
		bAutoWalk = false;
	}

	// 会話の窓が開いているあいだは、自分で歩いているときは、立ち止まる（ひとりで歩くときは、止まらない）
	if ( bAutoWalk )
	{
		Direction = GetRouteDirection();
		bWantsRun = false;
	}
	else if ( State->bDialogueOpen )
	{
		Direction = FVector2();
		bWantsRun = false;
	}
	const float32 Length = NMath::Sqrt( Direction.x * Direction.x + Direction.y * Direction.y );
	if ( Length > 1.0f )
	{
		Direction = Direction / Length;
	}
	bMoving = Length > 0.0f;

	// 走る（体力があるあいだ）
	bRunning = bMoving && bWantsRun && State->Stamina > 0.05f;
	State->Stamina = bRunning ? NMath::Max( 0.0f, State->Stamina - StaminaDrain * DeltaTime ) : NMath::Min( 1.0f, State->Stamina + StaminaRecovery * DeltaTime );

	// 動く（横と縦を別々に調べる。壁に沿って滑れるように）
	if ( bMoving )
	{
		const FVector2 Step = Direction * ( ( bRunning ? RunSpeed : WalkSpeed ) * DeltaTime );
		FVector2 Next = FVector2( Position.x + Step.x, Position.y );
		if ( !IsBlockedAt( Next ) )
		{
			Position = Next;
		}
		Next = FVector2( Position.x, Position.y + Step.y );
		if ( !IsBlockedAt( Next ) )
		{
			Position = Next;
		}
		if ( NMath::Abs( Direction.x ) > NMath::Abs( Direction.y ) )
		{
			Facing = Direction.x > 0.0f ? EFacing::Right : EFacing::Left;
		}
		else
		{
			Facing = Direction.y > 0.0f ? EFacing::Down : EFacing::Up;
		}
		AnimationTime += DeltaTime;
	}
	else
	{
		AnimationTime = 0.0f;
	}

	UpdateCamera( DeltaTime, false );
}

void CPlayer::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Sheet.IsValid() )
	{
		return;
	}
	FWorldScope World( CommandList, *State );

	// 足元の影
	CommandList.Blend( EDrawBlend::Alpha, 90 );
	CommandList.Oval( NMath::RoundToInt( Position.x ), NMath::RoundToInt( Position.y ) - 1, 10, 4, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	// 向きと足踏みのコマ
	const int32 Frame = bMoving ? ( static_cast<int32>( AnimationTime / FrameDuration ) % 2 ) : 0;
	int32 Cell = DownCell;
	if ( Facing == EFacing::Up )
	{
		Cell = UpCell;
	}
	else if ( Facing == EFacing::Right || Facing == EFacing::Left )
	{
		Cell = SideCell;
	}
	const float32 Left = NMath::Floor( Position.x + 0.5f ) - static_cast<float32>( NVillageArt::CharacterWidth / 2 );
	const float32 Top = NMath::Floor( Position.y + 0.5f ) - static_cast<float32>( NVillageArt::CharacterHeight - 1 );
	CommandList.ImagePart( Left, Top, ( Cell + Frame ) * NVillageArt::CharacterWidth, 0, NVillageArt::CharacterWidth, NVillageArt::CharacterHeight, Sheet, Facing == EFacing::Left );
}
