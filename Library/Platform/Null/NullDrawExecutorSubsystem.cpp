// SPDX-License-Identifier: MIT
#include "NullDrawExecutorSubsystem.h"

#include "Library/Framework/Render/Draw/DrawDistortedMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawDistortedMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawMaterialMesh.h"
#include "Library/Framework/Render/Draw/DrawMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawModelMaterial.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Framework/Render/Draw/DrawRibbon.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"

DEFINE_LOG_CATEGORY_STATIC( LogNullDraw, Log )

REGISTER_SUBSYSTEM( CNullDrawExecutorSubsystem )

namespace
{
	// 対になる命令の組（Push 側と Pop 側）。深さを 1 つずつ持つ
	enum class EPair : uint8
	{
		Target,
		Clip,
		Mask,
		Camera,
		WorldMatrix,
		Light,
		ShadowMap,
		Count,
	};

	template<typename TData>
	constexpr int32 WordsOf()
	{
		return static_cast<int32>( sizeof( TData ) / 4 );
	}

	// 命令の種類ごとの、最低限いるデータのワード数（ヘッダーの次のワードから。文字列が後ろに続く命令は、終端の '\0' の 1 ワードも足す）。
	// 知らない種類は -1
	int32 GetMinDataWords( EDrawCommand Type )
	{
		switch ( Type )
		{
		case EDrawCommand::Box: return WordsOf<FDrawBoxData>();
		case EDrawCommand::Circle: return WordsOf<FDrawCircleData>();
		case EDrawCommand::Line: return WordsOf<FDrawLineData>();
		case EDrawCommand::Text: return WordsOf<FDrawTextData>() + 1;
		case EDrawCommand::Sprite: return WordsOf<FDrawSpriteData>();
		case EDrawCommand::Blend: return WordsOf<FDrawBlendData>();
		case EDrawCommand::Line3D: return WordsOf<FDrawLine3DData>();
		case EDrawCommand::Triangle3D: return WordsOf<FDrawTriangle3DData>();
		case EDrawCommand::Sphere3D: return WordsOf<FDrawSphere3DData>();
		case EDrawCommand::Capsule3D: return WordsOf<FDrawCapsule3DData>();
		case EDrawCommand::Cube3D: return WordsOf<FDrawCube3DData>();
		case EDrawCommand::Billboard3D: return WordsOf<FDrawBillboard3DData>();
		case EDrawCommand::WorldMatrix: return WordsOf<FDrawMatrixData>();
		case EDrawCommand::Model3D: return WordsOf<FDrawModelData>();
		case EDrawCommand::ModelFramePose3D: return WordsOf<FDrawModelFramePoseData>();
		case EDrawCommand::SpriteRect: return WordsOf<FDrawSpriteRectData>();
		case EDrawCommand::Bright: return WordsOf<FDrawBrightData>();
		case EDrawCommand::TextFont: return WordsOf<FDrawTextFontData>() + 1;
		case EDrawCommand::ModelAnim: return WordsOf<FDrawModelAnimData>();
		case EDrawCommand::Quad3D: return WordsOf<FDrawQuad3DData>();
		case EDrawCommand::Mesh3D: return WordsOf<FDrawMeshData>();
		case EDrawCommand::Ribbon3D: return WordsOf<FDrawRibbonData>();
		case EDrawCommand::MaterialQuad3D: return WordsOf<FDrawMaterialQuadData>();
		case EDrawCommand::MaterialRibbon3D: return WordsOf<FDrawMaterialRibbonData>();
		case EDrawCommand::DistortedMaterialQuad3D: return WordsOf<FDrawDistortedMaterialQuadData>();
		case EDrawCommand::DistortedMaterialRibbon3D: return WordsOf<FDrawDistortedMaterialRibbonData>();
		case EDrawCommand::MaterialMesh3D: return WordsOf<FDrawMaterialMeshData>();
		case EDrawCommand::ModelMaterial3D: return WordsOf<FDrawModelMaterialData>();
		case EDrawCommand::Pixel: return WordsOf<FDrawPixelData>();
		case EDrawCommand::Triangle: return WordsOf<FDrawTriangleData>();
		case EDrawCommand::Quad: return WordsOf<FDrawQuadData>();
		case EDrawCommand::Oval: return WordsOf<FDrawOvalData>();
		case EDrawCommand::RoundBox: return WordsOf<FDrawRoundBoxData>();
		case EDrawCommand::BoxAA: return WordsOf<FDrawBoxAAData>();
		case EDrawCommand::CircleAA: return WordsOf<FDrawCircleAAData>();
		case EDrawCommand::LineAA: return WordsOf<FDrawLineAAData>();
		case EDrawCommand::Image: return WordsOf<FDrawImageData>();
		case EDrawCommand::ImageStretch: return WordsOf<FDrawImageStretchData>();
		case EDrawCommand::ImageQuad: return WordsOf<FDrawImageQuadData>();
		case EDrawCommand::ImagePart: return WordsOf<FDrawImagePartData>();
		case EDrawCommand::ImagePartStretch: return WordsOf<FDrawImagePartStretchData>();
		case EDrawCommand::ImagePartQuad: return WordsOf<FDrawImagePartQuadData>();
		case EDrawCommand::SpritePivot: return WordsOf<FDrawSpritePivotData>();
		case EDrawCommand::Tile: return WordsOf<FDrawTileData>();
		case EDrawCommand::CircleGauge: return WordsOf<FDrawCircleGaugeData>();
		case EDrawCommand::CustomBlend: return WordsOf<FDrawCustomBlendData>();
		case EDrawCommand::AddColor: return WordsOf<FDrawAddColorData>();
		case EDrawCommand::AlphaTest: return WordsOf<FDrawAlphaTestData>();
		case EDrawCommand::Filter: return WordsOf<FDrawFilterData>();
		case EDrawCommand::PushClip: return WordsOf<FDrawClipData>();
		case EDrawCommand::PopClip: return WordsOf<FDrawPopClipData>();
		case EDrawCommand::Transform2D: return WordsOf<FDrawMatrixData>();
		case EDrawCommand::PushTarget: return WordsOf<FDrawTargetData>();
		case EDrawCommand::PopTarget: return WordsOf<FDrawPopTargetData>();
		case EDrawCommand::Clear: return WordsOf<FDrawClearData>();
		case EDrawCommand::CopyScreen: return WordsOf<FDrawCopyScreenData>();
		case EDrawCommand::ImageFilter: return WordsOf<FDrawImageFilterData>();
		case EDrawCommand::ImageBlend: return WordsOf<FDrawImageBlendData>();
		case EDrawCommand::PushMask: return WordsOf<FDrawMaskData>();
		case EDrawCommand::PopMask: return WordsOf<FDrawPopMaskData>();
		case EDrawCommand::TextStyled: return WordsOf<FDrawTextStyledData>() + 1;
		case EDrawCommand::PushCamera: return WordsOf<FDrawCameraData>();
		case EDrawCommand::PopCamera: return WordsOf<FDrawPopCameraData>();
		case EDrawCommand::Depth: return WordsOf<FDrawDepthData>();
		case EDrawCommand::Culling: return WordsOf<FDrawCullingData>();
		case EDrawCommand::TextureAddress: return WordsOf<FDrawTextureAddressData>();
		case EDrawCommand::TextureTransform: return WordsOf<FDrawTextureTransformData>();
		case EDrawCommand::Fog: return WordsOf<FDrawFogData>();
		case EDrawCommand::HeightFog: return WordsOf<FDrawFogData>();
		case EDrawCommand::Anisotropy: return WordsOf<FDrawAnisotropyData>();
		case EDrawCommand::ClearDepth: return WordsOf<FDrawClearDepthData>();
		case EDrawCommand::PushWorldMatrix: return WordsOf<FDrawMatrixData>();
		case EDrawCommand::PopWorldMatrix: return WordsOf<FDrawPopWorldMatrixData>();
		case EDrawCommand::UseLighting: return WordsOf<FDrawUseLightingData>();
		case EDrawCommand::DefaultLight: return WordsOf<FDrawDefaultLightData>();
		case EDrawCommand::AmbientLight: return WordsOf<FDrawAmbientLightData>();
		case EDrawCommand::SurfaceMaterial: return WordsOf<FDrawSurfaceMaterialData>();
		case EDrawCommand::LightingOptions: return WordsOf<FDrawLightingOptionsData>();
		case EDrawCommand::PushLight: return WordsOf<FDrawLightData>();
		case EDrawCommand::PopLight: return WordsOf<FDrawPopLightData>();
		case EDrawCommand::Pixel3D: return WordsOf<FDrawPixel3DData>();
		case EDrawCommand::Cone3D: return WordsOf<FDrawCone3DData>();
		case EDrawCommand::BillboardPivot3D: return WordsOf<FDrawBillboardPivot3DData>();
		case EDrawCommand::BillboardQuad3D: return WordsOf<FDrawBillboardQuad3DData>();
		case EDrawCommand::Sprite3D: return WordsOf<FDrawSprite3DData>();
		case EDrawCommand::SpriteStretch3D: return WordsOf<FDrawSpriteStretch3DData>();
		case EDrawCommand::SpritePivot3D: return WordsOf<FDrawSpritePivot3DData>();
		case EDrawCommand::Primitive3D: return WordsOf<FDrawPrimitive3DData>();
		case EDrawCommand::PrimitiveBuffer3D: return WordsOf<FDrawPrimitiveBuffer3DData>();
		case EDrawCommand::BeginShadowMap: return WordsOf<FDrawBeginShadowMapData>();
		case EDrawCommand::EndShadowMap: return WordsOf<FDrawEndShadowMapData>();
		case EDrawCommand::UseShadowMap: return WordsOf<FDrawUseShadowMapData>();
		case EDrawCommand::PreviewShadowMap: return WordsOf<FDrawPreviewShadowMapData>();
		case EDrawCommand::UseShader: return WordsOf<FDrawUseShaderData>();
		case EDrawCommand::ShaderConstants: return WordsOf<FDrawShaderConstantsData>();
		case EDrawCommand::ShaderTexture: return WordsOf<FDrawShaderTextureData>();
		case EDrawCommand::PrimitiveShader3D: return WordsOf<FDrawPrimitiveShader3DData>();
		case EDrawCommand::ShaderRect: return WordsOf<FDrawShaderRectData>();
		case EDrawCommand::ShaderOutputTarget: return WordsOf<FDrawShaderOutputTargetData>();
		case EDrawCommand::ModelAnimFrame: return WordsOf<FDrawModelAnimFrameData>();
		}
		return -1;
	}

