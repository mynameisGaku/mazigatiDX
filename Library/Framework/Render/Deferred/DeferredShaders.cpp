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
