#include "LightingPanel.h"

#include "Source/Lighting/LightingScenery.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 500;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 10;

	const char* OnOff( bool bValue )
	{
		return bValue ? "On" : "Off";
	}

	const char* ToText( int32 AmbientLevel )
	{
		switch ( AmbientLevel )
		{
		case 1:
			return "0.12 (dim)";
		case 2:
			return "0.30";
		case 3:
			return "0.55 (bright)";
		default:
			return "0 (none)";
		}
	}
}

CLightingPanel::CLightingPanel( const TSharedPtr<FLightingDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CLightingPanel::Draw( FDrawContext& CommandList )
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
	const int32 Left = FLightingDemoState::ViewLeft + PanelMargin;
	const int32 Top = FLightingDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	// 見どころの名札
	const TArray<NLightingScenery::FStageLabel> Labels = NLightingScenery::MakeLabels( *State );
	for ( const NLightingScenery::FStageLabel& Label : Labels )
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
		if ( Pixel.x < static_cast<float32>( FLightingDemoState::ViewLeft ) || Pixel.x > static_cast<float32>( FLightingDemoState::ViewLeft + FLightingDemoState::ViewWidth ) || Pixel.y < static_cast<float32>( FLightingDemoState::ViewTop ) || Pixel.y > static_cast<float32>( FLightingDemoState::ViewTop + FLightingDemoState::ViewHeight ) )
		{
			continue;
		}
		Font->RecordStyled( CommandList, Pixel.x, Pixel.y, *Label.Text, FColor32( 235, 238, 250 ), Centered );
	}

	// ライティングの一覧
	CommandList.Blend( EDrawBlend::Alpha, 160 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const int32 Requested = NLightingScenery::CountRequestedLights( *State );
	const int32 Active = NLightingScenery::CountActiveLights( *State );
	const FString Lines[NumLines] = {
		FString::Printf( "1  Moon (directional)    %s", OnOff( State->bMoon ) ),
		FString::Printf( "2  Lamp (point, orbits)  %s", OnOff( State->bLamp ) ),
		FString::Printf( "3  Spotlight (sweeps)    %s", OnOff( State->bSpotlight ) ),
		FString::Printf( "4  Ambient light         %s", ToText( State->AmbientLevel ) ),
		FString::Printf( "5  Pixel lighting        %s", OnOff( State->bPixelLighting ) ),
		FString::Printf( "6  Specular highlights   %s", OnOff( State->bSpecular ) ),
		FString::Printf( "7  Wrap lighting         %s", OnOff( State->bWrapLighting ) ),
		FString::Printf( "8  Street lamps (point)  %d", State->ExtraLamps ),
		FString::Printf( "9  Lighting              %s", OnOff( State->bLighting ) ),
		FString::Printf( "Lights: %d requested, %d used (max %d)", Requested, Active, FLight::MaxActiveLights ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		const bool bOverLimit = Index == NumLines - 1 && Requested > Active;
		const FColor32 Color = bOverLimit ? FColor32( 255, 170, 110 ) : FColor32( 220, 226, 242 );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], Color, Shadow );
	}
}