	// 命令が、対のどれかの Push 側（+1）・Pop 側（-1）なら true にして、組と向きを返す
	bool TryGetPair( EDrawCommand Type, EPair& OutPair, int32& OutStep )
	{
		switch ( Type )
		{
		case EDrawCommand::PushTarget: OutPair = EPair::Target; OutStep = 1; return true;
		case EDrawCommand::PopTarget: OutPair = EPair::Target; OutStep = -1; return true;
		case EDrawCommand::PushClip: OutPair = EPair::Clip; OutStep = 1; return true;
		case EDrawCommand::PopClip: OutPair = EPair::Clip; OutStep = -1; return true;
		case EDrawCommand::PushMask: OutPair = EPair::Mask; OutStep = 1; return true;
		case EDrawCommand::PopMask: OutPair = EPair::Mask; OutStep = -1; return true;
		case EDrawCommand::PushCamera: OutPair = EPair::Camera; OutStep = 1; return true;
		case EDrawCommand::PopCamera: OutPair = EPair::Camera; OutStep = -1; return true;
		case EDrawCommand::PushWorldMatrix: OutPair = EPair::WorldMatrix; OutStep = 1; return true;
		case EDrawCommand::PopWorldMatrix: OutPair = EPair::WorldMatrix; OutStep = -1; return true;
		case EDrawCommand::PushLight: OutPair = EPair::Light; OutStep = 1; return true;
		case EDrawCommand::PopLight: OutPair = EPair::Light; OutStep = -1; return true;
		case EDrawCommand::BeginShadowMap: OutPair = EPair::ShadowMap; OutStep = 1; return true;
		case EDrawCommand::EndShadowMap: OutPair = EPair::ShadowMap; OutStep = -1; return true;
		default: break;
		}
		return false;
	}
}

