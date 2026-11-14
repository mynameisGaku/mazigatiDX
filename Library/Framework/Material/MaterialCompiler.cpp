// SPDX-License-Identifier: MIT
#include "MaterialCompiler.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Material/MaterialGraphCompiler.h"

const char* NMaterialSlabField::GetName( int32 Field )
{
	// SubstrateSlab のピンの並びと同じ
	static const char* const Names[NMaterialSlabField::Num] = { "BaseColor", "F0", "Roughness", "Metallic", "Emissive", "Opacity", "ClearCoat", "ClearCoatRoughness", "Subsurface", "Anisotropy", "ThinFilm", "Normal" };
	return Field >= 0 && Field < NMaterialSlabField::Num ? Names[Field] : "";
}

FMaterialProgram NMaterialCompiler::Compile( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities )
{
	NMaterialGraphCompiler::FCompiler Compiler( Definition, Capabilities );
	return Compiler.Run();
}
