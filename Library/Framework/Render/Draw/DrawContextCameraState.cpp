#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

namespace
{
	// フォグの設定を命令のデータにする（Mode が None なら、色などは使われない）
	FDrawFogData MakeFogData( const FFogSettings& Settings )
	{
		FDrawFogData Data;
		Data.Mode = static_cast<int32>( Settings.Mode );
		if ( Settings.Mode != EDrawFogMode::None )
		{
			Data.Color = FDrawRgb{ Settings.Color.r, Settings.Color.g, Settings.Color.b, 0 };
			Data.Start = Settings.Start;
			Data.End = Settings.End;
			Data.Density = Settings.Density;
		}
		return Data;
	}
}

void FDrawContextCameraState::PopCamera()
{
	const FDrawPopCameraData Data;
	Push( EDrawCommand::PopCamera, 0, Data );
}

void FDrawContextCameraState::Culling( EDrawCulling Mode )
{
	if ( !IsInEnumRange( static_cast<int32>( Mode ), static_cast<int32>( EDrawCulling::FrontFaces ) ) )
	{
		return;
	}
	FDrawCullingData Data;
	Data.Mode = static_cast<int32>( Mode );
	Push( EDrawCommand::Culling, 0, Data );
}

void FDrawContextCameraState::TextureAddress( EDrawTextureAddress Mode )
{
	TextureAddress( Mode, Mode );
}

void FDrawContextCameraState::ResetTextureTransform()
{
	TextureTransform( FTextureTransform() );
}

void FDrawContextCameraState::Anisotropy( int32 MaxLevel )
{
	FDrawAnisotropyData Data;
	Data.Level = NMath::Clamp( MaxLevel, 1, 16 );
	Push( EDrawCommand::Anisotropy, 0, Data );
}

void FDrawContextCameraState::ClearDepth()
{
	const FDrawClearDepthData Data;
	Push( EDrawCommand::ClearDepth, 0, Data );
}

void FDrawContextCameraState::Depth( const FDepthState& State )
{
	if ( !State.IsValid() )
	{
		return;
	}
	FDrawDepthData Data;
	Data.TestDepth = State.bTestDepth ? 1 : 0;
	Data.WriteDepth = State.bWriteDepth ? 1 : 0;
	Data.Compare = static_cast<int32>( State.Compare );
	Data.Bias = NMath::Clamp( State.Bias, -FDepthState::MaxBias, FDepthState::MaxBias );
	Push( EDrawCommand::Depth, 0, Data );
}

void FDrawContextCameraState::TextureAddress( EDrawTextureAddress U, EDrawTextureAddress V )
{
	const int32 Last = static_cast<int32>( EDrawTextureAddress::Border );
	if ( !IsInEnumRange( static_cast<int32>( U ), Last ) || !IsInEnumRange( static_cast<int32>( V ), Last ) )
	{
		return;
	}
	FDrawTextureAddressData Data;
	Data.U = static_cast<int32>( U );
	Data.V = static_cast<int32>( V );
	Push( EDrawCommand::TextureAddress, 0, Data );
}

void FDrawContextCameraState::TextureTransform( const FTextureTransform& Transform )
{
	if ( !Transform.IsValid() )
	{
		return;
	}
	FDrawTextureTransformData Data;
	if ( !Transform.IsIdentity() )
	{
		Data.Enabled = 1;
		Data.TranslationU = Transform.Translation.x;
		Data.TranslationV = Transform.Translation.y;
		Data.ScaleU = Transform.Scale.x;
		Data.ScaleV = Transform.Scale.y;
		Data.CenterU = Transform.RotationCenter.x;
		Data.CenterV = Transform.RotationCenter.y;
		Data.Rotation = Transform.Rotation;
	}
	Push( EDrawCommand::TextureTransform, 0, Data );
}

void FDrawContextCameraState::Fog( const FFogSettings& Settings )
{
	if ( !Settings.IsValid() )
	{
		return;
	}
	Push( EDrawCommand::Fog, 0, MakeFogData( Settings ) );
}

void FDrawContextCameraState::HeightFog( const FFogSettings& Settings )
{
	if ( !Settings.IsValid() )
	{
		return;
	}
	Push( EDrawCommand::HeightFog, 0, MakeFogData( Settings ) );
}

bool FDrawContextCameraState::PushCamera( const FCamera& Camera )
{
	if ( !Camera.IsValid() )
	{
		return false;
	}
	FDrawCameraData Data;
	Data.Position[0] = Camera.Position.x;
	Data.Position[1] = Camera.Position.y;
	Data.Position[2] = Camera.Position.z;
	Data.Target[0] = Camera.Target.x;
	Data.Target[1] = Camera.Target.y;
	Data.Target[2] = Camera.Target.z;
	Data.Up[0] = Camera.Up.x;
	Data.Up[1] = Camera.Up.y;
	Data.Up[2] = Camera.Up.z;
	Data.Orthographic = Camera.Projection == ECameraProjection::Orthographic ? 1 : 0;
	Data.FovY = Camera.FovY;
	Data.OrthoHeight = Camera.OrthoHeight;
	Data.Near = Camera.Near;
	Data.Far = Camera.Far;
	Data.HorizontalStretch = Camera.HorizontalStretch;
	Data.ScreenOffsetX = Camera.ScreenOffset.x;
	Data.ScreenOffsetY = Camera.ScreenOffset.y;
	Push( EDrawCommand::PushCamera, 0, Data );
	return true;
}
