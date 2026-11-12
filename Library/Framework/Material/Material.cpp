// SPDX-License-Identifier: MIT
#include "Library/Framework/Material/Material.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Material/MaterialHlsl.h"
#include "Library/Framework/Render/State/RenderState3D.h"

namespace
{
	// HLSL の cbMaterialFrame（UI・Particle・PostProcess）
	struct FMaterialFrameConstants
	{
		FVector4 Time;
		FVector4 Rect;
		FVector4 State;
	};

	// HLSL の cbScene（Surface）
	struct FMaterialSceneConstants
	{
		FMatrix4x4 ViewProjection;
		FVector4 LightDirection;
		FVector4 CameraPosition;
		FVector4 Time;
	};

	// HLSL の cbObject（Surface）
	struct FMaterialObjectConstants
	{
		FMatrix4x4 World;
		FVector4 Color;
		FVector4 Options;
	};

	FVector4 ToVector( const FColor32& Color )
	{
		return FVector4( static_cast<float32>( Color.r ) / 255.0f, static_cast<float32>( Color.g ) / 255.0f, static_cast<float32>( Color.b ) / 255.0f, static_cast<float32>( Color.a ) / 255.0f );
	}
}

void CMaterial::Compile( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities )
{
	Compiled = MakeShared<FMaterialCompiled>();
	Compiled->Definition = Definition;
	Compiled->Capabilities = Capabilities;
	Compiled->Program = NMaterialCompiler::Compile( Definition, Capabilities );
	if ( Compiled->Program.bValid )
	{
		Compiled->Hlsl = NMaterialHlsl::Generate( Compiled->Program, Definition.CustomHlsl );
	}
}

TSharedPtr<CMaterial> CMaterial::Create( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities )
{
	TSharedPtr<CMaterial> Material( new CMaterial() );
	Material->Compile( Definition, Capabilities );
	const FMaterialProgram& Program = Material->Compiled->Program;
	for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
	{
		if ( !Program.Params[Index].IsFromParameterCollection() )
		{
			FValue Value;
			Value.Name = Program.Params[Index].Name;
			Value.Value = Program.Params[Index].Default;
			Material->Values.Add( Value );
		}
	}
	return Material;
}

TSharedPtr<CMaterial> CMaterial::CreateInstance( const TSharedPtr<CMaterial>& Parent )
{
	if ( !Parent )
	{
		return TSharedPtr<CMaterial>();
	}
	TSharedPtr<CMaterial> Material( new CMaterial() );
	Material->Parent = Parent;
	return Material;
}

bool CMaterial::Reload( const FMaterialDefinition& Definition )
{
	if ( Parent )
	{
		return false;
	}
	const FMaterialProgram OldProgram = Compiled->Program;
	const TArray<FValue> OldValues = Values;
	const FMaterialCapabilities Capabilities = Compiled->Capabilities;
	Compile( Definition, Capabilities );
	// 同じ名前・同じ型の値を、引き継ぐ
	Values.Empty();
	const FMaterialProgram& Program = Compiled->Program;
	for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
	{
		const FMaterialParamSlot& Slot = Program.Params[Index];
		if ( Slot.IsFromParameterCollection() )
		{
			continue;
		}
		FValue Value;
		Value.Name = Slot.Name;
		Value.Value = Slot.Default;
		const FMaterialParamSlot* Old = OldProgram.FindParam( Slot.Name );
		const FValue* Previous = FindValue( OldValues, Slot.Name );
		if ( Old != nullptr && Old->Type == Slot.Type && Previous != nullptr )
		{
			Value.Value = Previous->Value;
		}
		Values.Add( Value );
	}
	++Revision;
	LastError.Empty();
	return true;
}

CMaterial::FValue* CMaterial::FindValue( TArray<FValue>& Items, const FString& Name )
{
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		if ( Items[Index].Name == Name )
		{
			return &Items[Index];
		}
	}
	return nullptr;
}

const CMaterial::FValue* CMaterial::FindValue( const TArray<FValue>& Items, const FString& Name ) const
{
	for ( int32 Index = 0; Index < Items.Num(); ++Index )
	{
		if ( Items[Index].Name == Name )
		{
			return &Items[Index];
		}
	}
	return nullptr;
}

TArray<FMaterialDiagnostic> CMaterial::GetDiagnostics() const
{
	const FMaterialCompiled& Data = GetCompiled();
	TArray<FMaterialDiagnostic> All = Data.Program.Diagnostics;
	All.Append( Data.ShaderDiagnostics );
	return All;
}

