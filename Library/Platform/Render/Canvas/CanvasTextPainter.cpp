// SPDX-License-Identifier: MIT
#include "CanvasTextPainter.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using NCanvasValidation::Valid;
	using NCanvasValidation::ValidPoints;
}

CCanvasTextPainter::CCanvasTextPainter( FCanvasExecution& InRun, CCanvasImagePainter& InImages )
	: Run( InRun )
	, Images( InImages )
	, Fonts( Run.Fonts )
	, Canvas( Run.Canvas )
	, bStopped( Run.bStopped )
	, Transform( Run.State.Transform )
{
}

void CCanvasTextPainter::Unsupported( const FDrawCommandView& View, const char* Reason )
{
	FCanvasExecution::Unsupported( View, Reason );
}

FColor32 CCanvasTextPainter::Color( FDrawRgb Value )
{
	return Run.State.MakeColor( Value );
}

bool CCanvasTextPainter::GetImage( int32 Handle, FTextureDrawView& Image )
{
	return Run.GetImage( Handle, Image );
}

bool CCanvasTextPainter::Execute( const FDrawCommandView& View )
{
	switch ( View.Type )
	{
	case EDrawCommand::Text:
	case EDrawCommand::TextFont:
	case EDrawCommand::TextStyled:
	{
		float32 X = 0, Y = 0; // 文字列の位置（Text / TextFont は左上。TextStyled は基準点）。
		float32 ScaleX = 1, ScaleY = 1, Angle = 0, AnchorX = 0, AnchorY = 0; // TextStyled の拡大・回転・基準点。
		int32 Length = 0, Capacity = 0, Font = -1; // 文字数、命令に含まれる文字の範囲、フォントの番号（負は既定）。
		FDrawRgb Value, Edge; // 文字と縁取りの色。
		bool bVertical = false; // 縦書きか（TextStyled だけ）。
		const char* Text = nullptr; // 命令の中の文字列。
		if ( View.Type == EDrawCommand::Text )
		{
			if ( !View.CanRead<FDrawTextData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTextData>(); // 固定部を所有コピーする。
			X = static_cast<float32>( Data.X );
			Y = static_cast<float32>( Data.Y );
			Length = Data.Length;
			Value = Data.Color;
			Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
			Text = View.GetTextChars();
		}
		else if ( View.Type == EDrawCommand::TextFont )
		{
			if ( !View.CanRead<FDrawTextFontData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTextFontData>(); // 固定部を所有コピーする。
			X = static_cast<float32>( Data.X );
			Y = static_cast<float32>( Data.Y );
			Length = Data.Length;
			Value = Data.Color;
			Edge = Data.EdgeColor;
			Font = Data.Font;
			Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
			Text = View.GetFontTextChars();
		}
		else
		{
			if ( !View.CanRead<FDrawTextStyledData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTextStyledData>(); // 固定部を所有コピーする。
			X = Data.X;
			Y = Data.Y;
			ScaleX = Data.ScaleX;
			ScaleY = Data.ScaleY;
			Angle = Data.Angle;
			AnchorX = Data.AnchorX;
			AnchorY = Data.AnchorY;
			Length = Data.Length;
			Value = Data.Color;
			Edge = Data.EdgeColor;
			Font = Data.Font;
			Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
			Text = View.GetStyledTextChars();
			if ( Data.Vertical != 0 )
			{
				bVertical = true;
			}
		}
		if ( Length <= 0 || Length >= Capacity || Text[Length] != '\0' )
		{
			break;
		}
		const float32 Raw[7] = { X, Y, ScaleX, ScaleY, Angle, AnchorX, AnchorY }; // まとめて検査する値。
		if ( !ValidPoints( Raw, 7 ) )
		{
			break;
		}
		FRenderTextFont FontValue; // 文字を画像にするための、フォントの値。
		FTextSize Size; // 位置揃えと基準点に使う、文字列の大きさ。
		if ( !Fonts || !Fonts->GetFontDrawView( Font, FontValue ) || !Fonts->MeasureFontText( Font, Text, Length, Size ) )
		{
			// フォントの表が無い実行先・無効なフォントは、既定の文字で、位置だけを合わせて描く。
			Unsupported( View, "Fonts are not available; the default text is used" );
			Transform.Apply( X, Y );
			if ( Valid( X ) && Valid( Y ) )
			{
				Canvas->Text( X, Y, Color( Value ), Text, Text + Length );
			}
			break;
		}
		if ( bVertical )
		{
			// DxLib の縦書きは、横書きを、時計回りに 90 度回した見た目になる（文字も横倒しになる。1 行目が右の列で、行が進むと、左の列）。
			// 基準点は、縦書きの割合（AnchorY が、読む向き（下）、AnchorX が、列の並ぶ向き（左））を、回す前の横書きの割合に直す。拡大率は、画面の横が ScaleX、縦が ScaleY のまま。
			const float32 Across = AnchorX;
			AnchorX = AnchorY;
			AnchorY = Across;
			const float32 Along = ScaleX;
			ScaleX = ScaleY;
			ScaleY = Along;
			Angle += 1.57079632679f;
		}
		if ( View.Type == EDrawCommand::TextFont && ( View.Has( FDrawFlag::AlignCenter ) || View.Has( FDrawFlag::AlignRight ) ) )
		{
			X -= static_cast<float32>( View.Has( FDrawFlag::AlignCenter ) ? Size.Width / 2 : Size.Width ); // X が、文字列の中央 / 右端。
		}
		// 文字の画像の中の点を、基準点まわりに拡大・回転して (X, Y) へ動かし、最後に 2D の変換を掛ける。
		const float32 Cos = NMath::Cos( Angle ), Sin = NMath::Sin( Angle ); // 画面（Y が下）での回転。正は時計回り。
		const float32 PivotX = AnchorX * Size.Width, PivotY = AnchorY * Size.Height; // 基準点（文字列の中の位置）。
		const float32 A0 = ScaleX * Cos, A1 = ScaleX * Sin, A2 = -ScaleY * Sin, A3 = ScaleY * Cos; // 拡大と回転。
		const float32 A4 = X - PivotX * A0 - PivotY * A2, A5 = Y - PivotX * A1 - PivotY * A3; // 基準点が (X, Y) に来る移動。
		const float32 Matrix[6] = { A0 * Transform.XX + A1 * Transform.YX, A0 * Transform.XY + A1 * Transform.YY, A2 * Transform.XX + A3 * Transform.YX, A2 * Transform.XY + A3 * Transform.YY,
			A4 * Transform.XX + A5 * Transform.YX + Transform.TX, A4 * Transform.XY + A5 * Transform.YY + Transform.TY };
		const FColor32 EdgeColor = Color( Edge ); // 縁取りにも、明るさと不透明度を掛ける。
		Canvas->TextFont( FontValue, Matrix, Color( Value ), EdgeColor, Text, Text + Length );
		if ( !FontValue.CharImages.IsEmpty() ) // 文字の代わりに入れる画像（文字と同じ変換で置く）。
		{
			TArray<FFontImagePlacement> Placements;
			if ( Fonts->GetFontImagePlacements( Font, Text, Length, Placements ) )
			{
				for ( const FFontImagePlacement& Placement : Placements )
				{
					FTextureDrawView Icon;
					if ( !GetImage( Placement.Image, Icon ) )
					{
						continue;
					}
					const float32 Left = static_cast<float32>( Placement.X ), Top = static_cast<float32>( Placement.Y ), Right = Left + static_cast<float32>( Icon.Width ), Bottom = Top + static_cast<float32>( Icon.Height );
					const float32 Local[8] = { Left, Top, Right, Top, Right, Bottom, Left, Bottom };
					float32 Points[8]; // 文字の中の点を、拡大・回転・移動した 4 点（2D の変換は、DrawImage が掛ける）。
					for ( int32 Index = 0; Index < 4; ++Index )
					{
						Points[Index * 2] = Local[Index * 2] * A0 + Local[Index * 2 + 1] * A2 + A4;
						Points[Index * 2 + 1] = Local[Index * 2] * A1 + Local[Index * 2 + 1] * A3 + A5;
					}
					const float32 FullUV[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
					Images.DrawImage( Icon, Points, FullUV );
				}
			}
		}
		break;
	}
	default:
		return false;
	}
	return true;
}
