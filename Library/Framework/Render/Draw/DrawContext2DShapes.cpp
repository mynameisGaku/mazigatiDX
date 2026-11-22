#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

namespace
{
	// 文字列つきの命令を、Buffer の最後に足す（命令のヘッダ + データ + 文字列 + 終端、4 バイト境界まで 0）。文字列は、長すぎれば切る
	void PushStyledText( FDrawBuffer& Buffer, const FDrawTextStyledData& Source, const char* String )
	{
		constexpr size_t DataWords = sizeof( FDrawTextStyledData ) / 4;
		const size_t MaxChars = ( static_cast<size_t>( NDrawCommand::MaxWords ) - 1 - DataWords ) * 4 - 1;
		size_t Length = NCString::Length( String );
		if ( Length > MaxChars )
		{
			Length = MaxChars;
		}
		const size_t CharWords = ( Length + 1 + 3 ) / 4;

		FDrawTextStyledData Data = Source;
		Data.Length = static_cast<int32>( Length );

		TArray<uint32>& Words = Buffer.Words;
		const int32 Begin = Words.AddZeroed( static_cast<int32>( 1 + DataWords + CharWords ) );
		uint32* const Out = Words.GetData() + Begin;
		Out[0] = NDrawCommand::MakeHeader( EDrawCommand::TextStyled, 0, static_cast<int32>( 1 + DataWords + CharWords ) );
		FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
		if ( Length > 0 )
		{
			FMemory::Memcpy( Out + 1 + DataWords, String, Length );
		}
	}
}

void FDrawContext2DShapes::Box( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, bool bFill, int32 Thickness )
{
	FDrawBoxData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Box, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext2DShapes::Circle( int32 X, int32 Y, int32 Radius, const FColor32& Color, bool bFill, int32 Thickness )
{
	FDrawCircleData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Radius = Radius;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Circle, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext2DShapes::Line( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, int32 Thickness )
{
	FDrawLineData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Line, 0, Data );
}

void FDrawContext2DShapes::Pixel( int32 X, int32 Y, const FColor32& Color )
{
	FDrawPixelData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Pixel, 0, Data );
}

void FDrawContext2DShapes::Triangle( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, const FColor32& Color, bool bFill )
{
	PushTriangle( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( X2 ), static_cast<float32>( Y2 ), Color, bFill, 1.0f, 0 );
}

void FDrawContext2DShapes::Quad( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 X3, int32 Y3, const FColor32& Color, bool bFill )
{
	PushQuad( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( X2 ), static_cast<float32>( Y2 ), static_cast<float32>( X3 ), static_cast<float32>( Y3 ), Color, bFill, 1.0f, 0 );
}

void FDrawContext2DShapes::Oval( int32 X, int32 Y, int32 Rx, int32 Ry, const FColor32& Color, bool bFill, int32 Thickness )
{
	PushOval( static_cast<float32>( X ), static_cast<float32>( Y ), static_cast<float32>( Rx ), static_cast<float32>( Ry ), Color, bFill, static_cast<float32>( Thickness ), 0, 0 );
}

void FDrawContext2DShapes::RoundBox( int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Rx, int32 Ry, const FColor32& Color, bool bFill )
{
	PushRoundBox( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( Rx ), static_cast<float32>( Ry ), Color, bFill, 1.0f, 0, 0 );
}

void FDrawContext2DShapes::LineAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, float32 Thickness )
{
	if ( !AllDrawable( X0, Y0, X1, Y1 ) || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawLineAAData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::LineAA, 0, Data );
}

