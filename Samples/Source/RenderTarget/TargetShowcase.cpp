#include "TargetShowcase.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"
#include "Source/Common/TestPattern.h"

namespace
{
	// 描画先（ミニマップ）の大きさ（ピクセル）
	constexpr int32 MapWidth = 160;
	constexpr int32 MapHeight = 120;
	// 引き伸ばして貼る倍率
	constexpr int32 Zoom = 2;
	// 絵と絵のあいだの間隔（ピクセル）
	constexpr int32 Gap = 40;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 24;
	// 市松模様の 1 マスの大きさ（ピクセル）
	constexpr int32 CheckerSize = 10;
	// 色の見本の大きさ（ピクセル）
	constexpr int32 SwatchSize = 48;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}

	// 保存先（プロジェクトのルートの Saved フォルダー）
	FString GetSavePath()
	{
		return NFileSystem::FindProjectRoot() / "Saved" / "RenderTarget.png";
	}
}

CTargetShowcase::CTargetShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InPattern, const TSharedPtr<CRenderTarget>& InMinimap, const TSharedPtr<CRenderTarget>& InSnapshot )
	: Origin( InOrigin )
	, Pattern( InPattern )
	, Minimap( InMinimap )
	, Snapshot( InSnapshot )
{
}

void CTargetShowcase::GetMarker( int32& OutX, int32& OutY ) const
{
	// ミニマップの真ん中を中心に、楕円を描いて動く
	OutX = MapWidth / 2 + Px( 56.0f * NMath::Cos( Time * 0.7f ) );
	OutY = MapHeight / 2 + Px( 40.0f * NMath::Sin( Time * 1.1f ) );
}

void CTargetShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;

	// 描画先の中身は、前のフレームの Draw が実行されたあとのもの。印の下の色を、読む（メインスレッドで、描いたあとの内容を読める）
	int32 X = 0;
	int32 Y = 0;
	GetMarker( X, Y );
	bProbeValid = Minimap.IsValid() && Minimap->ReadPixel( X, Y, ProbeColor );

	// S キーで、描画先を PNG に保存する（透明な部分も残る）
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::S ) && Minimap.IsValid() )
	{
		const FString Path = GetSavePath();
		SaveStatus = Minimap->Save( Path ) ? FString::Printf( "Saved: %s", *Path ) : FString::Printf( "Failed to save: %s", *Path );
	}
}

void CTargetShowcase::DrawChecker( FDrawContext& CommandList, int32 Left, int32 Top, int32 Width, int32 Height ) const
{
	for ( int32 Y = 0; Y < Height; Y += CheckerSize )
	{
		for ( int32 X = 0; X < Width; X += CheckerSize )
		{
			const bool bDark = ( ( X / CheckerSize ) + ( Y / CheckerSize ) ) % 2 == 0;
			const FColor32 Color = bDark ? FColor32( 60, 60, 60 ) : FColor32( 90, 90, 90 );
			CommandList.Box( Left + X, Top + Y, Left + NMath::Min( X + CheckerSize, Width ), Top + NMath::Min( Y + CheckerSize, Height ), Color );
		}
	}
}

void CTargetShowcase::DrawMinimapScene( FDrawContext& CommandList ) const
{
	// 回る棒・動く円・画像。背景は、透明のまま残す
	const float32 CenterX = static_cast<float32>( MapWidth ) * 0.5f;
	const float32 CenterY = static_cast<float32>( MapHeight ) * 0.5f;
	const float32 Angle = Time * 1.2f;
	const int32 DX = Px( 50.0f * NMath::Cos( Angle ) );
	const int32 DY = Px( 50.0f * NMath::Sin( Angle ) );
	CommandList.Image( 8.0f, 8.0f, Pattern );
	CommandList.Line( Px( CenterX ) - DX, Px( CenterY ) - DY, Px( CenterX ) + DX, Px( CenterY ) + DY, FColor32( 255, 160, 40 ), 6 );
	CommandList.Circle( Px( CenterX + 52.0f * NMath::Cos( Time * 0.9f ) ), Px( CenterY + 36.0f * NMath::Sin( Time * 0.6f ) ), 14, FColor32( 40, 220, 220 ) );
	CommandList.Box( 0, 0, MapWidth, MapHeight, FColor32( 255, 255, 255 ), false );
}