// ---- パラメーター --------------------------------------------------------------------------------
bool CMaterial::SetScalar( const FString& Name, float32 Value )
{
	const FMaterialParamSlot* Slot = GetProgram().FindParam( Name );
	if ( Slot == nullptr || Slot->Type != EMaterialValueType::Float )
	{
		return false;
	}
	FValue* Existing = FindValue( Values, Name );
	if ( Existing == nullptr )
	{
		FValue Added;
		Added.Name = Name;
		Values.Add( Added );
		Existing = &Values.Last();
	}
	Existing->Value = FVector4( Value, 0.0f, 0.0f, 0.0f );
	return true;
}

bool CMaterial::SetVector( const FString& Name, const FVector4& Value )
{
	const FMaterialParamSlot* Slot = GetProgram().FindParam( Name );
	if ( Slot == nullptr || NMaterial::NumComponents( Slot->Type ) < 2 )
	{
		return false;
	}
	FValue* Existing = FindValue( Values, Name );
	if ( Existing == nullptr )
	{
		FValue Added;
		Added.Name = Name;
		Values.Add( Added );
		Existing = &Values.Last();
	}
	Existing->Value = Value;
	return true;
}

bool CMaterial::SetTexture( const FString& Name, const TSharedPtr<IImage>& Image )
{
	bool bFound = false;
	const TArray<FMaterialTextureSlot>& Slots = GetProgram().Textures;
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		bFound = bFound || ( Slots[Index].Source == EMaterialTextureSource::Parameter && Slots[Index].Name == Name );
	}
	if ( !bFound )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Textures.Num(); ++Index )
	{
		if ( Textures[Index].Name == Name )
		{
			Textures[Index].Image = Image;
			return true;
		}
	}
	FTextureValue Added;
	Added.Name = Name;
	Added.Image = Image;
	Textures.Add( Added );
	return true;
}

bool CMaterial::FindEffectiveValue( const FMaterialParamSlot& Slot, FVector4& OutValue ) const
{
	if ( const FValue* Own = FindValue( Values, Slot.Name ) )
	{
		OutValue = Own->Value;
		return true;
	}
	if ( Parent )
	{
		return Parent->FindEffectiveValue( Slot, OutValue );
	}
	OutValue = Slot.Default;
	return true;
}

TSharedPtr<IImage> CMaterial::FindEffectiveTexture( const FString& Name ) const
{
	for ( int32 Index = 0; Index < Textures.Num(); ++Index )
	{
		if ( Textures[Index].Name == Name && Textures[Index].Image )
		{
			return Textures[Index].Image;
		}
	}
	return Parent ? Parent->FindEffectiveTexture( Name ) : TSharedPtr<IImage>();
}

bool CMaterial::GetScalar( const FString& Name, float32& OutValue ) const
{
	const FMaterialParamSlot* Slot = GetProgram().FindParam( Name );
	FVector4 Value;
	if ( Slot == nullptr || Slot->Type != EMaterialValueType::Float || !FindEffectiveValue( *Slot, Value ) )
	{
		return false;
	}
	OutValue = Value.x;
	return true;
}

bool CMaterial::GetVector( const FString& Name, FVector4& OutValue ) const
{
	const FMaterialParamSlot* Slot = GetProgram().FindParam( Name );
	return Slot != nullptr && NMaterial::NumComponents( Slot->Type ) >= 2 && FindEffectiveValue( *Slot, OutValue );
}

bool CMaterial::ResetParameter( const FString& Name )
{
	if ( !Parent )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Values.Num(); ++Index )
	{
		if ( Values[Index].Name == Name )
		{
			Values.RemoveAt( Index );
			return true;
		}
	}
	for ( int32 Index = 0; Index < Textures.Num(); ++Index )
	{
		if ( Textures[Index].Name == Name )
		{
			Textures.RemoveAt( Index );
			return true;
		}
	}
	return false;
}

void CMaterial::ClearOverrides()
{
	if ( Parent )
	{
		Values.Empty();
		Textures.Empty();
	}
}

bool CMaterial::IsOverridden( const FString& Name ) const
{
	if ( !Parent )
	{
		return false;
	}
	if ( FindValue( Values, Name ) != nullptr )
	{
		return true;
	}
	for ( int32 Index = 0; Index < Textures.Num(); ++Index )
	{
		if ( Textures[Index].Name == Name )
		{
			return true;
		}
	}
	return false;
}

