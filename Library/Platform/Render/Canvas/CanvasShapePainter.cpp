// SPDX-License-Identifier: MIT
#include "CanvasShapePainter.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using NCanvasValidation::Valid;
	using NCanvasValidation::ValidPoints;
	using NCanvasValidation::GetSegments;
}

CCanvasShapePainter::CCanvasShapePainter( FCanvasExecution& InRun )
	: Run( InRun )
	, Canvas( Run.Canvas )
	, bStopped( Run.bStopped )
	, Transform( Run.State.Transform )
{
}

FColor32 CCanvasShapePainter::ShapeColor( FDrawRgb Value )
{
	return Run.State.MakeShapeColor( Value );
}

// 凸な点列（XYの順）を、変換してから描く。
void CCanvasShapePainter::DrawPolygon( const float32* Points, int32 Count, FColor32 Value, bool bFill, float32 Thickness )
{
	if ( Count < 3 || Count > 512 || !ValidPoints( Points, Count * 2 ) || !Valid( Thickness ) || Thickness <= 0 )
	{
		return;
	}
	TArray<float32> Moved; // 変換後の点列。
	Moved.SetNum( Count * 2 );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		float32 X = Points[Index * 2], Y = Points[Index * 2 + 1]; // 変換する点。
		Transform.Apply( X, Y );
		Moved[Index * 2] = X;
		Moved[Index * 2 + 1] = Y;
	}
	if ( !ValidPoints( Moved.GetData(), Count * 2 ) )
	{
		return;
	}
	const float32 Width = Thickness * Transform.GetScale(); // 変換の拡大を、線の太さにも掛ける。
	if ( Count == 3 )
	{
		const float32 Three[6] = { Moved[0], Moved[1], Moved[2], Moved[3], Moved[4], Moved[5] };
		Canvas->Triangle( Three, Value, bFill, Width );
	}
	else if ( Count == 4 )
	{
		const float32 Four[8] = { Moved[0], Moved[1], Moved[2], Moved[3], Moved[4], Moved[5], Moved[6], Moved[7] };
		Canvas->Quad( Four, Value, bFill, Width );
	}
	else
	{
		Canvas->Polygon( Moved.GetData(), Count, Value, bFill, Width );
	}
}

// 楕円（半径が同じなら円）を、点列にして描く。
void CCanvasShapePainter::DrawEllipse( float32 X, float32 Y, float32 Rx, float32 Ry, FColor32 Value, bool bFill, float32 Thickness, int32 Segments )
{
	if ( !Valid( X ) || !Valid( Y ) || !Valid( Rx ) || !Valid( Ry ) || Rx <= 0 || Ry <= 0 )
	{
		return;
	}
	const int32 Count = GetSegments( NMath::Max( Rx, Ry ), Segments ); // 大きいほうの半径で分割を決める。
	TArray<float32> Points; // 輪郭は閉じる点を重複させない。
	Points.SetNum( Count * 2 );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const float32 Angle = Index * 6.28318530718f / Count; // 一周を等分する角度。
		Points[Index * 2] = X + Rx * NMath::Cos( Angle );
		Points[Index * 2 + 1] = Y + Ry * NMath::Sin( Angle );
	}
	DrawPolygon( Points.GetData(), Count, Value, bFill, Thickness );
}

