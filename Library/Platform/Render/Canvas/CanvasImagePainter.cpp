// SPDX-License-Identifier: MIT
#include "CanvasImagePainter.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using NCanvasValidation::Valid;
	using NCanvasValidation::ValidPoints;
}

CCanvasImagePainter::CCanvasImagePainter( FCanvasExecution& InRun )
	: Run( InRun )
	, Canvas( Run.Canvas )
	, bStopped( Run.bStopped )
	, bLinearFilter( Run.State.bLinearFilter )
	, bExplicitFilter( Run.State.bExplicitFilter )
	, AddColorValue( Run.State.AddColorValue )
	, Transform( Run.State.Transform )
{
}

void CCanvasImagePainter::Unsupported( const FDrawCommandView& View, const char* Reason )
{
	FCanvasExecution::Unsupported( View, Reason );
}

FColor32 CCanvasImagePainter::Color( FDrawRgb Value )
{
	return Run.State.MakeColor( Value );
}

bool CCanvasImagePainter::GetImage( int32 Handle, FTextureDrawView& Image )
{
	return Run.GetImage( Handle, Image );
}

// 画像の中の矩形を、UV にする。画像の外へ出る矩形は false。
bool CCanvasImagePainter::SourceUV( const FTextureDrawView& Image, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 ( &UV )[4] )
{
	if ( SrcX < 0 || SrcY < 0 || SrcW <= 0 || SrcH <= 0 || static_cast<int64>( SrcX ) + SrcW > Image.Width || static_cast<int64>( SrcY ) + SrcH > Image.Height )
	{
		return false;
	}
	UV[0] = static_cast<float32>( SrcX ) / Image.Width;
	UV[1] = static_cast<float32>( SrcY ) / Image.Height;
	UV[2] = static_cast<float32>( SrcX + SrcW ) / Image.Width;
	UV[3] = static_cast<float32>( SrcY + SrcH ) / Image.Height;
	return true;
}

// 反転の指定を、UV の入れ替えにする（位置はそのまま）。
void CCanvasImagePainter::FlipUV( const FDrawCommandView& View, float32 ( &UV )[4] )
{
	if ( View.Has( FDrawFlag::FlipX ) )
	{
		const float32 Old = UV[0]; // 左右を交換する。
		UV[0] = UV[2];
		UV[2] = Old;
	}
	if ( View.Has( FDrawFlag::FlipY ) )
	{
		const float32 Old = UV[1]; // 上下を交換する。
		UV[1] = UV[3];
		UV[3] = Old;
	}
}

// 画像を、4点（左上・右上・右下・左下）へ、変換してから貼る。
void CCanvasImagePainter::DrawImage( const FTextureDrawView& Image, const float32 ( &Points )[8], const float32 ( &UV )[4] )
{
	float32 Moved[8]; // 変換後の4点。
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		float32 X = Points[Index * 2], Y = Points[Index * 2 + 1]; // 変換する点。
		Transform.Apply( X, Y );
		Moved[Index * 2] = X;
		Moved[Index * 2 + 1] = Y;
	}
	if ( !ValidPoints( Points, 8 ) || !ValidPoints( Moved, 8 ) )
	{
		return;
	}
	if ( !Canvas->SetTextureFilter( bLinearFilter ) && bExplicitFilter )
	{
		return;
	}
	if ( AddColorValue[0] != 0 || AddColorValue[1] != 0 || AddColorValue[2] != 0 )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "AddColor (image / text)", "AddColor is applied to flat shapes only; images and text are drawn without it" );
	}
	// 派生画像は、画像の中のUVを、元の画像のUVへ写す（ふつうの画像は 0, 0, 1, 1 で、そのまま）。
	const float32 Mapped[4] = { Image.UvRect[0] + UV[0] * ( Image.UvRect[2] - Image.UvRect[0] ), Image.UvRect[1] + UV[1] * ( Image.UvRect[3] - Image.UvRect[1] ),
		Image.UvRect[0] + UV[2] * ( Image.UvRect[2] - Image.UvRect[0] ), Image.UvRect[1] + UV[3] * ( Image.UvRect[3] - Image.UvRect[1] ) };
	Canvas->Image( Image, Moved, Mapped, Color( FDrawRgb() ) );
}