void FDrawContext2DShapes::BoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, const FColor32& Color, bool bFill, float32 Thickness )
{
	if ( !AllDrawable( X0, Y0, X1, Y1 ) || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawBoxAAData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::BoxAA, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext2DShapes::CircleAA( float32 X, float32 Y, float32 Radius, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments )
{
	if ( !AllDrawable( X, Y, Radius ) || Radius < 0.0f || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawCircleAAData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Radius = Radius;
	Data.Thickness = Thickness;
	Data.NumSegments = ClampSegments( NumSegments );
	Data.Color = Rgb( Color );
	Push( EDrawCommand::CircleAA, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext2DShapes::OvalAA( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments )
{
	PushOval( X, Y, Rx, Ry, Color, bFill, Thickness, ClampSegments( NumSegments ), FDrawFlag::AntiAlias );
}

void FDrawContext2DShapes::TriangleAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill, float32 Thickness )
{
	PushTriangle( X0, Y0, X1, Y1, X2, Y2, Color, bFill, Thickness, FDrawFlag::AntiAlias );
}

void FDrawContext2DShapes::QuadAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill, float32 Thickness )
{
	PushQuad( X0, Y0, X1, Y1, X2, Y2, X3, Y3, Color, bFill, Thickness, FDrawFlag::AntiAlias );
}

void FDrawContext2DShapes::RoundBoxAA( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments )
{
	PushRoundBox( X0, Y0, X1, Y1, Rx, Ry, Color, bFill, Thickness, ClampSegments( NumSegments ), FDrawFlag::AntiAlias );
}

void FDrawContext2DShapes::Sprite( float32 X, float32 Y, int32 GraphHandle, float32 Scale, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	FDrawSpriteData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Scale = Scale;
	Data.Angle = AngleRadians;
	Data.Handle = GraphHandle;
	Push( EDrawCommand::Sprite, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DShapes::SpriteRect( float32 X, float32 Y, int32 GraphHandle, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	FDrawSpriteRectData Data;
	Data.X = X;
	Data.Y = Y;
	Data.ScaleX = ScaleX;
	Data.ScaleY = ScaleY;
	Data.Angle = AngleRadians;
	Data.PivotX = PivotX;
	Data.PivotY = PivotY;
	Data.Handle = GraphHandle;
	Data.SrcX = SrcX;
	Data.SrcY = SrcY;
	Data.SrcW = SrcW;
	Data.SrcH = SrcH;
	Push( EDrawCommand::SpriteRect, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DShapes::Ribbon3D( const FDrawRibbonData::FPoint* Points, int32 NumPoints, int32 Texture, float32 UVRepeat, int32 JoinFlags )
{
	if ( Points == nullptr || NumPoints < 2 || NumPoints > FDrawRibbonData::MaxPoints || Texture < -1 || !NMath::IsFinite( UVRepeat ) || UVRepeat <= 0.0f || JoinFlags < 0 || ( JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || NumPoints - static_cast<int32>( ( JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 )
	{
		return;
	}
	// テクスチャを反復する際に必要な、分割後の区間数。
	int32 TextureSections = 0;
	// 点列の途中だけを落として、不連続な経路へ変えてしまわないよう全点を先に確かめる。
	for ( int32 Index = 0; Index < NumPoints; ++Index )
	{
		// 検査してから複写する、今回の経路の点。
		const FDrawRibbonData::FPoint& Point = Points[Index];
		if ( !NMath::IsFinite( Point.Pos[0] ) || !NMath::IsFinite( Point.Pos[1] ) || !NMath::IsFinite( Point.Pos[2] ) || !NMath::IsFinite( Point.Width ) || Point.Width < 0.0f || !NMath::IsFinite( Point.U ) || Point.U < 0.0f || !NMath::IsFinite( Point.U * UVRepeat ) )
		{
			return;
		}
		if ( Index > 0 && Point.U < Points[Index - 1].U )
		{
			return;
		}
		// 幅と接線には全点を使い、補助点が作る区間だけを除外する。
		const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == NumPoints && ( JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
		if ( Texture >= 0 && bVisibleSegment )
		{
			// 始点側で使い始める画像の反復番号。
			const float64 First = NMath::Floor( static_cast<float64>( Points[Index - 1].U * UVRepeat ) );
			// 終点までに必要な反復数。整数境界と一致する終点では余分な区間を作らない。
			const float64 Sections = NMath::Max( 1.0, NMath::Ceil( static_cast<float64>( Point.U * UVRepeat ) ) - First );
			if ( Sections > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TextureSections ) )
			{
				return;
			}
			TextureSections += static_cast<int32>( Sections );
		}
	}
	// 命令の固定部分のワード数。
	constexpr size_t DataWords = sizeof( FDrawRibbonData ) / 4;
	// 点列の可変部分のワード数。点数の上限を検査した後に計算する。
	const size_t PointWords = static_cast<size_t>( NumPoints ) * sizeof( FDrawRibbonData::FPoint ) / 4;
	// 命令ヘッダー自身を含む全体のワード数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return;
	}
	// Pointsが既存の記録領域を参照する場合も、広げて失効する前に複写する。
	TArray<FDrawRibbonData::FPoint> Copied;
	Copied.Append( Points, NumPoints );
	// 点列に先行する、固定長の描画設定。
	FDrawRibbonData Data;
	Data.Texture = Texture;
	Data.NumPoints = NumPoints;
	Data.UVRepeat = UVRepeat;
	Data.JoinFlags = JoinFlags;
	// 既存の描画命令が入っている、今回の追記先。
	TArray<uint32>& Words = Buffer->Words;
	// 追加する命令の先頭。
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::Ribbon3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	FMemory::Memcpy( Out + 1 + DataWords, Copied.GetData(), static_cast<size_t>( NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
}

void FDrawContext2DShapes::Text( int32 X, int32 Y, const char* String, const FColor32& Color )
{
	constexpr size_t DataWords = sizeof( FDrawTextData ) / 4;
	// ヘッダに収まる大きさまで（それより長い文字列は切る）
	const size_t MaxChars = ( static_cast<size_t>( NDrawCommand::MaxWords ) - 1 - DataWords ) * 4 - 1;
	size_t Length = String != nullptr ? NCString::Length( String ) : 0;
	if ( Length > MaxChars )
	{
		Length = MaxChars;
	}
	const size_t CharWords = ( Length + 1 + 3 ) / 4;   // '\0' を含めて、4 バイト境界まで

	FDrawTextData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Color = Rgb( Color );
	Data.Length = static_cast<int32>( Length );

	TArray<uint32>& Words = Buffer->Words;
	const int32 Begin = Words.AddZeroed( static_cast<int32>( 1 + DataWords + CharWords ) );   // 0 で埋まる（終端の '\0' と、境界までの隙間）
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::Text, 0, static_cast<int32>( 1 + DataWords + CharWords ) );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	if ( Length > 0 )
	{
		FMemory::Memcpy( Out + 1 + DataWords, String, Length );
	}
}

void FDrawContext2DShapes::TextWithFont( int32 FontHandle, int32 X, int32 Y, const char* String, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor )
{
	constexpr size_t DataWords = sizeof( FDrawTextFontData ) / 4;
	const size_t MaxChars = ( static_cast<size_t>( NDrawCommand::MaxWords ) - 1 - DataWords ) * 4 - 1;
	size_t Length = String != nullptr ? NCString::Length( String ) : 0;
	if ( Length > MaxChars )
	{
		Length = MaxChars;
	}
	const size_t CharWords = ( Length + 1 + 3 ) / 4;

	FDrawTextFontData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Color = Rgb( Color );
	Data.Length = static_cast<int32>( Length );
	Data.Font = FontHandle;
	Data.EdgeColor = Rgb( EdgeColor );

	uint8 Flags = 0;
	if ( Align == EDrawTextAlign::Center )
	{
		Flags = FDrawFlag::AlignCenter;
	}
	else if ( Align == EDrawTextAlign::Right )
	{
		Flags = FDrawFlag::AlignRight;
	}

	TArray<uint32>& Words = Buffer->Words;
	const int32 Begin = Words.AddZeroed( static_cast<int32>( 1 + DataWords + CharWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::TextFont, Flags, static_cast<int32>( 1 + DataWords + CharWords ) );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	if ( Length > 0 )
	{
		FMemory::Memcpy( Out + 1 + DataWords, String, Length );
	}
}

bool FDrawContext2DShapes::TextStyled( int32 FontHandle, float32 X, float32 Y, const char* String, const FColor32& Color, const FTextStyle& Style )
{
	if ( String == nullptr || !AllDrawable( X, Y, Style.ScaleX, Style.ScaleY, Style.AngleRadians, Style.AnchorX, Style.AnchorY, Style.ShadowOffsetX, Style.ShadowOffsetY ) || Style.ScaleX <= 0.0f || Style.ScaleY <= 0.0f )
	{
		return false;
	}
	FDrawTextStyledData Data;
	Data.X = X;
	Data.Y = Y;
	Data.ScaleX = Style.ScaleX;
	Data.ScaleY = Style.ScaleY;
	Data.Angle = Style.AngleRadians;
	Data.AnchorX = Style.AnchorX;
	Data.AnchorY = Style.AnchorY;
	Data.Font = FontHandle;
	Data.Vertical = Style.bVertical ? 1 : 0;
	if ( Style.bShadow )
	{
		FDrawTextStyledData Shadow = Data;
		Shadow.X = X + Style.ShadowOffsetX;
		Shadow.Y = Y + Style.ShadowOffsetY;
		Shadow.Color = Rgb( Style.ShadowColor );
		Shadow.EdgeColor = Rgb( Style.ShadowColor );
		PushStyledText( *Buffer, Shadow, String );
	}
	Data.Color = Rgb( Color );
	Data.EdgeColor = Rgb( Style.EdgeColor );
	PushStyledText( *Buffer, Data, String );
	return true;
}

void FDrawContext2DShapes::TextF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... )
{
	char Small[256];

	va_list Args;
	va_start( Args, Format );
	va_list ArgsCopy;
	va_copy( ArgsCopy, Args );
	const int Needed = NCString::FormatV( Small, sizeof( Small ), Format, Args );
	va_end( Args );

	if ( Needed < 0 )
	{
		Text( X, Y, "", Color );
	}
	else if ( Needed < static_cast<int>( sizeof( Small ) ) )
	{
		Text( X, Y, Small, Color );
	}
	else
	{
		TArray<char> Large;
		Large.SetNum( Needed + 1 );
		NCString::FormatV( Large.GetData(), static_cast<size_t>( Large.Num() ), Format, ArgsCopy );
		Text( X, Y, Large.GetData(), Color );
	}
	va_end( ArgsCopy );
}
