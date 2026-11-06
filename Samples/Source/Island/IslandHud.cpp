#include "IslandHud.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Island/IslandScenery.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 470;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 6;

	// 地図の大きさと、右下からの余白
	constexpr int32 MapSize = 200;
	constexpr int32 MapMargin = 16;

	// 名札を出す距離（これより遠い結晶には出さない）
	constexpr float32 LabelDistance = 36.0f;

	// 色
	const FColor32 TextColor( 230, 236, 248 );
	const FColor32 NoteColor( 172, 180, 196 );
	const FColor32 CrystalTextColor( 150, 236, 255 );
	const FColor32 ExplorerDotColor( 255, 136, 60 );
	const FColor32 CrystalDotColor( 120, 230, 255 );
	const FColor32 LighthouseDotColor( 255, 236, 140 );

	// 一日の時刻（0〜1）を、時計の時刻（6 時〜18 時）の文字にする
	FString ToClockText( float32 DayTime )
	{
		const float32 Hours = 6.0f + DayTime * 12.0f;
		const int32 Hour = static_cast<int32>( Hours );
		const int32 Minute = static_cast<int32>( ( Hours - static_cast<float32>( Hour ) ) * 60.0f );
		return FString::Printf( "%02d:%02d", Hour, Minute );
	}

	const char* ToOnOffText( bool bOn )
	{
		return bOn ? "on" : "off";
	}

	FTextStyle MakeShadowStyle()
	{
		FTextStyle Style;
		Style.bShadow = true;
		Style.ShadowColor = FColor32( 0, 0, 0 );
		return Style;
	}
}

CIslandHud::CIslandHud( const TSharedPtr<FIslandState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CIslandHud::DrawPanel( FDrawContext& CommandList ) const
{
	const int32 Left = FIslandState::ViewLeft + PanelMargin;
	const int32 Top = FIslandState::ViewTop + PanelMargin;
	CommandList.Blend( EDrawBlend::Alpha, 160 );
	CommandList.RoundBox( Left - 8, Top - 8, Left + PanelWidth, Top + NumLines * LineHeight + 8, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const FString Lines[NumLines] = {
		FString::Printf( "Crystals     %d / %d", State->NumTaken, State->Crystals.Num() ),
		FString::Printf( "Time         %s%s", *ToClockText( State->DayTime ), State->bTimeRunning ? "" : " (stopped)" ),
		FString::Printf( "1 Shadows %s   2 Fog %s   3 Time %s", ToOnOffText( State->bShadows ), ToOnOffText( State->bFog ), State->bTimeRunning ? "running" : "stopped" ),
		FString::Printf( "WASD walk (Shift run), right click: walk there" ),
		FString::Printf( "Drag: turn the camera, wheel: zoom, R: auto walk" ),
		FString::Printf( "%s", State->bAutoWalk ? "Walking to the nearest crystal by itself..." : ( State->bHasTarget ? "Walking to the marked spot" : "" ) ),
	};
	const FTextStyle Style = MakeShadowStyle();
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		const FColor32 Color = Index == 0 ? CrystalTextColor : ( Index >= 3 ? NoteColor : TextColor );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], Color, Style );
	}
}

