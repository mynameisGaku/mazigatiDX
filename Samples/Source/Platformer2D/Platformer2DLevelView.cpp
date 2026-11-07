#include "Platformer2DLevelView.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	const FColor32 SkyTop( 92, 148, 252 );
	const FColor32 SkyBottom( 168, 212, 255 );
	const FColor32 GrassColor( 88, 190, 70 );
	const FColor32 DirtColor( 168, 104, 52 );
	const FColor32 DirtLine( 120, 70, 30 );
	const FColor32 BrickColor( 200, 96, 48 );
	const FColor32 BrickLine( 110, 48, 24 );
	const FColor32 QuestionColor( 244, 190, 40 );
	const FColor32 UsedColor( 132, 108, 84 );
	const FColor32 PipeColor( 40, 168, 72 );
	const FColor32 PipeDark( 20, 110, 48 );
}

CPlatformer2DLevelView::CPlatformer2DLevelView( const TSharedPtr<FPlatformer2DState>& InState )
	: State( InState )
{}

void CPlatformer2DLevelView::Tick( float32 DeltaTime )
{
	// 面の時間と、叩かれたブロックの持ち上がりを進める
	State->Time += DeltaTime;
	State->Level.Tick( DeltaTime );
}

void CPlatformer2DLevelView::DrawBackground( FDrawContext& CommandList ) const
{
	// 空（上が濃く、下が淡い。帯を重ねる）
	const int32 Bands = 8;
	for ( int32 Band = 0; Band < Bands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( Bands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.r ), static_cast<float32>( SkyBottom.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.g ), static_cast<float32>( SkyBottom.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( SkyTop.b ), static_cast<float32>( SkyBottom.b ), T ) ) );
		CommandList.Box( 0, FPlatformer2DState::ScreenHeight * Band / Bands, FPlatformer2DState::ScreenWidth, FPlatformer2DState::ScreenHeight * ( Band + 1 ) / Bands, Color );
	}
	// 遠くの丘と雲（カメラの半分の速さで動く）
	const float32 Parallax = State->CameraX * 0.5f;
	for ( int32 Index = 0; Index < 14; ++Index )
	{
		const float32 WorldX = static_cast<float32>( Index ) * 420.0f + 120.0f;
		const int32 X = static_cast<int32>( WorldX - Parallax );
		if ( X < -300 || X > FPlatformer2DState::ScreenWidth + 300 )
		{
			continue;
		}
		const int32 Ground = 19 * FPlatformer2DLevel::TileSize;
		CommandList.Oval( X, Ground, 150 + ( Index % 3 ) * 30, 90 + ( Index % 2 ) * 30, FColor32( 108, 196, 96 ), true );
		CommandList.Oval( X + 260, 140 + ( Index % 4 ) * 24, 60, 22, FColor32( 250, 252, 255 ), true );
		CommandList.Oval( X + 300, 128 + ( Index % 4 ) * 24, 40, 20, FColor32( 250, 252, 255 ), true );
	}
}

