#include "InfoPanel.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 470;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 8;

	const char* ToText( FRenderStateDemoState::EGlassDepth Value )
	{
		return Value == FRenderStateDemoState::EGlassDepth::Normal ? "Normal (writes depth)" : "ReadOnly (tests only)";
	}

	const char* ToText( EDrawCulling Value )
	{
		switch ( Value )
		{
		case EDrawCulling::None:
			return "None (both sides)";
		case EDrawCulling::BackFaces:
			return "BackFaces (hide back)";
		default:
			return "FrontFaces (hide front)";
		}
	}

	const char* ToText( FRenderStateDemoState::EFogChoice Value )
	{
		switch ( Value )
		{
		case FRenderStateDemoState::EFogChoice::Linear:
			return "Linear 25..90";
		case FRenderStateDemoState::EFogChoice::Exponential:
			return "Exponential 0.03";
		case FRenderStateDemoState::EFogChoice::ExponentialSquared:
			return "ExponentialSquared 0.02";
		default:
			return "Off";
		}
	}

	const char* ToText( FRenderStateDemoState::EGroundFilter Value )
	{
		switch ( Value )
		{
		case FRenderStateDemoState::EGroundFilter::Nearest:
			return "Nearest";
		case FRenderStateDemoState::EGroundFilter::Bilinear:
			return "Bilinear";
		default:
			return "Anisotropic";
		}
	}

	const char* OnOff( bool bValue )
	{
		return bValue ? "On" : "Off";
	}
}

CInfoPanel::CInfoPanel( const TSharedPtr<FRenderStateDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
	, Labels( NStageScenery::MakeLabels() )
{
}

void CInfoPanel::Draw( FDrawContext& CommandList )
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

	// 描画状態の一覧の範囲（名札は、この上には出さない）
	const int32 Left = FRenderStateDemoState::ViewLeft + PanelMargin;
	const int32 Top = FRenderStateDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	// 見どころの名札
	for ( const NStageScenery::FStageLabel& Label : Labels )
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
		if ( Pixel.x < static_cast<float32>( FRenderStateDemoState::ViewLeft ) || Pixel.x > static_cast<float32>( FRenderStateDemoState::ViewLeft + FRenderStateDemoState::ViewWidth ) || Pixel.y < static_cast<float32>( FRenderStateDemoState::ViewTop ) || Pixel.y > static_cast<float32>( FRenderStateDemoState::ViewTop + FRenderStateDemoState::ViewHeight ) )
		{
			continue;
		}
		Font->RecordStyled( CommandList, Pixel.x, Pixel.y, *Label.Text, FColor32( 235, 238, 250 ), Centered );
	}

	// 描画状態の一覧
	CommandList.Blend( EDrawBlend::Alpha, 160 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const FString Lines[NumLines] = {
		FString::Printf( "1  Glass depth      %s", ToText( State->GlassDepth ) ),
		FString::Printf( "2  Room culling     %s", ToText( State->CardCulling ) ),
		FString::Printf( "3  Distance fog     %s", ToText( State->Fog ) ),
		FString::Printf( "4  Ground mist      %s  (height fog)", OnOff( State->bHeightFog ) ),
		FString::Printf( "5  Ground filter    %s", ToText( State->GroundFilter ) ),
		FString::Printf( "6  Anisotropy       x%d", State->GroundAnisotropy ),
		FString::Printf( "7  Water scroll     %s", OnOff( State->bScrollWater ) ),
		FString::Printf( "8  Beam clears depth  %s", OnOff( State->bBeamClearsDepth ) ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], FColor32( 220, 226, 242 ), Shadow );
	}
}