bool CCanvasShapePainter::Execute( const FDrawCommandView& View )
{
	const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしか輪郭か。
	switch ( View.Type )
	{
	case EDrawCommand::Box:
	case EDrawCommand::BoxAA:
	{
		float32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
		FDrawRgb Value; // 図形の色。
		if ( View.Type == EDrawCommand::Box )
		{
			if ( !View.CanRead<FDrawBoxData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBoxData>(); // 固定部を所有コピーする。
			X0 = static_cast<float32>( Data.X0 );
			Y0 = static_cast<float32>( Data.Y0 );
			X1 = static_cast<float32>( Data.X1 );
			Y1 = static_cast<float32>( Data.Y1 );
			Thickness = static_cast<float32>( Data.Thickness );
			Value = Data.Color;
		}
		else
		{
			if ( !View.CanRead<FDrawBoxAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBoxAAData>(); // 固定部を所有コピーする。
			X0 = Data.X0;
			Y0 = Data.Y0;
			X1 = Data.X1;
			Y1 = Data.Y1;
			Thickness = Data.Thickness;
			Value = Data.Color;
		}
		if ( !Valid( X0 ) || !Valid( Y0 ) || !Valid( X1 ) || !Valid( Y1 ) || !Valid( Thickness ) || Thickness <= 0 )
		{
			break;
		}
		if ( !bFill && ( X0 == X1 || Y0 == Y1 ) )
		{
			// 幅か高さが0の輪郭は、線として描く（DxLibの DrawBox と同じ。格子の線などに使われる）。
			float32 LineX0 = X0, LineY0 = Y0, LineX1 = X1, LineY1 = Y1; // 変換する端点。
			Transform.Apply( LineX0, LineY0 );
			Transform.Apply( LineX1, LineY1 );
			if ( Valid( LineX0 ) && Valid( LineY0 ) && Valid( LineX1 ) && Valid( LineY1 ) )
			{
				Canvas->Line( LineX0, LineY0, LineX1, LineY1, ShapeColor( Value ), Thickness * Transform.GetScale() );
			}
		}
		else
		{
			float32 DrawX0 = X0, DrawY0 = Y0, DrawX1 = X1, DrawY1 = Y1; // 描く矩形。
			bool bDrawFill = bFill;
			if ( !bFill && View.Type == EDrawCommand::Box )
			{
				// DxLib の DrawBox は、太さのある輪郭を、矩形の内側（左・上は X0・Y0 から、右・下は X1・Y1 の手前まで）へ描く。辺の座標を中心にはしない。
				// 帯の中心線を、辺から半分の太さだけ内側へ寄せる。太さが矩形に収まらないときは、矩形の全体が塗られる。
				const float32 Left = NMath::Min( X0, X1 ), Right = NMath::Max( X0, X1 ), Top = NMath::Min( Y0, Y1 ), Bottom = NMath::Max( Y0, Y1 ); // 向きによらない矩形。
				if ( Right - Left <= Thickness || Bottom - Top <= Thickness )
				{
					DrawX0 = Left;
					DrawY0 = Top;
					DrawX1 = Right;
					DrawY1 = Bottom;
					bDrawFill = true;
				}
				else
				{
					const float32 Half = Thickness * 0.5f; // 帯の中心線までの寄せ幅。
					DrawX0 = Left + Half;
					DrawY0 = Top + Half;
					DrawX1 = Right - Half;
					DrawY1 = Bottom - Half;
				}
			}
			if ( Transform.bIdentity )
			{
				Canvas->Box( DrawX0, DrawY0, DrawX1, DrawY1, ShapeColor( Value ), bDrawFill, Thickness );
			}
			else
			{
				const float32 Points[8] = { DrawX0, DrawY0, DrawX1, DrawY0, DrawX1, DrawY1, DrawX0, DrawY1 }; // 変換すると、矩形でなくなる。
				DrawPolygon( Points, 4, ShapeColor( Value ), bDrawFill, Thickness );
			}
		}
		break;
	}
	case EDrawCommand::Line:
	case EDrawCommand::LineAA:
	{
		float32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
		FDrawRgb Value; // 線の色。
		if ( View.Type == EDrawCommand::Line )
		{
			if ( !View.CanRead<FDrawLineData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawLineData>(); // 固定部を所有コピーする。
			X0 = static_cast<float32>( Data.X0 );
			Y0 = static_cast<float32>( Data.Y0 );
			X1 = static_cast<float32>( Data.X1 );
			Y1 = static_cast<float32>( Data.Y1 );
			Thickness = static_cast<float32>( Data.Thickness );
			Value = Data.Color;
		}
		else
		{
			if ( !View.CanRead<FDrawLineAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawLineAAData>(); // 固定部を所有コピーする。
			X0 = Data.X0;
			Y0 = Data.Y0;
			X1 = Data.X1;
			Y1 = Data.Y1;
			Thickness = Data.Thickness;
			Value = Data.Color;
		}
		if ( !Valid( X0 ) || !Valid( Y0 ) || !Valid( X1 ) || !Valid( Y1 ) || !Valid( Thickness ) || Thickness <= 0 )
		{
			break;
		}
		Transform.Apply( X0, Y0 );
		Transform.Apply( X1, Y1 );
		if ( Valid( X0 ) && Valid( Y0 ) && Valid( X1 ) && Valid( Y1 ) )
		{
			Canvas->Line( X0, Y0, X1, Y1, ShapeColor( Value ), Thickness * Transform.GetScale() );
		}
		break;
	}
	case EDrawCommand::Circle:
	case EDrawCommand::CircleAA:
	{
		float32 X = 0, Y = 0, Radius = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
		int32 Segments = 0; // 0 は自動。
		FDrawRgb Value; // 円の色。
		if ( View.Type == EDrawCommand::Circle )
		{
			if ( !View.CanRead<FDrawCircleData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCircleData>(); // 固定部を所有コピーする。
			X = static_cast<float32>( Data.X );
			Y = static_cast<float32>( Data.Y );
			Radius = static_cast<float32>( Data.Radius );
			Thickness = static_cast<float32>( Data.Thickness );
			Value = Data.Color;
		}
		else
		{
			if ( !View.CanRead<FDrawCircleAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCircleAAData>(); // 固定部を所有コピーする。
			X = Data.X;
			Y = Data.Y;
			Radius = Data.Radius;
			Thickness = Data.Thickness;
			Segments = NMath::Clamp( Data.NumSegments, 0, 4096 );
			Value = Data.Color;
		}
		if ( !Valid( X ) || !Valid( Y ) || !Valid( Radius ) || Radius <= 0 || !Valid( Thickness ) || Thickness <= 0 )
		{
			break;
		}
		if ( Transform.bIdentity )
		{
			Canvas->Circle( X, Y, Radius, ShapeColor( Value ), bFill, Thickness, Segments );
		}
		else
		{
			DrawEllipse( X, Y, Radius, Radius, ShapeColor( Value ), bFill, Thickness, Segments );
		}
		break;
	}
	case EDrawCommand::Oval:
	{
		if ( !View.CanRead<FDrawOvalData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawOvalData>(); // 固定部を所有コピーする。
		DrawEllipse( Data.X, Data.Y, Data.Rx, Data.Ry, ShapeColor( Data.Color ), bFill, Data.Thickness, NMath::Clamp( Data.NumSegments, 0, 4096 ) );
		break;
	}
	case EDrawCommand::RoundBox:
	{
		if ( !View.CanRead<FDrawRoundBoxData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawRoundBoxData>(); // 固定部を所有コピーする。
		const float32 Raw[6] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.Rx, Data.Ry }; // まとめて検査する値。
		if ( !ValidPoints( Raw, 6 ) )
		{
			break;
		}
		const float32 Left = NMath::Min( Data.X0, Data.X1 ), Right = NMath::Max( Data.X0, Data.X1 ), Top = NMath::Min( Data.Y0, Data.Y1 ), Bottom = NMath::Max( Data.Y0, Data.Y1 ); // 向きをそろえた四辺。
		const float32 Rx = NMath::Clamp( Data.Rx, 0.0f, ( Right - Left ) * 0.5f ), Ry = NMath::Clamp( Data.Ry, 0.0f, ( Bottom - Top ) * 0.5f ); // 丸みは、辺の半分まで。
		if ( Right - Left <= 0 || Bottom - Top <= 0 )
		{
			break;
		}
		if ( Rx <= 0 || Ry <= 0 )
		{
			const float32 Points[8] = { Left, Top, Right, Top, Right, Bottom, Left, Bottom }; // 丸みなしは、矩形。
			DrawPolygon( Points, 4, ShapeColor( Data.Color ), bFill, Data.Thickness );
			break;
		}
		const int32 PerCorner = NMath::Clamp( GetSegments( NMath::Max( Rx, Ry ), NMath::Clamp( Data.NumSegments, 0, 4096 ) ) / 4, 2, 120 ); // 角 1 つの分割。
		const float32 CenterX[4] = { Right - Rx, Left + Rx, Left + Rx, Right - Rx }, CenterY[4] = { Bottom - Ry, Bottom - Ry, Top + Ry, Top + Ry }; // 右下・左下・左上・右上の、丸みの中心。
		TArray<float32> Points; // 角ごとに、4分の1の円弧を並べる。
		for ( int32 Corner = 0; Corner < 4; ++Corner )
		{
			for ( int32 Index = 0; Index <= PerCorner; ++Index )
			{
				const float32 Angle = ( Corner + static_cast<float32>( Index ) / PerCorner ) * 1.57079632679f; // 角ごとに 90 度ずつ進む。
				Points.Add( CenterX[Corner] + Rx * NMath::Cos( Angle ) );
				Points.Add( CenterY[Corner] + Ry * NMath::Sin( Angle ) );
			}
		}
		DrawPolygon( Points.GetData(), Points.Num() / 2, ShapeColor( Data.Color ), bFill, Data.Thickness );
		break;
	}
	case EDrawCommand::Pixel:
	{
		if ( !View.CanRead<FDrawPixelData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawPixelData>(); // 固定部を所有コピーする。
		const float32 X = static_cast<float32>( Data.X ), Y = static_cast<float32>( Data.Y ); // 1画素の矩形の左上。
		const float32 Points[8] = { X, Y, X + 1, Y, X + 1, Y + 1, X, Y + 1 }; // 1画素の矩形。
		DrawPolygon( Points, 4, ShapeColor( Data.Color ), true, 1 );
		break;
	}
	case EDrawCommand::Triangle:
	{
		if ( !View.CanRead<FDrawTriangleData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawTriangleData>(); // 固定部を所有コピーする。
		const float32 Points[6] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2 }; // XYの並び。
		DrawPolygon( Points, 3, ShapeColor( Data.Color ), bFill, Data.Thickness );
		break;
	}
	case EDrawCommand::Quad:
	{
		if ( !View.CanRead<FDrawQuadData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawQuadData>(); // 固定部を所有コピーする。
		const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2, Data.X3, Data.Y3 }; // XYの並び。
		DrawPolygon( Points, 4, ShapeColor( Data.Color ), bFill, Data.Thickness );
		break;
	}
	default:
		return false;
	}
	return true;
}
