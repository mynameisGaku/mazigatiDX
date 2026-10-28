#include "BlendShowcase.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 絵 1 つぶんの幅（ピクセル）。名前が重ならない間隔
	constexpr float32 CellWidth = 180.0f;
	// 下地の大きさ（ピクセル）
	constexpr int32 BackdropHalfWidth = 60;
	constexpr int32 BackdropHalfHeight = 36;
	// 円の半径（ピクセル）
	constexpr int32 CircleRadius = 30;
	// 上の列の絵の数
	constexpr int32 NumCells = 7;
	// アルファテストの箱の数・大きさ・間隔（ピクセル）
	constexpr int32 NumBoxes = 8;
	constexpr int32 BoxWidth = 56;
	constexpr int32 BoxHeight = 36;
	constexpr int32 BoxPitch = 64;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CBlendShowcase::CBlendShowcase( const FVector2& InOrigin )
	: Origin( InOrigin )
{
}

void CBlendShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CBlendShowcase::Draw( FDrawContext& CommandList )
{
	for ( int32 Index = 0; Index < NumCells; ++Index )
	{
		DrawCell( CommandList, Index, FVector2( Origin.x + CellWidth * static_cast<float32>( Index ), Origin.y ) );
	}
	DrawAlphaTest( CommandList );
}

void CBlendShowcase::DrawBackdrop( FDrawContext& CommandList, const FVector2& Center ) const
{
	const int32 X = Px( Center.x );
	const int32 Y = Px( Center.y );
	CommandList.Box( X - BackdropHalfWidth, Y - BackdropHalfHeight, X, Y + BackdropHalfHeight, FColor32( 50, 110, 170 ) );
	CommandList.Box( X, Y - BackdropHalfHeight, X + BackdropHalfWidth, Y + BackdropHalfHeight, FColor32( 230, 170, 60 ) );
	CommandList.Box( X - BackdropHalfWidth, Y - 4, X + BackdropHalfWidth, Y + 4, FColor32( 120, 120, 120 ) );
}

void CBlendShowcase::DrawCell( FDrawContext& CommandList, int32 Index, const FVector2& Center ) const
{
	DrawBackdrop( CommandList, Center );

	// 円は、下地の上を左右に動く
	const int32 CircleX = Px( Center.x + 24.0f * NMath::Sin( Time * 1.4f ) );
	const int32 CircleY = Px( Center.y );
	const FColor32 CircleColor( 230, 90, 60 );
	const char* Name = "";
	switch ( Index )
	{
	case 0:
		// 半透明（128 / 255）で重ねる
		Name = "Alpha 128";
		CommandList.Blend( EDrawBlend::Alpha, 128 );
		break;
	case 1:
		// 下地の色に足す（明るくなる）
		Name = "Add";
		CommandList.Blend( EDrawBlend::Add );
		break;
	case 2:
		// 下地の色から引く（暗くなる）
		Name = "Subtract";
		CommandList.Blend( EDrawBlend::Subtract );
		break;
	case 3:
		// 下地の色に掛ける（暗くなる）
		Name = "Multiply";
		CommandList.Blend( EDrawBlend::Multiply );
		break;
	case 4:
		// 描く色を反転する
		Name = "Invert";
		CommandList.Blend( EDrawBlend::Invert );
		break;
	case 5:
	{
		// 自分で決めたブレンド: スクリーン合成 = 描く色 + 下地の色 x ( 1 - 描く色 )。明るくなるが、白は超えない
		Name = "CustomBlend (screen)";
		FCustomBlend Screen;
		Screen.SrcColor = EDrawBlendFactor::One;
		Screen.DestColor = EDrawBlendFactor::InvSrcColor;
		Screen.ColorOp = EDrawBlendOp::Add;
		CommandList.CustomBlend( Screen );
		break;
	}
	default:
		// ブレンドはせず、描く色に色を足す（図形も画像も、全部に掛かる）
		Name = "AddColor( 90, 60, 0 )";
		CommandList.AddColor( 90, 60, 0 );
		break;
	}
	CommandList.Circle( CircleX, CircleY, CircleRadius, CircleColor );

	// 次の絵に引き継がない（これを忘れると、あとの絵も同じ状態で描かれる）
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.AddColor( 0, 0, 0 );
	CommandList.Text( Px( Center.x ) - BackdropHalfWidth, Px( Center.y ) + 46, Name, FColor32::White );
}

void CBlendShowcase::DrawAlphaTest( FDrawContext& CommandList ) const
{
	// 基準値は 0 から 255 を繰り返す。透明度が基準値より大きい箱だけが描かれる（透明度は、左から 32, 64, ... 255）
	const int32 Reference = static_cast<int32>( NMath::Fmod( Time * 40.0f, 256.0f ) );
	const int32 Left = Px( Origin.x ) - BackdropHalfWidth;
	const int32 Top = Px( Origin.y ) + 130;
	// 描かれなかった箱の場所が分かるよう、枠は、いつも描く（状態を変える前に、まとめて描く。変えたあとに描くと、枠にも状態が掛かる）
	for ( int32 Index = 0; Index < NumBoxes; ++Index )
	{
		const int32 X = Left + BoxPitch * Index;
		CommandList.Box( X, Top, X + BoxWidth, Top + BoxHeight, FColor32( 90, 90, 90 ), false );
	}
	// 箱の中。透明度は箱ごとに違い、アルファテストは基準値より透明度が高いものだけを通す
	for ( int32 Index = 0; Index < NumBoxes; ++Index )
	{
		const int32 X = Left + BoxPitch * Index;
		CommandList.Blend( EDrawBlend::Alpha, NMath::Min( 32 * ( Index + 1 ), 255 ) );
		CommandList.AlphaTest( EDrawCompare::Greater, Reference );
		CommandList.Box( X + 2, Top + 2, X + BoxWidth - 2, Top + BoxHeight - 2, FColor32::White );
	}
	// 使い終わったら戻す（このあとの文字が、アルファテストで消えないように）
	CommandList.AlphaTestOff();
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.TextF( Left, Top + 46, FColor32::White, "AlphaTest( Greater, %d ): only the boxes with an alpha greater than %d are drawn", Reference, Reference );
}