bool CNullDrawExecutorSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

void CNullDrawExecutorSubsystem::Deinitialize()
{
	LOG( LogNullDraw, Log, "Draw statistics: %lld span(s), %lld command(s), %lld word(s), %lld broken span(s), %lld broken command(s), %lld unbalanced span(s), %lld extra pop(s)",
		 static_cast<long long>( Stats.NumSpans ), static_cast<long long>( Stats.NumCommands ), static_cast<long long>( Stats.NumWords ),
		 static_cast<long long>( Stats.NumBrokenSpans ), static_cast<long long>( Stats.NumBrokenCommands ), static_cast<long long>( Stats.NumUnbalancedSpans ),
		 static_cast<long long>( Stats.NumExtraPops ) );
}

void CNullDrawExecutorSubsystem::Execute( const FDrawSpan& Span )
{
	++Stats.NumSpans;

	int32 Depth[static_cast<int32>( EPair::Count )] = {};
	int64 ConsumedWords = 0;
	bool bUnbalanced = false;

	ForEachDrawCommand( Span, [&]( const FDrawCommandView& Command )
	{
		++Stats.NumCommands;
		++Stats.CommandCounts[static_cast<int32>( Command.Type )];
		ConsumedWords += 1 + Command.NumDataWords;

		const int32 MinWords = GetMinDataWords( Command.Type );
		if ( MinWords < 0 || Command.NumDataWords < MinWords )
		{
			++Stats.NumBrokenCommands;
			return;
		}
		if ( Command.Type == EDrawCommand::ModelFramePose3D )
		{
			FDrawModelFramePoseData Data{}; // 検査する固定header値。
			if ( !NDrawModelFramePose::IsValidCommand( Command, Data ) )
			{
				++Stats.NumBrokenCommands;
			}
			return;
		}

		EPair Pair = EPair::Target;
		int32 Step = 0;
		if ( TryGetPair( Command.Type, Pair, Step ) )
		{
			int32& Current = Depth[static_cast<int32>( Pair )];
			if ( Step < 0 && Current == 0 )
			{
				++Stats.NumExtraPops;
			}
			else
			{
				Current += Step;
			}
		}
	} );

	Stats.NumWords += ConsumedWords;
	if ( ConsumedWords != static_cast<int64>( Span.NumWords ) )
	{
		++Stats.NumBrokenSpans;
	}
	for ( int32 Index = 0; Index < static_cast<int32>( EPair::Count ); ++Index )
	{
		if ( Depth[Index] != 0 )
		{
			bUnbalanced = true;
		}
	}
	if ( bUnbalanced )
	{
		++Stats.NumUnbalancedSpans;
	}
}

const FNullDrawStats& CNullDrawExecutorSubsystem::GetStats() const
{
	return Stats;
}

void CNullDrawExecutorSubsystem::ResetStats()
{
	Stats = FNullDrawStats();
}
