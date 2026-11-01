#include "PostEffectsPanel.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 600;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 8;

	const char* ToViewText( int32 Mode )
	{
		switch ( Mode )
		{
		case 1:
			return "8-bit look (clipped at 1)";
		case 2:
			return "Bloom only";
		case 3:
			return "Depth (second output, SV_Target1)";
		default:
			return "Final";
		}
	}

	const char* ToToneMapText( int32 Mode )
	{
		switch ( Mode )
		{
		case 1:
			return "Reinhard";
		case 2:
			return "None (clipped at 1)";
		default:
			return "ACES";
		}
	}

	const char* ToOnOffText( bool bOn )
	{
		return bOn ? "on" : "off";
	}
}

CPostEffectsPanel::CPostEffectsPanel( const TSharedPtr<FPostEffectsDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CPostEffectsPanel::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Font.IsValid() )
	{
		return;
	}
	FTextStyle Shadow;
	Shadow.bShadow = true;
	Shadow.ShadowColor = FColor32( 0, 0, 0 );

	const int32 Left = FPostEffectsDemoState::ViewLeft + PanelMargin;
	const int32 Top = FPostEffectsDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	CommandList.Blend( EDrawBlend::Alpha, 170 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const FString Lines[NumLines] = {
		FString::Printf( "1    Show          %s", ToViewText( State->ViewMode ) ),
		FString::Printf( "2    Tone map      %s", ToToneMapText( State->ToneMap ) ),
		FString::Printf( "3    Bloom         %s", ToOnOffText( State->bBloom ) ),
		FString::Printf( "4    Depth fog     %s", ToOnOffText( State->bFog ) ),
		FString::Printf( "Q/E  Exposure      x%.2f", State->Exposure ),
		FString::Printf( "5    Time          %s (%.1f s)", State->bTimeRunning ? "running" : "stopped", State->Time ),
		FString::Printf( "The scene goes into a float16 target (values above 1 survive) and" ),
		FString::Printf( "its depth into a float32 target at the same time (two outputs)" ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		const FColor32 Color = Index >= 6 ? FColor32( 170, 176, 192 ) : FColor32( 226, 232, 246 );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], Color, Shadow );
	}
}
