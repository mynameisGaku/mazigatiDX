#include "DeferredShaders.h"

#include "MazigatiEngineShaderSources.generated.h"

const char* NDeferredShaders::GetGBufferWriteSource()
{
	return NEngineShaderSources::Deferred_GBufferWrite;
}

const char* NDeferredShaders::GetGBufferReadSource()
{
	return NEngineShaderSources::Deferred_GBufferRead;
}

FString NDeferredShaders::GetSampleSource( EDeferredSampleShader Shader )
{
	// 共通の部品（場面の定数・光・見せ方、物の入出力）を、シェーダーごとに必要な順で連ねる
	const FString Scene = FString( NEngineShaderSources::Deferred_SampleCommon ) + NEngineShaderSources::Deferred_SampleGeometry;
	switch ( Shader )
	{
	case EDeferredSampleShader::Vertex:
		return Scene + NEngineShaderSources::Deferred_SampleVertex;
	case EDeferredSampleShader::ForwardObject:
		return Scene + NEngineShaderSources::Deferred_SampleForwardObject;
	case EDeferredSampleShader::ForwardGround:
		return Scene + NEngineShaderSources::Deferred_SampleForwardGround;
	case EDeferredSampleShader::GeometryObject:
		return FString( GetGBufferWriteSource() ) + Scene + NEngineShaderSources::Deferred_SampleGeometryObject;
	case EDeferredSampleShader::GeometryGround:
		return FString( GetGBufferWriteSource() ) + Scene + NEngineShaderSources::Deferred_SampleGeometryGround;
	case EDeferredSampleShader::Lighting:
		return FString( GetGBufferReadSource() ) + NEngineShaderSources::Deferred_SampleCommon + NEngineShaderSources::Deferred_SampleLighting;
	case EDeferredSampleShader::Composite:
		return FString( NEngineShaderSources::Deferred_SampleComposite );
	}
	return FString();
}
