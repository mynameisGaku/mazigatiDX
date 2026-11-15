// SPDX-License-Identifier: MIT
#include "TextureAssetStore.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

CTextureAssetStore::CTextureAssetStore( IRenderTextureResources& InResources ) : Resources( &InResources )
{
}

CTextureAssetStore::~CTextureAssetStore()
{
	Shutdown();
}

bool CTextureAssetStore::Initialize( IRenderTextureResources& InResources )
{
	if ( Resources )
	{
		return false;
	}
	Resources = &InResources;
	return true;
}

void CTextureAssetStore::Shutdown()
{
	Reset();
	Resources = nullptr;
}

int32 CTextureAssetStore::Create( int32 Width, int32 Height, const FColor32* Pixels )
{
	if ( !Resources || !Pixels || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024
		|| Textures.Num() >= MaxHandles || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	const FTextureResourceHandle Resource = Resources->CreateTextureRGBA8( Width, Height, Pixels ); // 番号の発行前に資源を完成させる。
	if ( !Resource.IsValid() )
	{
		return -1;
	}
	const int32 Handle = NextHandle++; // GPUのアドレスや番号をアセット番号へ流用しない。
	Textures.Add( Handle, Resource );
	return Handle;
}

int32 CTextureAssetStore::CreateRenderTarget( int32 Width, int32 Height, bool bDepth, ERenderTargetColorFormat Format, FColor32 ClearColor )
{
	if ( !Resources || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || Textures.Num() >= MaxHandles || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	FTextureResourceHandle Color; // 番号の発行前に資源を完成させる。
	if ( Format == ERenderTargetColorFormat::RGBA16F )
	{
		Color = Resources->CreateRenderTargetRGBA16F( Width, Height, FColor( ClearColor.r / 255.0f, ClearColor.g / 255.0f, ClearColor.b / 255.0f, ClearColor.a / 255.0f ) );
	}
	else if ( Format == ERenderTargetColorFormat::R32Float )
	{
		Color = Resources->CreateRenderTargetR32Float( Width, Height, ClearColor.r / 255.0f );
	}
	else
	{
		Color = Resources->CreateRenderTargetRGBA8( Width, Height, ClearColor );
	}
	if ( !Color.IsValid() )
	{
		return -1;
	}
	FTextureResourceHandle Depth; // 3Dを描く描画先だけが持つ。
	if ( bDepth )
	{
		Depth = Resources->CreateDepthTextureD32( Width, Height );
		if ( !Depth.IsValid() )
		{
			Resources->ReleaseTexture( Color );
			return -1;
		}
	}
	const int32 Handle = NextHandle++; // GPUのアドレスや番号をアセット番号へ流用しない。
	Textures.Add( Handle, Color );
	TargetDepths.Add( Handle, Depth );
	TargetFormats.Add( Handle, Format );
	TargetSizes.Add( Handle, ( static_cast<int64>( Width ) << 32 ) | static_cast<int64>( Height ) );
	return Handle;
}

bool CTextureAssetStore::ReadRenderTarget( int32 Handle, TArray<FColor32>& OutPixels, int32& OutWidth, int32& OutHeight )
{
	const auto* Resource = Textures.Find( Handle ); // 描画先として作った登録だけを読む。
	const auto* Format = TargetFormats.Find( Handle );
	if ( !Resource || !Resources || !Format || !TargetDepths.Find( Handle ) )
	{
		return false;
	}
	const auto ToByte = []( float32 Value ) { return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f + 0.5f ); }; // 0〜1に収める。
	if ( *Format == ERenderTargetColorFormat::RGBA16F )
	{
		TArray<FColor> Values;
		int32 Width = 0, Height = 0;
		if ( !Resources->ReadTextureRGBA16F( *Resource, Values, Width, Height ) )
		{
			return false;
		}
		TArray<FColor32> Pixels;
		Pixels.SetNum( Values.Num() );
		for ( int32 Index = 0; Index < Values.Num(); ++Index )
		{
			Pixels[Index] = FColor32( ToByte( Values[Index].r ), ToByte( Values[Index].g ), ToByte( Values[Index].b ), ToByte( Values[Index].a ) );
		}
		OutPixels = MoveTemp( Pixels );
		OutWidth = Width;
		OutHeight = Height;
		return true;
	}
	if ( *Format == ERenderTargetColorFormat::R32Float )
	{
		TArray<float32> Values;
		int32 Width = 0, Height = 0;
		if ( !Resources->ReadTextureR32Float( *Resource, Values, Width, Height ) )
		{
			return false;
		}
		TArray<FColor32> Pixels;
		Pixels.SetNum( Values.Num() );
		for ( int32 Index = 0; Index < Values.Num(); ++Index )
		{
			Pixels[Index] = FColor32( ToByte( Values[Index] ), 0, 0, 255 ); // 1 チャンネルは、赤だけ。
		}
		OutPixels = MoveTemp( Pixels );
		OutWidth = Width;
		OutHeight = Height;
		return true;
	}
	return Resources->ReadTextureRGBA8( *Resource, OutPixels, OutWidth, OutHeight );
}

bool CTextureAssetStore::GetShaderFormat( int32 Handle, EComputeTextureFormat& Out ) const
{
	if ( !Textures.Find( Handle ) )
	{
		return false;
	}
	const auto* Format = TargetFormats.Find( Handle );
	Out = !Format || *Format == ERenderTargetColorFormat::RGBA8 ? EComputeTextureFormat::RGBA8 : *Format == ERenderTargetColorFormat::RGBA16F ? EComputeTextureFormat::RGBA16F : EComputeTextureFormat::R32Float;
	return true;
}

bool CTextureAssetStore::GetRenderTargetDrawView( int32 Handle, FRenderTargetDrawView& Out )
{
	const auto* Resource = Textures.Find( Handle ); // 色の描画先。
	const auto* Depth = TargetDepths.Find( Handle ); // 鍵が無ければ、ふつうの画像。
	const auto* Size = TargetSizes.Find( Handle ); // 大きさは、作ったときの値（浮動小数点の描画先は、viewを借りられない）。
	if ( !Resource || !Depth || !Resources || !Size )
	{
		return false;
	}
	Out.Color = *Resource;
	Out.Depth = *Depth;
	if ( const auto* Format = TargetFormats.Find( Handle ) )
	{
		Out.Format = *Format;
	}
	Out.Width = static_cast<int32>( *Size >> 32 );
	Out.Height = static_cast<int32>( *Size & 0xFFFFFFFF );
	return true;
}

bool CTextureAssetStore::Update( int32 Handle, int32 Width, int32 Height, const FColor32* Pixels )
{
	const auto* Resource = Textures.Find( Handle ); // 有効な登録だけを更新する。
	return Resource && Resources && Pixels && Resources->UpdateTextureRGBA8( *Resource, Width, Height, Pixels );
}

void CTextureAssetStore::Delete( int32 Handle )
{
	if ( const auto* Resource = Textures.Find( Handle ) )
	{
		if ( Resources )
		{
			Resources->ReleaseTexture( *Resource );
			if ( const auto* Depth = TargetDepths.Find( Handle ) )
			{
				if ( Depth->IsValid() )
				{
					Resources->ReleaseTexture( *Depth );
				}
			}
		}
		TargetDepths.Remove( Handle );
		TargetFormats.Remove( Handle );
		TargetSizes.Remove( Handle );
		Textures.Remove( Handle );
	}
}

void CTextureAssetStore::Reset()
{
	for ( const auto& Pair : Textures )
	{
		if ( Resources )
		{
			Resources->ReleaseTexture( Pair.Value );
		}
	}
	for ( const auto& Pair : TargetDepths )
	{
		if ( Resources && Pair.Value.IsValid() )
		{
			Resources->ReleaseTexture( Pair.Value );
		}
	}
	TargetDepths.Reset();
	TargetFormats.Reset();
	TargetSizes.Reset();
	Textures.Reset();
}

int32 CTextureAssetStore::Num() const
{
	return Textures.Num();
}

bool CTextureAssetStore::GetSize( int32 Handle, int32& OutWidth, int32& OutHeight ) const
{
	if ( const auto* TargetSize = TargetSizes.Find( Handle ) ) // 描画先は、作ったときの大きさを返す。
	{
		OutWidth = static_cast<int32>( *TargetSize >> 32 );
		OutHeight = static_cast<int32>( *TargetSize & 0xFFFFFFFF );
		return true;
	}
	const auto* Resource = Textures.Find( Handle ); // 自分の登録だけを問い合わせる。
	FTextureDrawView View; // 失敗時に出力を保つ。
	if ( !Resource || !Resources || !Resources->GetTextureResourceView( *Resource, View ) )
	{
		return false;
	}
	OutWidth = View.Width;
	OutHeight = View.Height;
	return true;
}

bool CTextureAssetStore::GetResource( int32 Handle, FTextureResourceHandle& Out ) const
{
	const auto* Resource = Textures.Find( Handle ); // 自分の登録だけを問い合わせる。
	if ( !Resource )
	{
		return false;
	}
	Out = *Resource;
	return true;
}

bool CTextureAssetStore::GetTextureDrawView( int32 Handle, FTextureDrawView& Out )
{
	const auto* Resource = Textures.Find( Handle ); // 解放した番号を実行先へ渡さない。
	return Resource && Resources && Resources->GetTextureResourceView( *Resource, Out );
}
