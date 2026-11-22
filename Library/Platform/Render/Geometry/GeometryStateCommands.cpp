// SPDX-License-Identifier: MIT
#include "GeometryStateCommands.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbonMesh.h"
#include "Library/Platform/Render/Geometry/GeometryCommandHelpers.h"
#include "Library/Platform/Render/Geometry/GeometryQuadBuilder.h"
#include "Library/Platform/Render/Geometry/GeometryShapeBuilder.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	using NGeometryCommand::Finite;
	using NGeometryCommand::ToMatrix;
	using NGeometryCommand::ToColor;
}

CGeometryStateCommands::CGeometryStateCommands( FGeometryExecution& InRun )
	: Run( InRun )
	, Geometry( Run.Geometry )
	, World( Run.World )
	, Camera( Run.Camera )
	, State( Run.State )
	, Lighting( Run.Lighting )
	, Fog( Run.Fog )
	, HeightFog( Run.HeightFog )
	, TextureTransform( Run.TextureTransform )
	, bAnisotropicFilter( Run.bAnisotropicFilter )
	, AnisotropyLevel( Run.AnisotropyLevel )
	, WorldStack( Run.WorldStack )
	, CameraStack( Run.CameraStack )
{
}

EGeometryCommandResult CGeometryStateCommands::Execute( const FDrawCommandView& View )
{
	switch ( View.Type )
	{
	case EDrawCommand::PushCamera:
	{
		if ( !View.CanRead<FDrawCameraData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawCameraData>(); // 検査後の固定部。
		FCamera Next; // 検査完了まで現在のカメラを保つ。
		Next.Position = FVector3( Data.Position[0], Data.Position[1], Data.Position[2] );
		Next.Target = FVector3( Data.Target[0], Data.Target[1], Data.Target[2] );
		Next.Up = FVector3( Data.Up[0], Data.Up[1], Data.Up[2] );
		Next.Projection = Data.Orthographic ? ECameraProjection::Orthographic : ECameraProjection::Perspective;
		Next.FovY = Data.FovY;
		Next.OrthoHeight = Data.OrthoHeight;
		Next.Near = Data.Near;
		Next.Far = Data.Far;
		Next.HorizontalStretch = Data.HorizontalStretch;
		Next.ScreenOffset = FVector2( Data.ScreenOffsetX, Data.ScreenOffsetY );
		if ( !Next.IsValid() )
		{
			return EGeometryCommandResult::Rejected;
		}
		CameraStack.Add( Camera );
		Camera = Next;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::PopCamera:
	{
		if ( !View.CanRead<FDrawPopCameraData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopCameraData>(); // 検査後の固定部。
		(void)Data;
		if ( !CameraStack.IsEmpty() )
		{
			Camera = CameraStack.Pop();
		}
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::WorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		World = ToMatrix( Data );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::PushWorldMatrix:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 検査後の固定部。
		if ( !Finite( Data.M ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		WorldStack.Add( World );
		World = ToMatrix( Data ) * World;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::PopWorldMatrix:
	{
		if ( !View.CanRead<FDrawPopWorldMatrixData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawPopWorldMatrixData>(); // 検査後の固定部。
		(void)Data;
		if ( !WorldStack.IsEmpty() )
		{
			World = WorldStack.Pop();
		}
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::Depth:
	{
		if ( !View.CanRead<FDrawDepthData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawDepthData>(); // 検査後の固定部。
		if ( Data.Compare < 0 || Data.Compare > 7 )
		{
			return EGeometryCommandResult::Rejected;
		}
		State.bTestDepth = Data.TestDepth != 0;
		State.bWriteDepth = Data.WriteDepth != 0;
		State.Compare = static_cast<EGeometryCompare>( Data.Compare );
		State.DepthBias = Data.Bias;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::Culling:
	{
		if ( !View.CanRead<FDrawCullingData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawCullingData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > 2 )
		{
			return EGeometryCommandResult::Rejected;
		}
		State.Culling = static_cast<EGeometryCulling>( Data.Mode );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::ClearDepth:
	{
		if ( !View.CanRead<FDrawClearDepthData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawClearDepthData>(); // 検査後の固定部。
		(void)Data;
		if ( !Geometry )
		{
			return EGeometryCommandResult::Rejected;
		}
		Geometry->ClearDepth();
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::UseLighting:
	{
		if ( !View.CanRead<FDrawUseLightingData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawUseLightingData>(); // 検査後の固定部。
		Lighting.SetEnabled( Data.Enabled != 0 );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::DefaultLight:
	{
		if ( !View.CanRead<FDrawDefaultLightData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawDefaultLightData>(); // 検査後の固定部。
		Lighting.SetDefaultLight( Data.Enabled != 0, Data.ShadowMapSlots & NDrawShadowMap::AllSlots );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::AmbientLight:
	{
		if ( !View.CanRead<FDrawAmbientLightData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawAmbientLightData>(); // 検査後の固定部。
		if ( !Finite( Data.Color ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		Lighting.SetAmbient( ToColor( Data.Color ) );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::SurfaceMaterial:
	{
		if ( !View.CanRead<FDrawSurfaceMaterialData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawSurfaceMaterialData>(); // 検査後の固定部。
		FSurfaceMaterial Material; // 検査が済むまで、現在の材質を保つ。
		Material.Diffuse = ToColor( Data.Diffuse );
		Material.Ambient = ToColor( Data.Ambient );
		Material.Specular = ToColor( Data.Specular );
		Material.Emissive = ToColor( Data.Emissive );
		Material.Shininess = Data.Shininess;
		Material.bUseVertexDiffuse = Data.UseVertexDiffuse != 0;
		Material.bUseVertexSpecular = Data.UseVertexSpecular != 0;
		return Lighting.SetMaterial( Material ) ? EGeometryCommandResult::Executed : EGeometryCommandResult::Rejected;
	}
	case EDrawCommand::LightingOptions:
	{
		if ( !View.CanRead<FDrawLightingOptionsData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawLightingOptionsData>(); // 検査後の固定部。
		FLightingOptions Options; // 画素ごとの光は、この経路では頂点ごとになる。
		Options.bUseSpecular = Data.UseSpecular != 0;
		Options.bPixelLighting = Data.PixelLighting != 0;
		Options.bWrapLighting = Data.WrapLighting != 0;
		Options.bIgnoreSurfaceAngle = Data.IgnoreSurfaceAngle != 0;
		Lighting.SetOptions( Options );
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::PushLight:
	{
		if ( !View.CanRead<FDrawLightData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawLightData>(); // 検査後の固定部。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawLightType::Spot ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		FLight Light; // 命令の値を、計算用の値へ移す。
		Light.Type = static_cast<EDrawLightType>( Data.Type );
		Light.Position = FVector3( Data.Position[0], Data.Position[1], Data.Position[2] );
		Light.Direction = FVector3( Data.Direction[0], Data.Direction[1], Data.Direction[2] );
		Light.Diffuse = ToColor( Data.Diffuse );
		Light.Specular = ToColor( Data.Specular );
		Light.Ambient = ToColor( Data.Ambient );
		Light.Range = Data.Range;
		Light.ConstantAttenuation = Data.ConstantAttenuation;
		Light.LinearAttenuation = Data.LinearAttenuation;
		Light.QuadraticAttenuation = Data.QuadraticAttenuation;
		Light.OuterAngle = Data.OuterAngle;
		Light.InnerAngle = Data.InnerAngle;
		Light.ShadowMapSlots = Data.ShadowMapSlots & NDrawShadowMap::AllSlots;
		if ( !Lighting.PushLight( Light ) )
		{
			// 外すときの数を合わせるため、使えないライトも、光らないライトとして積む。
			FLight Dark = FLight::MakeDirectional( FVector3( 0.0f, -1.0f, 0.0f ), FColor( 0.0f, 0.0f, 0.0f, 0.0f ) );
			Lighting.PushLight( Dark );
		}
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::PopLight:
	{
		if ( !View.CanRead<FDrawPopLightData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		Lighting.PopLight();
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::Fog:
	{
		if ( !View.CanRead<FDrawFogData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawFogData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFogMode::Linear ) || !NMath::IsFinite( Data.Start ) || !NMath::IsFinite( Data.End ) || !NMath::IsFinite( Data.Density ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		Fog = Data;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::HeightFog:
	{
		if ( !View.CanRead<FDrawFogData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawFogData>(); // 検査後の固定部。
		if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFogMode::Linear ) || !NMath::IsFinite( Data.Start ) || !NMath::IsFinite( Data.End ) || !NMath::IsFinite( Data.Density ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		HeightFog = Data;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::TextureTransform:
	{
		if ( !View.CanRead<FDrawTextureTransformData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawTextureTransformData>(); // 検査後の固定部。
		if ( Data.Enabled != 0 && !( NMath::IsFinite( Data.TranslationU ) && NMath::IsFinite( Data.TranslationV ) && NMath::IsFinite( Data.ScaleU ) && NMath::IsFinite( Data.ScaleV ) && NMath::IsFinite( Data.CenterU )
			&& NMath::IsFinite( Data.CenterV ) && NMath::IsFinite( Data.Rotation ) ) )
		{
			return EGeometryCommandResult::Rejected;
		}
		TextureTransform = Data;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::Anisotropy:
	{
		if ( !View.CanRead<FDrawAnisotropyData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		AnisotropyLevel = NMath::Clamp( View.Read<FDrawAnisotropyData>().Level, 1, 16 ); // 異方性の最大の強さ。Filter が異方性のときだけ効く。
		State.MaxAnisotropy = bAnisotropicFilter ? static_cast<uint8>( AnisotropyLevel ) : 1;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::Filter:
	{
		if ( !View.CanRead<FDrawFilterData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawFilterData>(); // 最近傍・線形補間・異方性（線形のうえで、異方性の強さを使う）。
		if ( Data.Mode < 0 || Data.Mode > 2 )
		{
			return EGeometryCommandResult::Rejected;
		}
		State.bLinearFilter = Data.Mode >= 1;
		bAnisotropicFilter = Data.Mode == 2;
		State.MaxAnisotropy = bAnisotropicFilter ? static_cast<uint8>( AnisotropyLevel ) : 1;
		return EGeometryCommandResult::Executed;
	}
	case EDrawCommand::TextureAddress:
	{
		if ( !View.CanRead<FDrawTextureAddressData>() )
		{
			return EGeometryCommandResult::Rejected;
		}
		const auto Data = View.Read<FDrawTextureAddressData>(); // UVの外側の参照方法。
		if ( Data.U < 0 || Data.U > 3 || Data.V < 0 || Data.V > 3 )
		{
			return EGeometryCommandResult::Rejected;
		}
		State.AddressU = static_cast<EGeometryTextureAddress>( Data.U );
		State.AddressV = static_cast<EGeometryTextureAddress>( Data.V );
		return EGeometryCommandResult::Executed;
	}
	default:
		return EGeometryCommandResult::NotHandled;
	}
}