// 画像を、凸な多角形（x, y, u, v の頂点列）へ、変換してから貼る。
void CCanvasImagePainter::DrawImagePolygon( const FTextureDrawView& Image, TArray<float32>& Vertices )
{
	const int32 Count = Vertices.Num() / 4; // 頂点の数。
	if ( Count < 3 || !ValidPoints( Vertices.GetData(), Vertices.Num() ) )
	{
		return;
	}
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		Transform.Apply( Vertices[Index * 4], Vertices[Index * 4 + 1] );
	}
	if ( !ValidPoints( Vertices.GetData(), Vertices.Num() ) )
	{
		return;
	}
	if ( !Canvas->SetTextureFilter( bLinearFilter ) && bExplicitFilter )
	{
		return;
	}
	for ( int32 Index = 0; Index < Count; ++Index ) // 派生画像は、画像の中のUVを、元の画像のUVへ写す。
	{
		Vertices[Index * 4 + 2] = Image.UvRect[0] + Vertices[Index * 4 + 2] * ( Image.UvRect[2] - Image.UvRect[0] );
		Vertices[Index * 4 + 3] = Image.UvRect[1] + Vertices[Index * 4 + 3] * ( Image.UvRect[3] - Image.UvRect[1] );
	}
	Canvas->ImagePolygon( Image, Vertices.GetData(), Count, Color( FDrawRgb() ) );
}

// 画像の中の矩形を、基準点まわりに拡大・回転して描く（Sprite / SpriteRect / SpritePivot の共通）。
// 反転は、基準点を軸にする（反転しても、基準点は (X, Y) のまま）。
void CCanvasImagePainter::DrawSprite( const FDrawCommandView& View, const FTextureDrawView& Image, float32 X, float32 Y, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 Angle, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH )
{
	float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
	if ( !SourceUV( Image, SrcX, SrcY, SrcW, SrcH, UV ) || !Valid( Angle ) || !Valid( ScaleX ) || !Valid( ScaleY ) || !Valid( PivotX ) || !Valid( PivotY ) )
	{
		return;
	}
	const float32 SignX = View.Has( FDrawFlag::FlipX ) ? -1.0f : 1.0f, SignY = View.Has( FDrawFlag::FlipY ) ? -1.0f : 1.0f; // 基準点を軸にした反転。
	const float32 Left = ( 0 - PivotX ) * ScaleX * SignX, Right = ( SrcW - PivotX ) * ScaleX * SignX; // 基準点からの、左右の端。
	const float32 Top = ( 0 - PivotY ) * ScaleY * SignY, Bottom = ( SrcH - PivotY ) * ScaleY * SignY; // 基準点からの、上下の端。
	const float32 Cos = NMath::Cos( Angle ), Sin = NMath::Sin( Angle ); // 画面（Y が下）での回転。正は時計回り。
	const float32 Local[8] = { Left, Top, Right, Top, Right, Bottom, Left, Bottom }; // 画像の左上・右上・右下・左下。
	float32 Points[8]; // 回転して、(X, Y) へ動かした4点。
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Points[Index * 2] = X + Local[Index * 2] * Cos - Local[Index * 2 + 1] * Sin;
		Points[Index * 2 + 1] = Y + Local[Index * 2] * Sin + Local[Index * 2 + 1] * Cos;
	}
	DrawImage( Image, Points, UV );
}

