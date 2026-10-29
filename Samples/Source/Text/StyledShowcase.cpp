#include "StyledShowcase.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 見出しから最初の文字までの高さ（ピクセル）
	constexpr int32 TitleHeight = 28;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CStyledShowcase::CStyledShowcase( const FVector2& InOrigin, const TSharedPtr<CFont>& InFont )
	: Origin( InOrigin )
	, Font( InFont )
{
}

void CStyledShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CStyledShowcase::Draw( FDrawContext& CommandList )
{
	const float32 Left = Origin.x;
	const float32 Top = Origin.y + static_cast<float32>( TitleHeight );
	CommandList.Text( Px( Origin.x ), Px( Origin.y ), "FTextStyle: scale / rotate / shadow", FColor32::White );
	if ( !Font.IsValid() )
	{
		return;
	}

	// 拡大: 文字列の中央を基準にして、脈打つ（中央が、描く位置に来る）
	FTextStyle Pulse;
	Pulse.AnchorX = 0.5f;
	Pulse.AnchorY = 0.5f;
	Pulse.ScaleX = 1.0f + 0.5f * NMath::Sin( Time * 2.0f );
	Pulse.ScaleY = Pulse.ScaleX;
	Font->RecordStyled( CommandList, Left + 130.0f, Top + 30.0f, "Scale", FColor32::White, Pulse );

	// 回転: 中央を基準にして、その点のまわりを回る
	FTextStyle Spin;
	Spin.AnchorX = 0.5f;
	Spin.AnchorY = 0.5f;
	Spin.AngleRadians = Time;
	Font->RecordStyled( CommandList, Left + 130.0f, Top + 130.0f, "Rotate", FColor32( 120, 220, 255 ), Spin );
	CommandList.Circle( Px( Left + 130.0f ), Px( Top + 130.0f ), 3, FColor32( 255, 80, 80 ) );

	// 影: 文字列を、ずらして、別の色で先に描く
	FTextStyle Shadow;
	Shadow.bShadow = true;
	Shadow.ShadowColor = FColor32( 220, 40, 40 );
	Shadow.ShadowOffsetX = 4.0f;
	Shadow.ShadowOffsetY = 4.0f;
	Font->RecordStyled( CommandList, Left + 20.0f, Top + 200.0f, "Shadow", FColor32::White, Shadow );

	// 基準点: 同じ位置 (x, y) に、左上・右上・中央を基準にして描く（十字が、描く位置）
	const float32 AnchorX = Left + 130.0f;
	const float32 AnchorY = Top + 330.0f;
	CommandList.Line( Px( AnchorX ) - 70, Px( AnchorY ), Px( AnchorX ) + 70, Px( AnchorY ), FColor32( 255, 80, 80 ) );
	CommandList.Line( Px( AnchorX ), Px( AnchorY ) - 30, Px( AnchorX ), Px( AnchorY ) + 30, FColor32( 255, 80, 80 ) );
	FTextStyle TopLeft;
	FTextStyle BottomRight;
	BottomRight.AnchorX = 1.0f;
	BottomRight.AnchorY = 1.0f;
	Font->RecordStyled( CommandList, AnchorX, AnchorY, "Ab", FColor32( 255, 255, 100 ), TopLeft );
	Font->RecordStyled( CommandList, AnchorX, AnchorY, "Cd", FColor32( 100, 255, 100 ), BottomRight );

	// 縦書き: 上から下へ並べ、改行で左の列へ進む。基準点 (0, 0) は、列の右上
	FTextStyle Vertical;
	Vertical.bVertical = true;
	Font->RecordStyled( CommandList, Left + 300.0f, Top, "縦書きの\n見本です", FColor32::White, Vertical );
}
