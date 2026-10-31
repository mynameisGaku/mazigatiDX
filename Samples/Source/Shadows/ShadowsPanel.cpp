#include "ShadowsPanel.h"

#include "Source/Shadows/ShadowsScenery.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 560;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 8;

	const char* OnOff( bool bValue )
	{
		return bValue ? "On" : "Off";
	}

	const char* ToAreaText( int32 AreaMode )
	{
		switch ( AreaMode )
		{
		case 1:
			return "Wide (90 x 90, coarse)";
		case 2:
			return "Camera's view (coarse)";
		default:
			return "Courtyard (44 x 44, fine)";
		}
	}

	const char* ToDepthText( int32 DepthMode )
	{
		switch ( DepthMode )
		{
		case 1:
			return "0 (stripes on lit surfaces)";
		case 2:
			return "0.05 (shadows float off)";
		default:
			return "0.005 (default)";
		}
	}
}

CShadowsPanel::CShadowsPanel( const TSharedPtr<FShadowsDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CShadowsPanel::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Font.IsValid() )
	{
		return;
	}
	const FCameraViewport Viewport = State->GetScreenViewport();
	const FCamera& Camera = State->Camera;

	FTextStyle Shadow;
	Shadow.bShadow = true;
	Shadow.ShadowColor = FColor32( 0, 0, 0 );
	FTextStyle Centered = Shadow;
	Centered.AnchorX = 0.5f;
	Centered.AnchorY = 1.0f;

	// 一覧の範囲（名札は、この上には出さない）
	const int32 Left = FShadowsDemoState::ViewLeft + PanelMargin;
	const int32 Top = FShadowsDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	// 見どころの名札
	const TArray<NShadowsScenery::FStageLabel> Labels = NShadowsScenery::MakeLabels( *State );
	for ( const NShadowsScenery::FStageLabel& Label : Labels )
	{
		FVector2 Pixel;
		if ( !Camera.WorldToScreen( Viewport, Label.Position, Pixel ) )
		{
			continue;
		}
		if ( Pixel.x < static_cast<float32>( PanelRight ) && Pixel.y < static_cast<float32>( PanelBottom ) )
		{
			continue;
		}
		if ( Pixel.x < static_cast<float32>( FShadowsDemoState::ViewLeft ) || Pixel.x > static_cast<float32>( FShadowsDemoState::ViewLeft + FShadowsDemoState::ViewWidth ) || Pixel.y < static_cast<float32>( FShadowsDemoState::ViewTop ) || Pixel.y > static_cast<float32>( FShadowsDemoState::ViewTop + FShadowsDemoState::ViewHeight ) )
		{
			continue;
		}
		Font->RecordStyled( CommandList, Pixel.x, Pixel.y, *Label.Text, FColor32( 20, 24, 36 ), Centered );
	}

	// 設定の一覧
	CommandList.Blend( EDrawBlend::Alpha, 170 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const int32 MapSize = FShadowsDemoState::MapSizes[NMath::Clamp( State->SizeLevel, 0, FShadowsDemoState::NumSizeLevels - 1 )];
	const int32 SunDegrees = static_cast<int32>( NMath::Fmod( State->SunAzimuth * 57.29578f, 360.0f ) );
	const FString Lines[NumLines] = {
		FString::Printf( "1  Shadows                %s", OnOff( State->bShadows ) ),
		FString::Printf( "2  Shadow map size        %d x %d", MapSize, MapSize ),
		FString::Printf( "3  Area of the shadows    %s", ToAreaText( State->AreaMode ) ),
		FString::Printf( "4  Sun                    %s (%d deg)", State->bSunMoving ? "moving" : "stopped", SunDegrees ),
		FString::Printf( "5  Depth adjust           %s", ToDepthText( State->DepthMode ) ),
		FString::Printf( "6  Shadow map preview     %s", OnOff( State->bPreview ) ),
		FString::Printf( "7  Lantern (unshadowed)   %s", OnOff( State->bLantern ) ),
		FString::Printf( "Only the sun is shadowed; sky, fill and lantern lights are not" ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		const FColor32 Color = Index == NumLines - 1 ? FColor32( 170, 176, 192 ) : FColor32( 226, 232, 246 );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], Color, Shadow );
	}

	// 影の絵の中身を、右下に出す
	if ( State->bPreview && State->bShadows && State->ShadowMap.IsValid() )
	{
		const int32 Right = FShadowsDemoState::ViewLeft + FShadowsDemoState::ViewWidth - PanelMargin;
		const int32 Bottom = FShadowsDemoState::ViewTop + FShadowsDemoState::ViewHeight - PanelMargin;
		const int32 PreviewLeft = Right - FShadowsDemoState::PreviewSize;
		const int32 PreviewTop = Bottom - FShadowsDemoState::PreviewSize;
		CommandList.Box( PreviewLeft - 2, PreviewTop - 2, Right + 2, Bottom + 2, FColor32::White, false, 2 );
		CommandList.PreviewShadowMap( State->ShadowMap, PreviewLeft, PreviewTop, Right, Bottom );
		Font->RecordStyled( CommandList, static_cast<float32>( PreviewLeft ), static_cast<float32>( PreviewTop - 22 ), "Shadow map (depth from the sun; darker red = nearer)", FColor32( 226, 232, 246 ), Shadow );
	}
}