void CIslandHud::DrawMap( FDrawContext& CommandList ) const
{
	if ( !State->MapTexture.IsValid() )
	{
		return;
	}
	const int32 Right = FIslandState::ViewLeft + FIslandState::ViewWidth - MapMargin;
	const int32 Bottom = FIslandState::ViewTop + FIslandState::ViewHeight - MapMargin;
	const int32 Left = Right - MapSize;
	const int32 Top = Bottom - MapSize;
	CommandList.Box( Left - 3, Top - 3, Right + 3, Bottom + 3, FColor32( 20, 24, 32 ) );
	CommandList.Filter( EDrawFilter::Nearest );
	CommandList.ImageStretch( static_cast<float32>( Left ), static_cast<float32>( Top ), static_cast<float32>( Right ), static_cast<float32>( Bottom ), State->MapTexture );
	CommandList.Filter( EDrawFilter::Bilinear );

	// 世界の (x, z) → 地図の画素（上が北 = +z）
	const auto ToMap = [&]( const FVector3& World )
	{
		const float32 U = ( World.x + FIslandState::TerrainHalfSize ) / ( FIslandState::TerrainHalfSize * 2.0f );
		const float32 V = ( FIslandState::TerrainHalfSize - World.z ) / ( FIslandState::TerrainHalfSize * 2.0f );
		return FVector2( static_cast<float32>( Left ) + U * MapSize, static_cast<float32>( Top ) + V * MapSize );
	};
	for ( const FIslandState::FCrystal& Crystal : State->Crystals )
	{
		if ( !Crystal.bTaken )
		{
			const FVector2 Dot = ToMap( Crystal.Position );
			CommandList.Circle( static_cast<int32>( Dot.x ), static_cast<int32>( Dot.y ), 3, CrystalDotColor );
		}
	}
	const FVector2 Lighthouse = ToMap( State->LighthousePosition );
	CommandList.Box( static_cast<int32>( Lighthouse.x ) - 3, static_cast<int32>( Lighthouse.y ) - 3, static_cast<int32>( Lighthouse.x ) + 3, static_cast<int32>( Lighthouse.y ) + 3, LighthouseDotColor );

	// 探検家（点と、向きの線）と、カメラが見ている向き（薄い線）
	const FVector2 Explorer = ToMap( State->ExplorerPosition );
	const FVector2 Facing( NMath::Sin( State->ExplorerYaw ), -NMath::Cos( State->ExplorerYaw ) );
	const FVector2 View( -NMath::Sin( State->CameraYaw ), -NMath::Cos( State->CameraYaw ) );
	CommandList.Line( static_cast<int32>( Explorer.x ), static_cast<int32>( Explorer.y ), static_cast<int32>( Explorer.x + View.x * 22.0f ), static_cast<int32>( Explorer.y + View.y * 22.0f ), FColor32( 255, 255, 255, 255 ) );
	CommandList.Circle( static_cast<int32>( Explorer.x ), static_cast<int32>( Explorer.y ), 4, ExplorerDotColor );
	CommandList.Line( static_cast<int32>( Explorer.x ), static_cast<int32>( Explorer.y ), static_cast<int32>( Explorer.x + Facing.x * 9.0f ), static_cast<int32>( Explorer.y + Facing.y * 9.0f ), ExplorerDotColor, 2 );
	Font->RecordStyled( CommandList, static_cast<float32>( Left + 4 ), static_cast<float32>( Top + 2 ), "N", TextColor, MakeShadowStyle() );
}

void CIslandHud::DrawLabels( FDrawContext& CommandList ) const
{
	const FTextStyle Style = MakeShadowStyle();
	const FCameraViewport Viewport = State->GetScreenViewport();
	for ( int32 Index = 0; Index < State->Crystals.Num(); ++Index )
	{
		const FIslandState::FCrystal& Crystal = State->Crystals[Index];
		if ( Crystal.bTaken )
		{
			continue;
		}
		const FVector3 Delta = Crystal.Position - State->ExplorerPosition;
		const float32 Distance = Delta.Length();
		FVector2 Pixel;
		if ( Distance > LabelDistance || !State->Camera.WorldToScreen( Viewport, NIslandScenery::GetCrystalPosition( *State, Index ) + FVector3( 0.0f, 1.8f, 0.0f ), Pixel ) )
		{
			continue;
		}
		if ( Pixel.y < static_cast<float32>( FIslandState::ViewTop ) )
		{
			continue;
		}
		const FString Text = FString::Printf( "Crystal %d m", static_cast<int32>( Distance ) );
		Font->RecordStyled( CommandList, Pixel.x - 40.0f, Pixel.y - 10.0f, *Text, CrystalTextColor, Style );
	}
	if ( State->AllTakenTime >= 0.0f )
	{
		const char* Message = "All crystals found! They will come back soon.";
		const float32 X = static_cast<float32>( FIslandState::ViewLeft + FIslandState::ViewWidth / 2 - 210 );
		const float32 Y = static_cast<float32>( FIslandState::ViewTop + 90 );
		Font->RecordStyled( CommandList, X, Y, Message, FColor32( 255, 236, 140 ), Style );
	}
}

void CIslandHud::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Font.IsValid() )
	{
		return;
	}
	DrawLabels( CommandList );
	DrawPanel( CommandList );
	DrawMap( CommandList );
}
