#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

void FDrawContext2DImages::Image( float32 X, float32 Y, FImageHandle Handle, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !AllDrawable( X, Y ) )
	{
		return;
	}
	FDrawImageData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::Image, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DImages::ImageStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, FImageHandle Handle )
{
	if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1 ) )
	{
		return;
	}
	FDrawImageStretchData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::ImageStretch, 0, Data );
}

void FDrawContext2DImages::ImageQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, FImageHandle Handle )
{
	if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) )
	{
		return;
	}
	FDrawImageQuadData Data;
	Data.X[0] = X0;
	Data.Y[0] = Y0;
	Data.X[1] = X1;
	Data.Y[1] = Y1;
	Data.X[2] = X2;
	Data.Y[2] = Y2;
	Data.X[3] = X3;
	Data.Y[3] = Y3;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::ImageQuad, 0, Data );
}

void FDrawContext2DImages::ImagePart( float32 X, float32 Y, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !AllDrawable( X, Y ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
	{
		return;
	}
	FDrawImagePartData Data;
	Data.X = X;
	Data.Y = Y;
	Data.SrcX = SrcX;
	Data.SrcY = SrcY;
	Data.SrcW = SrcW;
	Data.SrcH = SrcH;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::ImagePart, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DImages::ImagePartStretch( float32 X0, float32 Y0, float32 X1, float32 Y1, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle )
{
	if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1 ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
	{
		return;
	}
	FDrawImagePartStretchData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.SrcX = SrcX;
	Data.SrcY = SrcY;
	Data.SrcW = SrcW;
	Data.SrcH = SrcH;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::ImagePartStretch, 0, Data );
}

void FDrawContext2DImages::ImagePartQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Handle )
{
	if ( !Handle.IsValid() || !AllDrawable( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) || !IsValidSource( SrcX, SrcY, SrcW, SrcH ) )
	{
		return;
	}
	FDrawImagePartQuadData Data;
	Data.X[0] = X0;
	Data.Y[0] = Y0;
	Data.X[1] = X1;
	Data.Y[1] = Y1;
	Data.X[2] = X2;
	Data.Y[2] = Y2;
	Data.X[3] = X3;
	Data.Y[3] = Y3;
	Data.SrcX = SrcX;
	Data.SrcY = SrcY;
	Data.SrcW = SrcW;
	Data.SrcH = SrcH;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::ImagePartQuad, 0, Data );
}

void FDrawContext2DImages::SpritePivot( float32 X, float32 Y, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !AllDrawable( X, Y, PivotX, PivotY, ScaleX, ScaleY, AngleRadians ) )
	{
		return;
	}
	FDrawSpritePivotData Data;
	Data.X = X;
	Data.Y = Y;
	Data.PivotX = PivotX;
	Data.PivotY = PivotY;
	Data.ScaleX = ScaleX;
	Data.ScaleY = ScaleY;
	Data.Angle = AngleRadians;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::SpritePivot, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DImages::Tile( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Handle, int32 OffsetX, int32 OffsetY, float32 Scale, float32 AngleRadians )
{
	if ( !Handle.IsValid() || Scale <= 0.0f || !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ), static_cast<float32>( OffsetX ), static_cast<float32>( OffsetY ), Scale, AngleRadians ) )
	{
		return;
	}
	FDrawTileData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.OffsetX = OffsetX;
	Data.OffsetY = OffsetY;
	Data.Scale = Scale;
	Data.Angle = AngleRadians;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::Tile, 0, Data );
}

void FDrawContext2DImages::CircleGauge( float32 X, float32 Y, float32 Fraction, FImageHandle Handle, float32 StartFraction, float32 Scale, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || Scale <= 0.0f || !AllDrawable( X, Y, Fraction, StartFraction, Scale ) )
	{
		return;
	}
	FDrawCircleGaugeData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Percent = NMath::Clamp( Fraction, 0.0f, 1.0f ) * 100.0f;
	Data.StartPercent = NMath::Clamp( StartFraction, 0.0f, 1.0f ) * 100.0f;
	Data.Scale = Scale;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::CircleGauge, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext2DImages::Tint( const FColor32& Color )
{
	FDrawBrightData Data;
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Bright, 0, Data );
}

void FDrawContext2DImages::Blend( EDrawBlend Mode, int32 Param )
{
	if ( Mode == EDrawBlend::Custom || !IsInEnumRange( static_cast<int32>( Mode ), static_cast<int32>( EDrawBlend::PmaInvert ) ) )
	{
		return;
	}
	FDrawBlendData Data;
	Data.Mode = static_cast<int32>( Mode );
	Data.Param = NMath::Clamp( Param, 0, 255 );
	Push( EDrawCommand::Blend, 0, Data );
	CurrentBlendMode = Mode;
	CurrentBlendParam = Data.Param;
}