void CTargetShowcase::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	const int32 Top = Px( Origin.y ) + TitleHeight;
	const int32 ZoomLeft = Left + MapWidth + Gap;
	const int32 SnapshotLeft = ZoomLeft + MapWidth * Zoom + Gap;
	// 色の読み取りと保存の説明は、引き伸ばした絵の下に置く
	const int32 ProbeTop = Top + MapHeight * Zoom + Gap + 24;

	CommandList.Text( Left, Px( Origin.y ), "Minimap: CommandList.Image", FColor32::White );
	CommandList.Text( ZoomLeft, Px( Origin.y ), "ImageStretch (2x)", FColor32::White );
	CommandList.Text( SnapshotLeft, Px( Origin.y ), "CopyScreen -> Snapshot", FColor32::White );

	// 1. ミニマップへ描く（描画先を替えるのは、このスコープの中だけ）。背景は透明
	if ( Minimap.IsValid() )
	{
		FTargetScope Scope( CommandList, Minimap );
		CommandList.Clear( FColor32( 0, 0, 0, 0 ) );
		DrawMinimapScene( CommandList );
	}

	// 2. ふつうの画像として、画面へ貼る（透明な部分は、下の市松模様が見える）。左: 等倍、中: 2 倍
	DrawChecker( CommandList, Left, Top, MapWidth, MapHeight );
	CommandList.Image( static_cast<float32>( Left ), static_cast<float32>( Top ), Minimap );
	DrawChecker( CommandList, ZoomLeft, Top, MapWidth * Zoom, MapHeight * Zoom );
	CommandList.ImageStretch( static_cast<float32>( ZoomLeft ), static_cast<float32>( Top ), static_cast<float32>( ZoomLeft + MapWidth * Zoom ), static_cast<float32>( Top + MapHeight * Zoom ), Minimap );

	// 3. 印（描画先から色を読む位置）を、左の絵の上に描く
	int32 MarkerX = 0;
	int32 MarkerY = 0;
	GetMarker( MarkerX, MarkerY );
	CommandList.Box( Left + MarkerX - 4, Top + MarkerY - 4, Left + MarkerX + 5, Top + MarkerY + 5, FColor32::White, false );

	// 4. 画面の一部（いま描いた左の絵。市松模様と印も含む）を、同じ大きさの描画先へ写して、貼る。画面にあるものが、そのまま写る
	CommandList.CopyScreen( Left, Top, Left + MapWidth, Top + MapHeight, Snapshot );
	CommandList.Image( static_cast<float32>( SnapshotLeft ), static_cast<float32>( Top ), Snapshot );

	// 5. 読んだ色
	CommandList.Text( Left, ProbeTop - 24, "Minimap->ReadPixel( x, y, Color ) at the marker:", FColor32::White );
	if ( bProbeValid )
	{
		CommandList.TextF( Left, ProbeTop, FColor32::Gray, "( %d, %d ) = ( R %d, G %d, B %d, A %d )", MarkerX, MarkerY, ProbeColor.r, ProbeColor.g, ProbeColor.b, ProbeColor.a );
		// 色の見本: 市松模様の上に、読んだ色を、読んだアルファで重ねる（透明なら、市松模様のまま）
		DrawChecker( CommandList, Left, ProbeTop + 24, SwatchSize * 2, SwatchSize );
		CommandList.Blend( EDrawBlend::Alpha, ProbeColor.a );
		CommandList.Box( Left, ProbeTop + 24, Left + SwatchSize * 2, ProbeTop + 24 + SwatchSize, FColor32( ProbeColor.r, ProbeColor.g, ProbeColor.b ) );
		CommandList.Blend( EDrawBlend::Opaque );
	}
	CommandList.Text( ZoomLeft, ProbeTop - 24, "Press S: Minimap->Save( \"Saved/RenderTarget.png\" ) (the transparent parts are kept)", FColor32::White );
	if ( !SaveStatus.IsEmpty() )
	{
		CommandList.Text( ZoomLeft, ProbeTop, *SaveStatus, FColor32::Gray );
	}
}