void CMaterial::LoadTextures( CAssetSubsystem& Assets )
{
	if ( Parent )
	{
		Parent->LoadTextures( Assets );
	}
	const TArray<FMaterialTextureSlot>& Slots = GetProgram().Textures;
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		const FMaterialTextureSlot& Slot = Slots[Index];
		if ( Slot.Source != EMaterialTextureSource::Parameter || Slot.DefaultPath.IsEmpty() || ( !Parent && FindEffectiveTexture( Slot.Name ) ) )
		{
			continue;
		}
		if ( !Parent )
		{
			const TSharedPtr<CTexture> Texture = Assets.LoadTexture( Slot.DefaultPath );
			if ( Texture )
			{
				SetTexture( Slot.Name, Texture );
			}
		}
	}
}

bool CMaterial::EvaluateCpu( const FMaterialEvalContext& Input, TArray<FVector4>& OutValues ) const
{
	FMaterialEvalContext Context = Input;
	BuildParamBlock( Context.ParamValues );
	return NMaterialEval::Evaluate( GetProgram(), Context, OutValues );
}

// ---- コレクション --------------------------------------------------------------------------------
void CMaterial::BindParameterCollection( const TSharedPtr<CMaterialParameterCollection>& ParameterCollection )
{
	if ( !ParameterCollection )
	{
		return;
	}
	for ( int32 Index = 0; Index < ParameterCollections.Num(); ++Index )
	{
		if ( ParameterCollections[Index]->GetName() == ParameterCollection->GetName() )
		{
			ParameterCollections[Index] = ParameterCollection;
			return;
		}
	}
	ParameterCollections.Add( ParameterCollection );
}

TSharedPtr<CMaterialParameterCollection> CMaterial::FindParameterCollection( const FString& ParameterCollectionName ) const
{
	for ( int32 Index = 0; Index < ParameterCollections.Num(); ++Index )
	{
		if ( ParameterCollections[Index]->GetName() == ParameterCollectionName )
		{
			return ParameterCollections[Index];
		}
	}
	if ( ParameterCollectionResolver )
	{
		TSharedPtr<CMaterialParameterCollection> Found = ParameterCollectionResolver( ParameterCollectionName );
		if ( Found )
		{
			return Found;
		}
	}
	return Parent ? Parent->FindParameterCollection( ParameterCollectionName ) : TSharedPtr<CMaterialParameterCollection>();
}

void CMaterial::BuildParamBlock( TArray<FVector4>& OutBlock ) const
{
	const FMaterialProgram& Program = GetProgram();
	OutBlock.Empty();
	OutBlock.SetNum( NMath::Max( 1, Program.Params.Num() ), FVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
	for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
	{
		const FMaterialParamSlot& Slot = Program.Params[Index];
		FVector4 Value = Slot.Default;
		if ( Slot.IsFromParameterCollection() )
		{
			const TSharedPtr<CMaterialParameterCollection> ParameterCollection = FindParameterCollection( Slot.ParameterCollection );
			if ( ParameterCollection )
			{
				ParameterCollection->TryGet( Slot.Name, Value );
			}
		}
		else
		{
			FindEffectiveValue( Slot, Value );
		}
		OutBlock[Slot.Index] = Value;
	}
}

// ---- GPU ----------------------------------------------------------------------------------------
bool CMaterial::Fail( const FString& Message )
{
	LastError = Message;
	return false;
}

bool CMaterial::PrepareShaders()
{
	FMaterialCompiled& Data = GetCompiled();
	if ( Data.bShadersTried )
	{
		return Data.bShadersOk;
	}
	Data.bShadersTried = true;
	Data.ShaderDiagnostics.Empty();
	if ( !Data.Program.bValid )
	{
		return false;
	}
	auto Report = [&]( EMaterialSeverity Severity, const FString& Message )
	{
		FMaterialDiagnostic Diagnostic;
		Diagnostic.Severity = Severity;
		Diagnostic.Message = Message;
		Data.ShaderDiagnostics.Add( Diagnostic );
	};
	if ( !Data.Capabilities.bPixelShaders )
	{
		Report( EMaterialSeverity::Info, "this backend cannot run pixel shaders: the material is not drawn" );
		return false;
	}
	FString Message;
	Data.Pixel = CShader::Compile( EShaderStage::Pixel, Data.Hlsl, "main", &Message );
	if ( !Data.Pixel )
	{
		Report( EMaterialSeverity::Error, FString( "the pixel shader could not be compiled: " ) + Message );
		return false;
	}
	if ( Data.Program.Domain == EMaterialDomain::Surface )
	{
		Data.Vertex = CShader::Compile( EShaderStage::Vertex, NMaterialHlsl::GetSurfaceVertexSource(), "main", &Message );
		if ( !Data.Vertex )
		{
			Report( EMaterialSeverity::Error, FString( "the surface vertex shader could not be compiled: " ) + Message );
			return false;
		}
	}
	Data.bShadersOk = true;
	return true;
}

void CMaterial::BindTextures( FDrawContext& Draw, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth ) const
{
	const TArray<FMaterialTextureSlot>& Slots = GetProgram().Textures;
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		const FMaterialTextureSlot& Slot = Slots[Index];
		TSharedPtr<IImage> Image;
		switch ( Slot.Source )
		{
		case EMaterialTextureSource::SceneColor:
			Image = Scene;
			break;
		case EMaterialTextureSource::SceneDepth:
			Image = Depth;
			break;
		default:
			Image = FindEffectiveTexture( Slot.Name );
			break;
		}
		if ( Image )
		{
			Draw.ShaderTexture( Slot.Register, Image->GetImageHandle() );
		}
	}
}

