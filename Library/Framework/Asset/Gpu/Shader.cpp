// SPDX-License-Identifier: MIT
#include "Shader.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"

namespace
{
	// 失敗の理由を、呼び出し側に渡す（OutMessage が nullptr なら捨てる）
	void SetMessage( FString* OutMessage, const FString& Message )
	{
		if ( OutMessage != nullptr )
		{
			*OutMessage = Message;
		}
	}
}

TSharedPtr<CShader> CShader::Create( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() || Code == nullptr || NumBytes < 1 )
	{
		return nullptr;
	}
	const int32 Shader = Backend->MakeShader( Stage, Code, NumBytes );
	if ( Shader < 0 )
	{
		return nullptr;
	}
	return TSharedPtr<CShader>( new CShader( Shader, Stage ) );
}

TSharedPtr<CShader> CShader::LoadFile( EShaderStage Stage, const FString& Path )
{
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Path, Bytes ) || Bytes.IsEmpty() )
	{
		return nullptr;
	}
	return Create( Stage, Bytes.GetData(), Bytes.Num() );
}

TSharedPtr<CShader> CShader::Compile( EShaderStage Stage, const FString& Source, const char* EntryPoint, FString* OutMessage, const char* ShaderModel )
{
	SetMessage( OutMessage, FString() );
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() )
	{
		SetMessage( OutMessage, "the shader can be made only on the main thread" );
		return nullptr;
	}
	if ( Source.IsEmpty() || EntryPoint == nullptr || ShaderModel == nullptr )
	{
		SetMessage( OutMessage, "the source, the entry point and the shader model are required" );
		return nullptr;
	}
	TArray<uint8> Code;
	FString Message;
	if ( !Backend->CompileShader( Stage, Source.GetData(), Source.Len(), "shader.hlsl", EntryPoint, ShaderModel, Code, Message ) || Code.IsEmpty() )
	{
		SetMessage( OutMessage, Message );
		return nullptr;
	}
	TSharedPtr<CShader> Shader = Create( Stage, Code.GetData(), Code.Num() );
	if ( Shader == nullptr )
	{
		SetMessage( OutMessage, "the compiled shader cannot be used" );
	}
	return Shader;
}

TSharedPtr<CShader> CShader::CompileFile( EShaderStage Stage, const FString& Path, const char* EntryPoint, FString* OutMessage, const char* ShaderModel )
{
	SetMessage( OutMessage, FString() );
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() )
	{
		SetMessage( OutMessage, "the shader can be made only on the main thread" );
		return nullptr;
	}
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Path, Bytes ) || Bytes.IsEmpty() )
	{
		SetMessage( OutMessage, FString( "cannot read the file: " ) + Path );
		return nullptr;
	}
	if ( EntryPoint == nullptr || ShaderModel == nullptr )
	{
		SetMessage( OutMessage, "the entry point and the shader model are required" );
		return nullptr;
	}
	TArray<uint8> Code;
	FString Message;
	if ( !Backend->CompileShader( Stage, reinterpret_cast<const char*>( Bytes.GetData() ), Bytes.Num(), Path.GetData(), EntryPoint, ShaderModel, Code, Message ) || Code.IsEmpty() )
	{
		SetMessage( OutMessage, Message );
		return nullptr;
	}
	TSharedPtr<CShader> Shader = Create( Stage, Code.GetData(), Code.Num() );
	if ( Shader == nullptr )
	{
		SetMessage( OutMessage, "the compiled shader cannot be used" );
	}
	return Shader;
}

CShader::~CShader()
{
	NAssetService::ReleaseShader( Shader );
}

FShaderHandle CShader::GetShaderHandle() const
{
	return FShaderHandle( Shader, Stage );
}
