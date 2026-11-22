#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

bool FDrawContextTargets::PushTarget( FImageHandle Target )
{
	if ( !Target.IsValid() )
	{
		return false;
	}
	FDrawTargetData Data;
	Data.Handle = Target.Value;
	Push( EDrawCommand::PushTarget, 0, Data );
	return true;
}

void FDrawContextTargets::PopTarget()
{
	const FDrawPopTargetData Data;
	Push( EDrawCommand::PopTarget, 0, Data );
}

void FDrawContextTargets::Clear( const FColor32& Color )
{
	FDrawClearData Data;
	Data.R = Color.r;
	Data.G = Color.g;
	Data.B = Color.b;
	Data.A = Color.a;
	Push( EDrawCommand::Clear, 0, Data );
}

void FDrawContextTargets::CopyScreen( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Dest )
{
	if ( !Dest.IsValid() || X1 <= X0 || Y1 <= Y0 || !AllDrawable( static_cast<float32>( X0 ), static_cast<float32>( Y0 ), static_cast<float32>( X1 ), static_cast<float32>( Y1 ) ) )
	{
		return;
	}
	FDrawCopyScreenData Data;
	Data.X0 = X0;
	Data.Y0 = Y0;
	Data.X1 = X1;
	Data.Y1 = Y1;
	Data.Handle = Dest.Value;
	Push( EDrawCommand::CopyScreen, 0, Data );
}

bool FDrawContextTargets::ApplyFilter( FImageHandle Target, const FGraphFilter& Filter )
{
	return PushImageFilter( Target, Target, Filter, false, false, 0, 0, 0, 0, 0, 0 );
}

bool FDrawContextTargets::ApplyFilter( FImageHandle Source, FImageHandle Dest, const FGraphFilter& Filter, bool bAlphaBlend )
{
	return PushImageFilter( Source, Dest, Filter, bAlphaBlend, false, 0, 0, 0, 0, 0, 0 );
}

bool FDrawContextTargets::ApplyFilterRect( FImageHandle Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Dest, int32 DestX, int32 DestY, const FGraphFilter& Filter, bool bAlphaBlend )
{
	return PushImageFilter( Source, Dest, Filter, bAlphaBlend, true, SrcX, SrcY, SrcW, SrcH, DestX, DestY );
}

bool FDrawContextTargets::BlendImages( FImageHandle Base, FImageHandle Overlay, FImageHandle Dest, const FGraphBlend& Mode )
{
	if ( !Base.IsValid() || !Overlay.IsValid() || !Dest.IsValid() || !Mode.IsValid() )
	{
		return false;
	}
	FDrawImageBlendData Data;
	Data.Base = Base.Value;
	Data.Blend = Overlay.Value;
	Data.Dest = Dest.Value;
	Data.Type = static_cast<int32>( Mode.Type );
	Data.Ratio = NMath::Clamp( Mode.Ratio, 0, 255 );
	Data.Premultiplied = Mode.bPremultiplied ? 1 : 0;
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Data.Select[Index] = static_cast<int32>( Mode.Select[Index] );
	}
	Push( EDrawCommand::ImageBlend, 0, Data );
	return true;
}

bool FDrawContextTargets::PushMask( FImageHandle Mask, EDrawMaskChannel Channel, bool bInvert )
{
	if ( !Mask.IsValid() || !IsInEnumRange( static_cast<int32>( Channel ), static_cast<int32>( EDrawMaskChannel::Blue ) ) )
	{
		return false;
	}
	FDrawMaskData Data;
	Data.Handle = Mask.Value;
	Data.Channel = static_cast<int32>( Channel );
	Data.Invert = bInvert ? 1 : 0;
	Push( EDrawCommand::PushMask, 0, Data );
	return true;
}

void FDrawContextTargets::PopMask()
{
	const FDrawPopMaskData Data;
	Push( EDrawCommand::PopMask, 0, Data );
}