void FDrawContext2DImages::CustomBlend( const FCustomBlend& Settings )
{
	const int32 MaxFactor = static_cast<int32>( EDrawBlendFactor::SrcAlphaSat );
	const int32 MaxOp = static_cast<int32>( EDrawBlendOp::Max );
	if ( !IsInEnumRange( static_cast<int32>( Settings.SrcColor ), MaxFactor ) || !IsInEnumRange( static_cast<int32>( Settings.DestColor ), MaxFactor )
		 || !IsInEnumRange( static_cast<int32>( Settings.SrcAlpha ), MaxFactor ) || !IsInEnumRange( static_cast<int32>( Settings.DestAlpha ), MaxFactor )
		 || !IsInEnumRange( static_cast<int32>( Settings.ColorOp ), MaxOp ) || !IsInEnumRange( static_cast<int32>( Settings.AlphaOp ), MaxOp ) )
	{
		return;
	}
	FDrawCustomBlendData Data;
	Data.SrcColor = static_cast<int32>( Settings.SrcColor );
	Data.DestColor = static_cast<int32>( Settings.DestColor );
	Data.ColorOp = static_cast<int32>( Settings.ColorOp );
	Data.SrcAlpha = static_cast<int32>( Settings.SrcAlpha );
	Data.DestAlpha = static_cast<int32>( Settings.DestAlpha );
	Data.AlphaOp = static_cast<int32>( Settings.AlphaOp );
	Data.Param = NMath::Clamp( Settings.Param, 0, 255 );
	Push( EDrawCommand::CustomBlend, 0, Data );
	CurrentBlendMode = EDrawBlend::Custom;
	CurrentBlendParam = Data.Param;
	CurrentCustomBlend = Settings;
	CurrentCustomBlend.Param = Data.Param;
}

EDrawBlend FDrawContext2DImages::GetBlendMode() const
{
	return CurrentBlendMode;
}

int32 FDrawContext2DImages::GetBlendParam() const
{
	return CurrentBlendParam;
}

FBlendState FDrawContext2DImages::GetBlendState() const
{
	FBlendState State;
	State.Mode = CurrentBlendMode;
	State.Param = CurrentBlendParam;
	State.Custom = CurrentCustomBlend;
	return State;
}

void FDrawContext2DImages::SetBlendState( const FBlendState& State )
{
	if ( State.Mode == EDrawBlend::Custom )
	{
		CustomBlend( State.Custom );
	}
	else
	{
		Blend( State.Mode, State.Param );
	}
}

void FDrawContext2DImages::AddColor( int32 R, int32 G, int32 B )
{
	FDrawAddColorData Data;
	Data.R = NMath::Clamp( R, -255, 255 );
	Data.G = NMath::Clamp( G, -255, 255 );
	Data.B = NMath::Clamp( B, -255, 255 );
	Push( EDrawCommand::AddColor, 0, Data );
}

void FDrawContext2DImages::AlphaTest( EDrawCompare Compare, int32 Reference )
{
	if ( !IsInEnumRange( static_cast<int32>( Compare ), static_cast<int32>( EDrawCompare::Always ) ) )
	{
		return;
	}
	FDrawAlphaTestData Data;
	Data.Enabled = 1;
	Data.Mode = static_cast<int32>( Compare );
	Data.Reference = NMath::Clamp( Reference, 0, 255 );
	Push( EDrawCommand::AlphaTest, 0, Data );
}

void FDrawContext2DImages::AlphaTestOff()
{
	const FDrawAlphaTestData Data;
	Push( EDrawCommand::AlphaTest, 0, Data );
}

void FDrawContext2DImages::Filter( EDrawFilter Mode )
{
	if ( !IsInEnumRange( static_cast<int32>( Mode ), static_cast<int32>( EDrawFilter::Anisotropic ) ) )
	{
		return;
	}
	FDrawFilterData Data;
	Data.Mode = static_cast<int32>( Mode );
	Push( EDrawCommand::Filter, 0, Data );
}

bool FDrawContext2DImages::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	if ( !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ) ) )
	{
		return false;
	}
	FDrawClipData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Push( EDrawCommand::PushClip, 0, Data );
	return true;
}

void FDrawContext2DImages::PopClip()
{
	const FDrawPopClipData Data;
	Push( EDrawCommand::PopClip, 0, Data );
}

void FDrawContext2DImages::Transform2D( const FMatrix4x4& Matrix )
{
	FDrawMatrixData Data;
	PackAffine( Data.M, Matrix );
	for ( const float32 Value : Data.M )
	{
		if ( !IsDrawable( Value ) )
		{
			return;
		}
	}
	Push( EDrawCommand::Transform2D, 0, Data );
}

void FDrawContext2DImages::ResetTransform2D()
{
	Transform2D( FMatrix4x4::Identity );
}

