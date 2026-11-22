#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"


void FDrawContextCore::BeginScene3D()
{
	Buffer->Words.Add( NDrawCommand::MakeHeader( EDrawCommand::BeginScene3D, 0, 1 ) );
}

void FDrawContextCore::EndScene3D()
{
	Buffer->Words.Add( NDrawCommand::MakeHeader( EDrawCommand::EndScene3D, 0, 1 ) );
}

bool FDrawContextCore::SceneDrawKey3D( ESceneDrawClass Class, float32 Depth )
{
	if ( Class > ESceneDrawClass::Translucent || !NMath::IsFinite( Depth ) ) { return false; }
	FDrawSceneKeyData Data; // 直後の描画と一緒に並べ替える値。
	Data.Class = Class;
	Data.Depth = Depth;
	Push( EDrawCommand::SceneDrawKey3D, 0, Data );
	return true;
}

bool FDrawContextCore::PushImageFilter( FImageHandle Source, FImageHandle Dest, const FGraphFilter& Filter, bool bAlphaBlend, bool bRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY )
{
	if ( !Source.IsValid() || !Dest.IsValid() || !Filter.IsValid() )
	{
		return false;
	}
	if ( bRect && ( !IsValidSource( SrcX, SrcY, SrcW, SrcH ) || !AllDrawable( static_cast<float32>( DestX ), static_cast<float32>( DestY ) ) ) )
	{
		return false;
	}
	FDrawImageFilterData Data;
	Data.Source = Source.Value;
	Data.Dest = Dest.Value;
	Data.Type = static_cast<int32>( Filter.Type );
	Data.Premultiplied = Filter.bPremultiplied ? 1 : 0;
	Data.Blend = bAlphaBlend ? 1 : 0;
	Data.HasRect = bRect ? 1 : 0;
	Data.SrcX = SrcX;
	Data.SrcY = SrcY;
	Data.SrcW = SrcW;
	Data.SrcH = SrcH;
	Data.DestX = DestX;
	Data.DestY = DestY;
	Data.Map = Filter.Map.Value;
	for ( int32 Index = 0; Index < 8; ++Index )
	{
		Data.Params[Index] = Filter.Params[Index];
	}
	Push( EDrawCommand::ImageFilter, 0, Data );
	return true;
}

void FDrawContextCore::PushTriangle( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags )
{
	if ( !AllDrawable( X0, Y0, X1, Y1, X2, Y2 ) || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawTriangleData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.X2 = X2;
	Data.Y2 = Y2;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Triangle, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
}

void FDrawContextCore::PushQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags )
{
	if ( !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawQuadData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.X2 = X2;
	Data.Y2 = Y2;
	Data.X3 = X3;
	Data.Y3 = Y3;
	Data.Thickness = Thickness;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Quad, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
}

void FDrawContextCore::PushOval( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags )
{
	if ( !AllDrawable( X, Y, Rx, Ry ) || Rx < 0.0f || Ry < 0.0f || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawOvalData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Rx = Rx;
	Data.Ry = Ry;
	Data.Thickness = Thickness;
	Data.NumSegments = NumSegments;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Oval, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
}

void FDrawContextCore::PushRoundBox( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags )
{
	if ( !AllDrawable( X0, Y0, X1, Y1, Rx, Ry ) || Rx < 0.0f || Ry < 0.0f || !IsPositiveThickness( Thickness ) )
	{
		return;
	}
	FDrawRoundBoxData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Rx = Rx;
	Data.Ry = Ry;
	Data.Thickness = Thickness;
	Data.NumSegments = NumSegments;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::RoundBox, static_cast<uint8>( Flags | ( bFill ? FDrawFlag::Fill : 0 ) ), Data );
}

void FDrawContextCore::Set3( float32 ( &Out )[3], const FVector3& V )
{
	Out[0] = V.x;
	Out[1] = V.y;
	Out[2] = V.z;
}

void FDrawContextCore::PackAffine( float32 ( &Out )[12], const FMatrix4x4& Matrix )
{
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 3; ++Column )
		{
			Out[Row * 3 + Column] = Matrix.m[Row][Column];
		}
	}
}
