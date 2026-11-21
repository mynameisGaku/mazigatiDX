#include "LightCullingShaders.h"

#include "MazigatiEngineShaderSources.generated.h"

const char* NLightCullingShaders::GetTileLightListSource()
{
	return NEngineShaderSources::LightCulling_TileLightList;
}
