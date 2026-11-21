#include "GBuffer.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

namespace
{
	// 描画先の名前につける接尾辞（EGBufferTarget の並び）
	const char* const TargetSuffixes[CGBuffer::NumTargets] = { "-albedo", "-normal", "-depth", "-emission" };

	const TSharedPtr<CRenderTarget> NoTarget;
}

FRenderTargetDesc CGBuffer::MakeDesc( EGBufferTarget Target, int32 Width, int32 Height )
{
	FRenderTargetDesc Desc;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.bAlpha = false;
	Desc.bDepth = false;
	switch ( Target )
	{
	case EGBufferTarget::Albedo:
		// 3D の描画の奥行きバッファは、ジオメトリのパスで描く先（この画像）が持つ
		Desc.bDepth = true;
		Desc.Format = ERenderTargetFormat::Color8;
		Desc.Channels = 4;
		break;
	case EGBufferTarget::Normal:
		Desc.Format = ERenderTargetFormat::Float16;
		Desc.Channels = 4;
		break;
	case EGBufferTarget::Depth:
		Desc.Format = ERenderTargetFormat::Float32;
		Desc.Channels = 1;
		break;
	case EGBufferTarget::Emission:
		Desc.Format = ERenderTargetFormat::Float16;
		Desc.Channels = 4;
		break;
	}
	return Desc;
}

bool CGBuffer::Create( CAssetSubsystem& Assets, const FString& Name, int32 InWidth, int32 InHeight )
{
	Release();
	TSharedPtr<CRenderTarget> Made[NumTargets];
	for ( int32 Index = 0; Index < NumTargets; ++Index )
	{
		const FRenderTargetDesc Desc = MakeDesc( static_cast<EGBufferTarget>( Index ), InWidth, InHeight );
		if ( !Desc.IsValid() )
		{
			return false;
		}
		Made[Index] = Assets.CreateRenderTarget( Name + TargetSuffixes[Index], Desc );
		if ( !Made[Index].IsValid() )
		{
			return false;
		}
	}
	return Adopt( Made );
}

bool CGBuffer::Adopt( const TSharedPtr<CRenderTarget> ( &InTargets )[NumTargets] )
{
	if ( !InTargets[0].IsValid() )
	{
		return false;
	}
	const int32 InWidth = InTargets[0]->GetDesc().Width;
	const int32 InHeight = InTargets[0]->GetDesc().Height;
	for ( int32 Index = 0; Index < NumTargets; ++Index )
	{
		if ( !InTargets[Index].IsValid() )
		{
			return false;
		}
		const FRenderTargetDesc& Actual = InTargets[Index]->GetDesc();
		const FRenderTargetDesc Expected = MakeDesc( static_cast<EGBufferTarget>( Index ), InWidth, InHeight );
		if ( !Expected.IsValid() || Actual.Width != Expected.Width || Actual.Height != Expected.Height || Actual.bAlpha != Expected.bAlpha || Actual.bDepth != Expected.bDepth ||
			Actual.MultiSamples != Expected.MultiSamples || Actual.Format != Expected.Format || Actual.Channels != Expected.Channels )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < NumTargets; ++Index )
	{
		Targets[Index] = InTargets[Index];
	}
	Width = InWidth;
	Height = InHeight;
	return true;
}

void CGBuffer::Release()
{
	for ( TSharedPtr<CRenderTarget>& Target : Targets )
	{
		Target.Reset();
	}
	Width = 0;
	Height = 0;
}

bool CGBuffer::IsValid() const
{
	for ( const TSharedPtr<CRenderTarget>& Target : Targets )
	{
		if ( !Target.IsValid() )
		{
			return false;
		}
	}
	return true;
}

const TSharedPtr<CRenderTarget>& CGBuffer::GetTarget( EGBufferTarget Target ) const
{
	const int32 Index = static_cast<int32>( Target );
	return Index >= 0 && Index < NumTargets ? Targets[Index] : NoTarget;
}

bool CGBuffer::BeginGeometryPass( FDrawContext& CommandList ) const
{
	if ( !IsValid() )
	{
		return false;
	}
	// 何も描かれていない画素の印: 法線 0・深度 1（Far）・自分で光る色 0。地の色は、描く先（奥行きバッファつき）なので、そのまま消す
	const EGBufferTarget Cleared[] = { EGBufferTarget::Normal, EGBufferTarget::Depth, EGBufferTarget::Emission };
	for ( const EGBufferTarget Target : Cleared )
	{
		CommandList.PushTarget( GetTarget( Target ) );
		CommandList.Clear( Target == EGBufferTarget::Depth ? FColor32( 255, 255, 255, 255 ) : FColor32( 0, 0, 0, 0 ) );
		CommandList.PopTarget();
	}
	if ( !CommandList.PushTarget( GetTarget( EGBufferTarget::Albedo ) ) )
	{
		return false;
	}
	CommandList.Clear( FColor32( 0, 0, 0, 255 ) );
	CommandList.ClearDepth();
	CommandList.ShaderOutputTarget( 1, GetTarget( EGBufferTarget::Normal ) );
	CommandList.ShaderOutputTarget( 2, GetTarget( EGBufferTarget::Depth ) );
	CommandList.ShaderOutputTarget( 3, GetTarget( EGBufferTarget::Emission ) );
	return true;
}

void CGBuffer::EndGeometryPass( FDrawContext& CommandList ) const
{
	// 書きながら読まない（このあと、4 枚を画像として読む）
	CommandList.StopUsingShaderOutputTarget( 3 );
	CommandList.StopUsingShaderOutputTarget( 2 );
	CommandList.StopUsingShaderOutputTarget( 1 );
	CommandList.StopUsingVertexShader();
	CommandList.StopUsingPixelShader();
	CommandList.PopTarget();
}

bool CGBuffer::UseForLighting( FDrawContext& CommandList ) const
{
	if ( !IsValid() )
	{
		return false;
	}
	CommandList.Filter( EDrawFilter::Nearest );
	CommandList.TextureAddress( EDrawTextureAddress::Clamp );
	for ( int32 Index = 0; Index < NumTargets; ++Index )
	{
		CommandList.ShaderTexture( Index, Targets[Index] );
	}
	return true;
}