void CPlatformer2DLevelView::DrawTile( FDrawContext& CommandList, int32 TileX, int32 TileY, char Tile ) const
{
	const int32 Size = FPlatformer2DLevel::TileSize;
	const int32 Left = State->ToScreenX( static_cast<float32>( TileX * Size ) );
	int32 Top = TileY * Size;
	// 叩かれて、持ち上がっているブロック
	for ( const FTile2DBump& Bump : State->Level.Bumps )
	{
		if ( Bump.X == TileX && Bump.Y == TileY )
		{
			Top -= static_cast<int32>( 10.0f * NMath::Sin( NMath::PI * Bump.Time / 0.2f ) );
		}
	}
	const int32 Right = Left + Size;
	const int32 Bottom = Top + Size;
	switch ( Tile )
	{
	case '#':
		CommandList.Box( Left, Top, Right, Bottom, DirtColor, true );
		CommandList.Box( Left, Top, Right, Bottom, DirtLine, false );
		if ( !State->Level.IsSolid( TileX, TileY - 1 ) )
		{
			CommandList.Box( Left, Top, Right, Top + 8, GrassColor, true );
		}
		break;
	case 'B':
		CommandList.Box( Left, Top, Right, Bottom, BrickColor, true );
		CommandList.Line( Left, Top + Size / 2, Right, Top + Size / 2, BrickLine );
		CommandList.Line( Left + Size / 2, Top, Left + Size / 2, Top + Size / 2, BrickLine );
		CommandList.Line( Left + Size / 4, Top + Size / 2, Left + Size / 4, Bottom, BrickLine );
		CommandList.Line( Left + Size * 3 / 4, Top + Size / 2, Left + Size * 3 / 4, Bottom, BrickLine );
		CommandList.Box( Left, Top, Right, Bottom, BrickLine, false );
		break;
	case '?':
	case 'M':
	case '*':
		{
			// 点滅（ゆっくり明るさが変わる）
			const float32 Blink = 0.5f + 0.5f * NMath::Sin( State->Time * 6.0f );
			const FColor32 Color( QuestionColor.r, static_cast<uint8>( 150.0f + 40.0f * Blink ), QuestionColor.b );
			CommandList.Box( Left, Top, Right, Bottom, Color, true );
			CommandList.Box( Left, Top, Right, Bottom, FColor32( 120, 70, 10 ), false, 2 );
			CommandList.Text( Left + 11, Top + 7, "?", FColor32( 120, 60, 0 ) );
		}
		break;
	case 'U':
		CommandList.Box( Left, Top, Right, Bottom, UsedColor, true );
		CommandList.Box( Left, Top, Right, Bottom, FColor32( 80, 62, 44 ), false, 2 );
		break;
	case 'p':
		{
			const bool bTop = State->Level.Get( TileX, TileY - 1 ) != 'p';
			CommandList.Box( Left, Top, Right, Bottom, PipeColor, true );
			CommandList.Box( Left, Top, Right, Bottom, PipeDark, false, 2 );
			if ( bTop )
			{
				CommandList.Box( Left - 2, Top, Right + 2, Top + 14, PipeColor, true );
				CommandList.Box( Left - 2, Top, Right + 2, Top + 14, PipeDark, false, 2 );
			}
		}
		break;
	case 'o':
		{
			const float32 Spin = NMath::Abs( NMath::Cos( State->Time * 5.0f + static_cast<float32>( TileX ) * 0.5f ) );
			const int32 Rx = 3 + static_cast<int32>( 7.0f * Spin );
			CommandList.Oval( Left + Size / 2, Top + Size / 2, Rx, 11, FColor32( 250, 206, 40 ), true );
			CommandList.Oval( Left + Size / 2, Top + Size / 2, Rx, 11, FColor32( 170, 120, 0 ), false );
		}
		break;
	case 'G':
		{
			const int32 PoleX = Left + Size / 2;
			CommandList.Box( PoleX - 3, Top, PoleX + 3, Bottom, FColor32( 236, 240, 236 ), true );
			if ( State->Level.Get( TileX, TileY - 1 ) != 'G' )
			{
				CommandList.Circle( PoleX, Top - 4, 7, FColor32( 90, 200, 120 ), true );
				// 旗
				CommandList.Triangle( PoleX + 3, Top + 4, PoleX + 3, Top + 40, PoleX + 56, Top + 22, FColor32( 230, 70, 70 ), true );
			}
		}
		break;
	default:
		break;
	}
}

void CPlatformer2DLevelView::Draw( FDrawContext& CommandList )
{
	DrawBackground( CommandList );
	const int32 Size = FPlatformer2DLevel::TileSize;
	const int32 First = FPlatformer2DLevel::ToTile( State->CameraX );
	const int32 Last = FPlatformer2DLevel::ToTile( State->CameraX + static_cast<float32>( FPlatformer2DState::ScreenWidth ) );
	for ( int32 TileY = 0; TileY < State->Level.Height; ++TileY )
	{
		for ( int32 TileX = First; TileX <= Last; ++TileX )
		{
			const char Tile = State->Level.Get( TileX, TileY );
			if ( Tile != ' ' && TileX >= 0 && TileX < State->Level.Width )
			{
				DrawTile( CommandList, TileX, TileY, Tile );
			}
		}
	}
	// 面の下の端（穴の下）は、暗くしておく
	CommandList.Box( 0, State->Level.Height * Size, FPlatformer2DState::ScreenWidth, FPlatformer2DState::ScreenHeight, FColor32( 20, 24, 36 ), true );
}