void CMaterial::UnbindTextures( FDrawContext& Draw ) const
{
	const TArray<FMaterialTextureSlot>& Slots = GetProgram().Textures;
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		Draw.StopUsingShaderTexture( Slots[Index].Register );
	}
}

bool CMaterial::DrawRect( FDrawContext& Draw, float32 X, float32 Y, float32 Width, float32 Height, const FColor32& Tint, const FMaterialFrameState& State )
{
	const EMaterialDomain Domain = GetDomain();
	if ( Domain != EMaterialDomain::UI && Domain != EMaterialDomain::Particle )
	{
		return Fail( "DrawRect is for UI and Particle materials" );
	}
	if ( !IsValid() || !PrepareShaders() )
	{
		return Fail( "the material has errors or its shaders are not available" );
	}
	if ( Width <= 0.0f || Height <= 0.0f )
	{
		return false;
	}
	TArray<FVector4> Block;
	BuildParamBlock( Block );
	FMaterialFrameConstants Frame;
	Frame.Time = FVector4( State.Time, State.DeltaTime, State.NearPlane, State.FarPlane );
	Frame.Rect = FVector4( Width, Height, 0.0f, 0.0f );
	Frame.State = FVector4( State.Hover, State.Press, State.Value, State.Enabled );
	FMaterialCompiled& Data = GetCompiled();
	Draw.ShaderConstants( EShaderStage::Pixel, 4, Block.GetData(), Block.Num() * static_cast<int32>( sizeof( FVector4 ) ) );
	Draw.ShaderConstants( EShaderStage::Pixel, 5, Frame );
	BindTextures( Draw, TSharedPtr<IImage>(), TSharedPtr<IImage>() );
	Draw.UsePixelShader( Data.Pixel );
	const bool bOk = Draw.ShaderRect( X, Y, Width, Height, Tint );
	Draw.StopUsingPixelShader();
	UnbindTextures( Draw );
	LastError.Empty();
	return bOk;
}

bool CMaterial::BeginParticles( FDrawContext& Draw, const FMaterialFrameState& State ) const
{
	// 並列に記録されるので、ここでは、状態を変えない（シェーダーは、先に PrepareShaders で作っておく）
	if ( GetDomain() != EMaterialDomain::Particle || !IsValid() || !IsReady() )
	{
		return false;
	}
	TArray<FVector4> Block;
	BuildParamBlock( Block );
	FMaterialFrameConstants Frame;
	Frame.Time = FVector4( State.Time, State.DeltaTime, State.NearPlane, State.FarPlane );
	Frame.Rect = FVector4( 1.0f, 1.0f, 0.0f, 0.0f );
	Frame.State = FVector4( State.Hover, State.Press, State.Value, State.Enabled );
	const FMaterialCompiled& Data = GetCompiled();
	Draw.ShaderConstants( EShaderStage::Pixel, 4, Block.GetData(), Block.Num() * static_cast<int32>( sizeof( FVector4 ) ) );
	Draw.ShaderConstants( EShaderStage::Pixel, 5, Frame );
	BindTextures( Draw, TSharedPtr<IImage>(), TSharedPtr<IImage>() );
	Draw.UsePixelShader( Data.Pixel );
	return true;
}

