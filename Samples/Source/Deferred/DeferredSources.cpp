#include "DeferredSources.h"

#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Deferred/DeferredShaders.h"

DEFINE_LOG_CATEGORY_STATIC( LogDeferredSample, Log )

namespace
{
	// 1 つコンパイルする（HLSL は EngineContent/Shaders/Deferred のファイル。NDeferredShaders が全文を渡す）。失敗したら、理由をログに出して、nullptr を返す
	TSharedPtr<CShader> CompileOne( EShaderStage Stage, EDeferredSampleShader Shader, const char* Name )
	{
		FString Message;
		TSharedPtr<CShader> Compiled = CShader::Compile( Stage, NDeferredShaders::GetSampleSource( Shader ), "main", &Message );
		if ( !Compiled.IsValid() )
		{
			LOG( LogDeferredSample, Error, "Cannot compile the shader %s: %s", Name, Message.GetData() );
		}
		return Compiled;
	}
}

bool NDeferredSources::CompileAll( FDeferredDemoState& State )
{
	State.SceneVertex = CompileOne( EShaderStage::Vertex, EDeferredSampleShader::Vertex, "scene vertex" );
	State.ForwardObjectPixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::ForwardObject, "forward object" );
	State.ForwardGroundPixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::ForwardGround, "forward ground" );
	State.GeometryObjectPixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::GeometryObject, "geometry object" );
	State.GeometryGroundPixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::GeometryGround, "geometry ground" );
	State.LightingPixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::Lighting, "lighting" );
	State.CompositePixel = CompileOne( EShaderStage::Pixel, EDeferredSampleShader::Composite, "composite" );
	return State.SceneVertex.IsValid() && State.ForwardObjectPixel.IsValid() && State.ForwardGroundPixel.IsValid() && State.GeometryObjectPixel.IsValid() && State.GeometryGroundPixel.IsValid() && State.LightingPixel.IsValid() && State.CompositePixel.IsValid();
}
