#include "ShadersPanel.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 560;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 6;

	const char* ToObjectText( int32 Mode )
	{
		switch ( Mode )
		{
		case 1:
			return "Normal colors";
		case 2:
			return "Depth";
		case 3:
			return "Wobble (vertex shader)";
		default:
			return "Toon";
		}
	}

	const char* ToGroundText( int32 Mode )
	{
		return Mode == 1 ? "Checker" : "Ripples";
	}

	const char* ToPostText( int32 Mode )
	{
		switch ( Mode )
		{
		case 1:
			return "Grayscale";
		case 2:
			return "Vignette";
		case 3:
			return "Mosaic";
		case 4:
			return "Wave";
		default:
			return "None";
		}
	}
}

CShadersPanel::CShadersPanel( const TSharedPtr<FShadersDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CShadersPanel::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Font.IsValid() )
	{
		return;
	}
	FTextStyle Shadow;
	Shadow.bShadow = true;
	Shadow.ShadowColor = FColor32( 0, 0, 0 );

	const int32 Left = FShadersDemoState::ViewLeft + PanelMargin;
	const int32 Top = FShadersDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	CommandList.Blend( EDrawBlend::Alpha, 170 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const FString Lines[NumLines] = {
		FString::Printf( "1  Objects (pixel shader)    %s", ToObjectText( State->ObjectMode ) ),
		FString::Printf( "2  Ground (pixel shader)     %s", ToGroundText( State->GroundMode ) ),
		FString::Printf( "3  Post effect (ShaderRect)  %s", ToPostText( State->PostMode ) ),
		FString::Printf( "4  Time                      %s (%.1f s)", State->bTimeRunning ? "running" : "stopped", State->Time ),
		FString::Printf( "The scene is drawn into a render target with your own HLSL shaders," ),
		FString::Printf( "then the target is shown through a post-effect pixel shader" ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		const FColor32 Color = Index >= 4 ? FColor32( 170, 176, 192 ) : FColor32( 226, 232, 246 );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], Color, Shadow );
	}
}
