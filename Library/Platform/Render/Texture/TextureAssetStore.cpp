// SPDX-License-Identifier: MIT
#include "TextureAssetStore.h"
#include "Library/Core/Types/Numeric.h"

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
		}
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
	Textures.Reset();
}

int32 CTextureAssetStore::Num() const
{
	return Textures.Num();
}

bool CTextureAssetStore::GetSize( int32 Handle, int32& OutWidth, int32& OutHeight ) const
{
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

bool CTextureAssetStore::GetTextureDrawView( int32 Handle, FTextureDrawView& Out )
{
	const auto* Resource = Textures.Find( Handle ); // 解放した番号を実行先へ渡さない。
	return Resource && Resources && Resources->GetTextureResourceView( *Resource, Out );
}
