#include "DeferredSources.h"

#include "Library/Core/Collection/String.h"
#include "Library/Core/File/File.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Render/Deferred/DeferredShaders.h"

DEFINE_LOG_CATEGORY_STATIC( LogDeferredSample, Log )

namespace
{
	// サンプルの HLSL は、サンプルの素材（Samples/Assets/Deferred）のファイルで、実行時に読む（製品のライブラリへは埋め込まない）。
	// G-buffer の書き出し・読み出しの部品だけが、製品の EngineContent/Shaders/Deferred にあり、NDeferredShaders が渡す。
	constexpr const char* SourceFolder = "Deferred/";

	// 素材のファイル 1 つを、文字列として読む（先頭の BOM は外す）。読めなければ、理由をログに出して false
	bool ReadSource( CAssetSubsystem& Assets, const char* Name, FString& OutText )
	{
		const TOptional<FString> Path = Assets.ResolvePath( FString( SourceFolder ) + Name );
		TArray<uint8> Bytes;
		if ( !Path.IsSet() || !NFile::ReadAllBytes( Path.Get( FString() ), Bytes ) || Bytes.IsEmpty() )
		{
			LOG( LogDeferredSample, Error, "Cannot read the shader file Assets/%s%s", SourceFolder, Name );
			return false;
		}
		int32 Offset = 0;
		if ( Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF )
		{
			Offset = 3;
		}
		OutText = FString( reinterpret_cast<const char*>( Bytes.GetData() ) + Offset, Bytes.Num() - Offset );
		return true;
	}

	// 1 つコンパイルする。失敗したら、理由をログに出して、nullptr を返す
	TSharedPtr<CShader> CompileOne( EShaderStage Stage, const FString& Source, const char* Name )
	{
		FString Message;
		TSharedPtr<CShader> Compiled = CShader::Compile( Stage, Source, "main", &Message );
		if ( !Compiled.IsValid() )
		{
			LOG( LogDeferredSample, Error, "Cannot compile the shader %s: %s", Name, Message.GetData() );
		}
		return Compiled;
	}
}

bool NDeferredSources::CompileAll( FDeferredDemoState& State, CAssetSubsystem& Assets )
{
	// 共通の部品（場面の定数・光・見せ方、物の入出力）と、シェーダーごとの本体
	FString Common;
	FString Geometry;
	FString Vertex;
	FString ForwardObject;
	FString ForwardGround;
	FString GeometryObject;
	FString GeometryGround;
	FString Lighting;
	FString Composite;
	FString Transparent;
	if ( !ReadSource( Assets, "SampleCommon.hlsli", Common ) || !ReadSource( Assets, "SampleGeometry.hlsli", Geometry ) || !ReadSource( Assets, "SampleVertex.hlsl", Vertex ) ||
		!ReadSource( Assets, "SampleForwardObject.hlsl", ForwardObject ) || !ReadSource( Assets, "SampleForwardGround.hlsl", ForwardGround ) ||
		!ReadSource( Assets, "SampleGeometryObject.hlsl", GeometryObject ) || !ReadSource( Assets, "SampleGeometryGround.hlsl", GeometryGround ) ||
		!ReadSource( Assets, "SampleLighting.hlsl", Lighting ) || !ReadSource( Assets, "SampleComposite.hlsl", Composite ) || !ReadSource( Assets, "SampleTransparent.hlsl", Transparent ) )
	{
		return false;
	}

	// 必要な部品を、シェーダーごとに、この順で連ねる
	const FString Scene = Common + Geometry;
	const FString Write = FString( NDeferredShaders::GetGBufferWriteSource() ) + Scene;
	const FString Read = FString( NDeferredShaders::GetGBufferReadSource() );
	State.SceneVertex = CompileOne( EShaderStage::Vertex, Scene + Vertex, "scene vertex" );
	State.ForwardObjectPixel = CompileOne( EShaderStage::Pixel, Scene + ForwardObject, "forward object" );
	State.ForwardGroundPixel = CompileOne( EShaderStage::Pixel, Scene + ForwardGround, "forward ground" );
	State.GeometryObjectPixel = CompileOne( EShaderStage::Pixel, Write + GeometryObject, "geometry object" );
	State.GeometryGroundPixel = CompileOne( EShaderStage::Pixel, Write + GeometryGround, "geometry ground" );
	State.LightingPixel = CompileOne( EShaderStage::Pixel, Read + Common + Lighting, "lighting" );
	State.CompositePixel = CompileOne( EShaderStage::Pixel, Composite, "composite" );
	State.TransparentPixel = CompileOne( EShaderStage::Pixel, Read + Scene + Transparent, "transparent" );
	return State.SceneVertex.IsValid() && State.ForwardObjectPixel.IsValid() && State.ForwardGroundPixel.IsValid() && State.GeometryObjectPixel.IsValid() && State.GeometryGroundPixel.IsValid() && State.LightingPixel.IsValid() && State.CompositePixel.IsValid() && State.TransparentPixel.IsValid();
}
