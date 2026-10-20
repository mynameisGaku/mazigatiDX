#include "ImGuiTexture.h"

#include <d3d11.h>

#include "Library/Core/Collection/Map.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	struct FEntry
	{
		ID3D11ShaderResourceView* View = nullptr;
		const void* Texture = nullptr;   // View を作った元の ID3D11Texture2D（ハンドルが別の画像に使い回されたかの判定）
		int64 LastUsedFrame = 0;
	};

	TMap<int32, FEntry>& Cache()
	{
		static TMap<int32, FEntry> Instance;
		return Instance;
	}

	int64 GFrame = 0;
	ID3D11SamplerState* GPointSampler = nullptr;

	ID3D11Device* GetDevice() { return static_cast<ID3D11Device*>( const_cast<void*>( GetUseDirect3D11Device() ) ); }
	ID3D11DeviceContext* GetContext() { return static_cast<ID3D11DeviceContext*>( const_cast<void*>( GetUseDirect3D11DeviceContext() ) ); }

	void ReleaseView( FEntry& Entry )
	{
		if ( Entry.View != nullptr ) Entry.View->Release();
		Entry.View = nullptr;
		Entry.Texture = nullptr;
	}

	void SetPointSampler( const ImDrawList*, const ImDrawCmd* )
	{
		ID3D11DeviceContext* Context = GetContext();
		if ( Context == nullptr || GPointSampler == nullptr ) return;
		Context->PSSetSamplers( 0, 1, &GPointSampler );
	}
}

ImTextureID NDebugUI::GetImGuiTexture( int32 GraphHandle )
{
	if ( GraphHandle < 0 || GetUseDirect3DVersion() != DX_DIRECT3D_11 ) return 0;

	const void* Texture = GetGraphID3D11Texture2D( GraphHandle );
	if ( Texture == nullptr ) return 0;

	FEntry& Entry = Cache().FindOrAdd( GraphHandle );
	Entry.LastUsedFrame = GFrame;
	if ( Entry.View != nullptr && Entry.Texture == Texture ) return static_cast<ImTextureID>( reinterpret_cast<uintptr_t>( Entry.View ) );

	// 初めて、または別の画像になった
	ReleaseView( Entry );
	ID3D11Device* Device = GetDevice();
	if ( Device == nullptr ) return 0;
	if ( FAILED( Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( Texture ) ), nullptr, &Entry.View ) ) ) return 0;
	Entry.Texture = Texture;
	return static_cast<ImTextureID>( reinterpret_cast<uintptr_t>( Entry.View ) );
}

void NDebugUI::TickImGuiTextureCache()
{
	++GFrame;
	for ( auto It = Cache().CreateIterator(); It; ++It )
	{
		if ( GFrame - It.Value().LastUsedFrame <= 120 ) continue;
		ReleaseView( It.Value() );
		It.RemoveCurrent();
	}
}

void NDebugUI::ClearImGuiTextureCache()
{
	for ( auto It = Cache().CreateIterator(); It; ++It ) ReleaseView( It.Value() );
	Cache().Empty();
	if ( GPointSampler != nullptr )
	{
		GPointSampler->Release();
		GPointSampler = nullptr;
	}
}

int32 NDebugUI::GetImGuiTextureCacheSize()
{
	return Cache().Num();
}

void NDebugUI::BeginNearest( ImDrawList* DrawList )
{
	if ( GPointSampler == nullptr )
	{
		ID3D11Device* Device = GetDevice();
		if ( Device == nullptr ) return;
		D3D11_SAMPLER_DESC Desc = {};
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		Desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		Desc.MaxLOD = D3D11_FLOAT32_MAX;
		if ( FAILED( Device->CreateSamplerState( &Desc, &GPointSampler ) ) ) return;
	}
	DrawList->AddCallback( SetPointSampler, nullptr );
}

void NDebugUI::EndNearest( ImDrawList* DrawList )
{
	DrawList->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
}
