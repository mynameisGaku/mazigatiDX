#include "Shapes3DPanel.h"

#include "Source/Shapes3D/Shapes3DScenery.h"

namespace
{
	// 一覧の位置と大きさ（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
	constexpr int32 PanelWidth = 520;
	constexpr int32 LineHeight = 22;
	constexpr int32 NumLines = 6;

	const char* OnOff( bool bValue )
	{
		return bValue ? "On" : "Off";
	}

	const char* ToText( int32 CullingMode )
	{
		switch ( CullingMode )
		{
		case 1:
			return "BackFaces (hide back faces)";
		case 2:
			return "FrontFaces (hide front faces)";
		default:
			return "None (both sides)";
		}
	}
}

CShapes3DPanel::CShapes3DPanel( const TSharedPtr<FShapes3DDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CShapes3DPanel::Draw( FDrawContext& CommandList )
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
	const int32 Left = FShapes3DDemoState::ViewLeft + PanelMargin;
	const int32 Top = FShapes3DDemoState::ViewTop + PanelMargin;
	const int32 PanelRight = Left + PanelWidth;
	const int32 PanelBottom = Top + NumLines * LineHeight + 8;

	// 見どころの名札
	const TArray<NShapes3DScenery::FStageLabel> Labels = NShapes3DScenery::MakeLabels( *State );
	for ( const NShapes3DScenery::FStageLabel& Label : Labels )
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
		if ( Pixel.x < static_cast<float32>( FShapes3DDemoState::ViewLeft ) || Pixel.x > static_cast<float32>( FShapes3DDemoState::ViewLeft + FShapes3DDemoState::ViewWidth ) || Pixel.y < static_cast<float32>( FShapes3DDemoState::ViewTop ) || Pixel.y > static_cast<float32>( FShapes3DDemoState::ViewTop + FShapes3DDemoState::ViewHeight ) )
		{
			continue;
		}
		Font->RecordStyled( CommandList, Pixel.x, Pixel.y, *Label.Text, FColor32( 235, 238, 250 ), Centered );
	}

	// 設定の一覧
	CommandList.Blend( EDrawBlend::Alpha, 160 );
	CommandList.RoundBox( Left - 8, Top - 8, PanelRight, PanelBottom, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	const FString Lines[NumLines] = {
		FString::Printf( "1  Cones drawn as wireframe       %s", OnOff( State->bWireCones ) ),
		FString::Printf( "2  Fog (Primitive3D yes, Pixel3D no) %s", OnOff( State->bFog ) ),
		FString::Printf( "3  Culling of polygons            %s", ToText( State->CullingMode ) ),
		FString::Printf( "4  Billboard pivot                %s", State->bStandOnGround ? "feet (0.5, 1)" : "center (0.5, 0.5)" ),
		FString::Printf( "5  Translucent images             %s", OnOff( State->bTranslucent ) ),
		FString::Printf( "6  Lighting                       %s", OnOff( State->bLighting ) ),
	};
	for ( int32 Index = 0; Index < NumLines; ++Index )
	{
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LineHeight ), *Lines[Index], FColor32( 220, 226, 242 ), Shadow );
	}
}
