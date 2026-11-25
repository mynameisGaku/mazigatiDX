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

int32 CTextureAssetStore::Create( int32 Width, int32 Height, const FColor32* Pixels, bool bMipmaps, ETextureMipMode MipMode )
{
	if ( !Resources || !Pixels || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024
		|| ( MipMode != ETextureMipMode::Color && MipMode != ETextureMipMode::LinearData ) || Textures.Num() >= MaxHandles || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	const FTextureResourceHandle Resource = !bMipmaps ? Resources->CreateTextureRGBA8( Width, Height, Pixels ) : ( MipMode == ETextureMipMode::LinearData ? Resources->CreateTextureRGBA8DataMipped( Width, Height, Pixels ) : Resources->CreateTextureRGBA8Mipped( Width, Height, Pixels ) ); // 番号の発行前に資源を完成させる。
	if ( !Resource.IsValid() )
	{
		return -1;
	}
	const int32 Handle = NextHandle++; // GPUのアドレスや番号をアセット番号へ流用しない。
	Textures.Add( Handle, Resource );
	return Handle;
}

int32 CTextureAssetStore::CreateRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	int32 BaseWidth = 0, BaseHeight = 0; // 元の画像の大きさ。
	if ( !Resources || Source < 0 || TargetDepths.Find( Source ) || Regions.Find( Source ) || !GetSize( Source, BaseWidth, BaseHeight ) || X < 0 || Y < 0 || Width <= 0 || Height <= 0
		|| static_cast<int64>( X ) + Width > BaseWidth || static_cast<int64>( Y ) + Height > BaseHeight || Textures.Num() + Regions.Num() >= MaxHandles || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	FRegion Region;
	Region.Source = Source;
	Region.X = X;
	Region.Y = Y;
	Region.Width = Width;
	Region.Height = Height;
	const int32 Handle = NextHandle++; // 元の画像の番号は流用しない。
	Regions.Add( Handle, Region );
	return Handle;
}

bool CTextureAssetStore::GetRegionUvBounds( int32 Handle, float32 ( &OutUV )[4] )
{
	const auto* Region = Regions.Find( Handle );
	const auto* Resource = Region ? Textures.Find( Region->Source ) : nullptr;
	FTextureDrawView Base; // 元の画像。
	if ( !Resource || !Resources || !Resources->GetTextureResourceView( *Resource, Base ) || Base.Width <= 0 || Base.Height <= 0 )
	{
		return false;
	}
	const float32 Width = static_cast<float32>( Base.Width ), Height = static_cast<float32>( Base.Height ); // 1画素の領域でも、1点を正しく読めるよう、半画素だけ内側へ寄せる。
	OutUV[0] = ( static_cast<float32>( Region->X ) + 0.5f ) / Width;
	OutUV[1] = ( static_cast<float32>( Region->Y ) + 0.5f ) / Height;
	OutUV[2] = ( static_cast<float32>( Region->X + Region->Width ) - 0.5f ) / Width;
	OutUV[3] = ( static_cast<float32>( Region->Y + Region->Height ) - 0.5f ) / Height;
	return true;
}

bool CTextureAssetStore::AcquireScratchTarget( int32 Width, int32 Height, int32& OutHandle )
{
	const int64 Key = ( static_cast<int64>( Width ) << 32 ) | static_cast<int64>( Height );
	if ( const int32* Found = ScratchTargets.Find( Key ) )
	{
		if ( Textures.Find( *Found ) )
		{
			OutHandle = *Found;
			return true;
		}
		ScratchTargets.Remove( Key );
	}
	if ( ScratchTargets.Num() >= 8 )
	{
		return false;
	}
	const int32 Handle = CreateRenderTarget( Width, Height, false, ERenderTargetColorFormat::RGBA8, FColor32( 0, 0, 0, 0 ) );
	if ( Handle < 0 )
	{
		return false;
	}
	ScratchTargets.Add( Key, Handle );
	OutHandle = Handle;
	return true;
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
	if ( Regions.Remove( Handle ) )
	{
		return; // 派生画像は、GPU資源を持たない。
	}
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
	Regions.Reset();
	ScratchTargets.Reset();
	Textures.Reset();
}

int32 CTextureAssetStore::Num() const
{
	return Textures.Num();
}

bool CTextureAssetStore::GetSize( int32 Handle, int32& OutWidth, int32& OutHeight ) const
{
	if ( const auto* Region = Regions.Find( Handle ) ) // 派生画像は、矩形の大きさ（元の画像が生きているあいだだけ）。
	{
		if ( !Textures.Find( Region->Source ) )
		{
			return false;
		}
		OutWidth = Region->Width;
		OutHeight = Region->Height;
		return true;
	}
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
	const auto* Region = Regions.Find( Handle ); // 派生画像も、元のGPU資源をシェーダーから借りる。
	const auto* Resource = Textures.Find( Region ? Region->Source : Handle );
	if ( !Resource )
	{
		return false;
	}
	Out = *Resource;
	return true;
}

bool CTextureAssetStore::GetTextureDrawView( int32 Handle, FTextureDrawView& Out )
{
	if ( const auto* Region = Regions.Find( Handle ) ) // 派生画像は、元の画像の資源に、矩形のUVを添える。
	{
		const auto* SourceResource = Textures.Find( Region->Source );
		FTextureDrawView Base;
		if ( !SourceResource || !Resources || !Resources->GetTextureResourceView( *SourceResource, Base ) || Base.Width <= 0 || Base.Height <= 0 )
		{
			return false;
		}
		Out = Base;
		Out.Width = Region->Width;
		Out.Height = Region->Height;
		Out.UvRect[0] = static_cast<float32>( Region->X ) / static_cast<float32>( Base.Width );
		Out.UvRect[1] = static_cast<float32>( Region->Y ) / static_cast<float32>( Base.Height );
		Out.UvRect[2] = static_cast<float32>( Region->X + Region->Width ) / static_cast<float32>( Base.Width );
		Out.UvRect[3] = static_cast<float32>( Region->Y + Region->Height ) / static_cast<float32>( Base.Height );
		return true;
	}
	const auto* Resource = Textures.Find( Handle ); // 解放した番号を実行先へ渡さない。
	return Resource && Resources && Resources->GetTextureResourceView( *Resource, Out );
}