bool CCanvasImagePainter::Execute( const FDrawCommandView& View )
{
	switch ( View.Type )
	{
	case EDrawCommand::Image:
	{
		if ( !View.CanRead<FDrawImageData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImageData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( !GetImage( Data.Handle, Image ) )
		{
			break;
		}
		float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y + static_cast<float32>( Image.Height ), Data.X, Data.Y + static_cast<float32>( Image.Height ) }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::ImageStretch:
	{
		if ( !View.CanRead<FDrawImageStretchData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImageStretchData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( !GetImage( Data.Handle, Image ) )
		{
			break;
		}
		float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::ImageQuad:
	{
		if ( !View.CanRead<FDrawImageQuadData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImageQuadData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( !GetImage( Data.Handle, Image ) )
		{
			break;
		}
		float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::ImagePart:
	{
		if ( !View.CanRead<FDrawImagePartData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImagePartData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
		if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
		{
			break;
		}
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y + static_cast<float32>( Data.SrcH ), Data.X, Data.Y + static_cast<float32>( Data.SrcH ) }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::ImagePartStretch:
	{
		if ( !View.CanRead<FDrawImagePartStretchData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImagePartStretchData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
		if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
		{
			break;
		}
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::ImagePartQuad:
	{
		if ( !View.CanRead<FDrawImagePartQuadData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImagePartQuadData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
		if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
		{
			break;
		}
		FlipUV( View, UV );
		const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
		DrawImage( Image, Points, UV );
		break;
	}
	case EDrawCommand::Sprite:
	{
		if ( !View.CanRead<FDrawSpriteData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawSpriteData>(); // 中心・拡大率・回転。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( GetImage( Data.Handle, Image ) )
		{
			DrawSprite( View, Image, Data.X, Data.Y, Image.Width * 0.5f, Image.Height * 0.5f, Data.Scale, Data.Scale, Data.Angle, 0, 0, Image.Width, Image.Height );
		}
		break;
	}
	case EDrawCommand::SpriteRect:
	{
		if ( !View.CanRead<FDrawSpriteRectData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawSpriteRectData>(); // 画像の一部分・基準点・拡大率・回転。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( GetImage( Data.Handle, Image ) )
		{
			DrawSprite( View, Image, Data.X, Data.Y, Data.PivotX, Data.PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH );
		}
		break;
	}
	case EDrawCommand::SpritePivot:
	{
		if ( !View.CanRead<FDrawSpritePivotData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawSpritePivotData>(); // 基準点・拡大率・回転。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( GetImage( Data.Handle, Image ) )
		{
			DrawSprite( View, Image, Data.X, Data.Y, Data.PivotX, Data.PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, 0, 0, Image.Width, Image.Height );
		}
		break;
	}
	case EDrawCommand::Tile:
	{
		if ( !View.CanRead<FDrawTileData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawTileData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( !GetImage( Data.Handle, Image ) || !Valid( Data.Scale ) || Data.Scale <= 0 || !Valid( Data.Angle ) || Data.X1 <= Data.X0 || Data.Y1 <= Data.Y0 )
		{
			break;
		}
		if ( Data.Angle != 0.0f )
		{
			Unsupported( View, "A tile with an angle is skipped" );
			break;
		}
		// DxLibのDrawTileと同じ並べ方: 拡大した画像の中心を「矩形の中心 - 拡大率 x (Tx, Ty)」に置く。(Tx, Ty) は整数。
		const float32 TileWidth = static_cast<float32>( Image.Width ) * Data.Scale, TileHeight = static_cast<float32>( Image.Height ) * Data.Scale; // 1枚の大きさ。
		const int32 ShiftX = NMath::RoundToInt( ( ( static_cast<float32>( Data.X1 - Data.X0 ) - TileWidth ) * 0.5f - static_cast<float32>( Data.OffsetX ) ) / Data.Scale );
		const int32 ShiftY = NMath::RoundToInt( ( ( static_cast<float32>( Data.Y1 - Data.Y0 ) - TileHeight ) * 0.5f - static_cast<float32>( Data.OffsetY ) ) / Data.Scale );
		const float32 Left = static_cast<float32>( Data.X0 + Data.X1 ) * 0.5f - Data.Scale * static_cast<float32>( ShiftX ) - TileWidth * 0.5f; // 基準になる1枚の左端。
		const float32 Top = static_cast<float32>( Data.Y0 + Data.Y1 ) * 0.5f - Data.Scale * static_cast<float32>( ShiftY ) - TileHeight * 0.5f; // 基準になる1枚の上端。
		const int32 FirstX = static_cast<int32>( NMath::Floor( ( static_cast<float32>( Data.X0 ) - Left ) / TileWidth ) ), LastX = static_cast<int32>( NMath::Ceil( ( static_cast<float32>( Data.X1 ) - Left ) / TileWidth ) );
		const int32 FirstY = static_cast<int32>( NMath::Floor( ( static_cast<float32>( Data.Y0 ) - Top ) / TileHeight ) ), LastY = static_cast<int32>( NMath::Ceil( ( static_cast<float32>( Data.Y1 ) - Top ) / TileHeight ) );
		if ( static_cast<int64>( LastX - FirstX ) * ( LastY - FirstY ) > 4096 )
		{
			Unsupported( View, "A tile with more than 4096 pieces is skipped" );
			break;
		}
		for ( int32 Row = FirstY; Row < LastY; ++Row )
		{
			for ( int32 Column = FirstX; Column < LastX; ++Column )
			{
				const float32 TileX = Left + static_cast<float32>( Column ) * TileWidth, TileY = Top + static_cast<float32>( Row ) * TileHeight; // この1枚の左上。
				const float32 X0 = NMath::Max( TileX, static_cast<float32>( Data.X0 ) ), X1 = NMath::Min( TileX + TileWidth, static_cast<float32>( Data.X1 ) ); // 矩形で切った範囲。
				const float32 Y0 = NMath::Max( TileY, static_cast<float32>( Data.Y0 ) ), Y1 = NMath::Min( TileY + TileHeight, static_cast<float32>( Data.Y1 ) );
				if ( X1 <= X0 || Y1 <= Y0 )
				{
					continue;
				}
				const float32 UV[4] = { ( X0 - TileX ) / TileWidth, ( Y0 - TileY ) / TileHeight, ( X1 - TileX ) / TileWidth, ( Y1 - TileY ) / TileHeight }; // 切った範囲に当たる画像の範囲。
				const float32 Points[8] = { X0, Y0, X1, Y0, X1, Y1, X0, Y1 };
				DrawImage( Image, Points, UV );
			}
		}
		break;
	}
	case EDrawCommand::CircleGauge:
	{
		if ( !View.CanRead<FDrawCircleGaugeData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawCircleGaugeData>(); // 固定部を所有コピーする。
		FTextureDrawView Image; // 所有者から借りた描画用画像。
		if ( !GetImage( Data.Handle, Image ) || !Valid( Data.X ) || !Valid( Data.Y ) || !Valid( Data.Percent ) || !Valid( Data.StartPercent ) || !Valid( Data.Scale ) || Data.Scale <= 0 || Data.Percent <= 0.0f )
		{
			break;
		}
		if ( View.Has( FDrawFlag::FlipX ) || View.Has( FDrawFlag::FlipY ) )
		{
			Unsupported( View, "A flipped circle gauge is skipped" );
			break;
		}
		const float64 Half[2] = { Image.Width * static_cast<float64>( Data.Scale ) * 0.5, Image.Height * static_cast<float64>( Data.Scale ) * 0.5 }; // 画像の半分の幅と高さ（拡大後）。
		// 真上から時計回りの割合 From〜To（0〜1）の扇を、画像の縁までの多角形にして貼る。
		const auto DrawSector = [&]( float64 From, float64 To )
		{
			if ( To <= From )
			{
				return;
			}
			constexpr float64 TwoPi = 6.283185307179586;
			TArray<float32> Vertices; // x, y, u, v の頂点列。
			// 中心から、真上を0として時計回りに Angle だけ進んだ向きが、画像の縁に当たる点。
			const auto AddVertex = [&]( float64 OffsetX, float64 OffsetY )
			{
				Vertices.Add( static_cast<float32>( Data.X + OffsetX ) );
				Vertices.Add( static_cast<float32>( Data.Y + OffsetY ) );
				Vertices.Add( static_cast<float32>( 0.5 + OffsetX / ( Half[0] * 2.0 ) ) );
				Vertices.Add( static_cast<float32>( 0.5 + OffsetY / ( Half[1] * 2.0 ) ) );
			};
			const auto AddEdge = [&]( float64 Fraction )
			{
				const float64 Angle = Fraction * TwoPi; // 真上から時計回りの角度。
				const float64 DirectionX = NMath::Sin( Angle ), DirectionY = -NMath::Cos( Angle ); // 画面の向き（Yは下が正）。
				float64 Length = 1.0e30;
				if ( NMath::Abs( DirectionX ) > 1.0e-12 )
				{
					Length = NMath::Min( Length, Half[0] / NMath::Abs( DirectionX ) );
				}
				if ( NMath::Abs( DirectionY ) > 1.0e-12 )
				{
					Length = NMath::Min( Length, Half[1] / NMath::Abs( DirectionY ) );
				}
				AddVertex( DirectionX * Length, DirectionY * Length );
			};
			AddVertex( 0.0, 0.0 );
			AddEdge( From );
			// 間にある、画像の4つの角（右上・右下・左下・左上の順）。
			const float64 CornerAngle = NMath::Atan2( Half[0], Half[1] ); // 真上から右上の角までの角度。
			const float64 Corners[4][3] = { { CornerAngle, Half[0], -Half[1] }, { 3.141592653589793 - CornerAngle, Half[0], Half[1] }, { 3.141592653589793 + CornerAngle, -Half[0], Half[1] }, { TwoPi - CornerAngle, -Half[0], -Half[1] } };
			for ( const auto& Corner : Corners )
			{
				if ( Corner[0] > From * TwoPi && Corner[0] < To * TwoPi )
				{
					AddVertex( Corner[1], Corner[2] );
				}
			}
			AddEdge( To );
			DrawImagePolygon( Image, Vertices );
		};
		// DxLibと同じ解釈: 描く量が100以上なら全体、描き終わりが100を超えるなら、先頭へ回り込む。
		const float64 Start = NMath::Clamp( static_cast<float64>( Data.StartPercent ), 0.0, 100.0 ), End = Start + static_cast<float64>( Data.Percent );
		if ( Data.Percent >= 100.0f )
		{
			DrawSector( 0.0, 1.0 ); // 全体は、描き始めの位置に関係ない（1回で描く）。
		}
		else if ( End <= 100.0 )
		{
			DrawSector( Start / 100.0, End / 100.0 );
		}
		else
		{
			DrawSector( Start / 100.0, 1.0 );
			DrawSector( 0.0, ( End - 100.0 ) / 100.0 );
		}
		break;
	}
	default:
		return false;
	}
	return true;
}