bool CMaterial::DrawParticleQuad( FDrawContext& Draw, const FDrawQuad3DData& Quad, const FMaterialParticleState& Particle ) const
{
	return Draw.ShaderQuad3D( Quad, FVector4( Particle.Life, Particle.Age, Particle.Random, Particle.Speed ) );
}

void CMaterial::EndParticles( FDrawContext& Draw ) const
{
	Draw.StopUsingPixelShader();
	UnbindTextures( Draw );
}

bool CMaterial::DrawPostProcess( FDrawContext& Draw, float32 X, float32 Y, float32 Width, float32 Height, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth, const FMaterialFrameState& State )
{
	if ( GetDomain() != EMaterialDomain::PostProcess )
	{
		return Fail( "DrawPostProcess is for PostProcess materials" );
	}
	if ( !IsValid() || !PrepareShaders() )
	{
		return Fail( "the material has errors or its shaders are not available" );
	}
	if ( !Scene || Width <= 0.0f || Height <= 0.0f )
	{
		return Fail( "a PostProcess material needs the scene image and a rectangle with a size" );
	}
	TArray<FVector4> Block;
	BuildParamBlock( Block );
	FMaterialFrameConstants Frame;
	Frame.Time = FVector4( State.Time, State.DeltaTime, State.NearPlane, State.FarPlane );
	Frame.Rect = FVector4( Width, Height, 0.0f, 0.0f );
	Frame.State = FVector4( State.Hover, State.Press, State.Value, State.Enabled );
	FMaterialCompiled& Data = GetCompiled();

	// 全体にかける効果なので、奥行きは比べない。画像の外を読む所は、端の色を伸ばす
	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	Draw.Depth( NoDepth );
	Draw.Filter( EDrawFilter::Bilinear );
	Draw.TextureAddress( EDrawTextureAddress::Clamp );
	Draw.ShaderConstants( EShaderStage::Pixel, 4, Block.GetData(), Block.Num() * static_cast<int32>( sizeof( FVector4 ) ) );
	Draw.ShaderConstants( EShaderStage::Pixel, 5, Frame );
	BindTextures( Draw, Scene, Depth );
	Draw.UsePixelShader( Data.Pixel );
	const bool bOk = Draw.ShaderRect( X, Y, Width, Height );
	Draw.StopUsingPixelShader();
	UnbindTextures( Draw );
	LastError.Empty();
	return bOk;
}

bool CMaterial::BeginSurface( FDrawContext& Draw, const FMaterialSceneState& Scene )
{
	if ( GetDomain() != EMaterialDomain::Surface )
	{
		return Fail( "BeginSurface is for Surface materials" );
	}
	if ( !IsValid() || !PrepareShaders() )
	{
		return Fail( "the material has errors or its shaders are not available" );
	}
	TArray<FVector4> Block;
	BuildParamBlock( Block );
	FMaterialSceneConstants Constants;
	Constants.ViewProjection = Scene.ViewProjection;
	Constants.LightDirection = FVector4( Scene.LightDirection, 0.0f );
	Constants.CameraPosition = FVector4( Scene.CameraPosition, 1.0f );
	Constants.Time = FVector4( Scene.Time, Scene.NearPlane, Scene.FarPlane, 0.0f );
	FMaterialCompiled& Data = GetCompiled();
	Draw.ShaderConstants( EShaderStage::Vertex, 4, Constants );
	Draw.ShaderConstants( EShaderStage::Pixel, 4, Constants );
	Draw.ShaderConstants( EShaderStage::Pixel, 6, Block.GetData(), Block.Num() * static_cast<int32>( sizeof( FVector4 ) ) );
	BindTextures( Draw, TSharedPtr<IImage>(), TSharedPtr<IImage>() );
	Draw.UseVertexShader( Data.Vertex );
	Draw.UsePixelShader( Data.Pixel );
	LastError.Empty();
	return true;
}

bool CMaterial::DrawSurfaceMesh( FDrawContext& Draw, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint )
{
	FMaterialObjectConstants Object;
	Object.World = World;
	Object.Color = ToVector( Tint );
	Object.Options = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	Draw.ShaderConstants( EShaderStage::Vertex, 5, Object );
	Draw.ShaderConstants( EShaderStage::Pixel, 5, Object );
	return Draw.PrimitiveIndexedShader3D( Vertices, NumVertices, Indices, NumIndices, EDrawPrimitive::Triangles );
}

void CMaterial::EndSurface( FDrawContext& Draw )
{
	Draw.StopUsingVertexShader();
	Draw.StopUsingPixelShader();
	UnbindTextures( Draw );
}
