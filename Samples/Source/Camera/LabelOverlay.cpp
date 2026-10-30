#include "LabelOverlay.h"

namespace
{
	// 物の上のどれだけ離れたところへ、名前を置くか（物を囲む球の半径に足す。ワールドの単位）
	constexpr float32 LabelRise = 0.6f;

	// 画面の端に出す説明の位置（見せる範囲の左上から）
	constexpr int32 PanelMargin = 16;
}

CLabelOverlay::CLabelOverlay( const TSharedPtr<FCameraDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CLabelOverlay::Draw( FDrawContext& CommandList )
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

	// 物の名前と、距離。見えていない物（視錐台の外）は、描かれていないので、名前も出さない
	for ( int32 Index = 0; Index < State->Objects.Num(); ++Index )
	{
		if ( !State->Visible.IsValidIndex( Index ) || !State->Visible[Index] )
		{
			continue;
		}
		const FDemoObject& Object = State->Objects[Index];
		FVector2 Pixel;
		if ( !Camera.WorldToScreen( Viewport, Object.Position + FVector3( 0.0f, Object.GetBoundingRadius() + LabelRise, 0.0f ), Pixel ) )
		{
			continue;
		}
		if ( Pixel.x < static_cast<float32>( FCameraDemoState::ViewLeft ) || Pixel.x > static_cast<float32>( FCameraDemoState::ViewLeft + FCameraDemoState::ViewWidth ) || Pixel.y < static_cast<float32>( FCameraDemoState::ViewTop ) || Pixel.y > static_cast<float32>( FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight ) )
		{
			continue;
		}
		const bool bHovered = Index == State->HoveredIndex;
		const float32 Distance = ( Object.Position - Camera.Position ).Length();
		const FString Label = FString::Printf( "%s  %.1f", *Object.Name, Distance );
		Font->RecordStyled( CommandList, Pixel.x, Pixel.y, *Label, bHovered ? FColor32( 255, 230, 90 ) : FColor32( 230, 234, 246 ), Centered );
	}

	// カメラの説明と、視錐台カリングの結果
	const int32 Left = FCameraDemoState::ViewLeft + PanelMargin;
	const int32 Top = FCameraDemoState::ViewTop + PanelMargin;
	CommandList.Blend( EDrawBlend::Alpha, 150 );
	CommandList.RoundBox( Left - 8, Top - 8, Left + 380, Top + 108, 8, 8, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );
	const bool bPerspective = Camera.Projection == ECameraProjection::Perspective;
	const FString Projection = bPerspective ? FString::Printf( "Perspective   FovY %.0f deg", Camera.FovY * 180.0f / NMath::PI ) : FString::Printf( "Orthographic   Height %.1f", Camera.OrthoHeight );
	Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top ), *Projection, FColor32::White, Shadow );
	const FString Position = FString::Printf( "Position (%.1f, %.1f, %.1f)", Camera.Position.x, Camera.Position.y, Camera.Position.z );
	Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + 24 ), *Position, FColor32( 190, 196, 214 ), Shadow );
	const FString Culling = FString::Printf( "In the frustum: %d / %d objects", State->CountVisible(), State->Objects.Num() );
	Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + 48 ), *Culling, FColor32( 120, 235, 150 ), Shadow );
	const FString Hovered = State->Objects.IsValidIndex( State->HoveredIndex ) ? FString::Printf( "Mouse ray hits: %s", *State->Objects[State->HoveredIndex].Name ) : FString( "Mouse ray hits: (nothing)" );
	Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + 72 ), *Hovered, FColor32( 255, 230, 90 ), Shadow );
}
