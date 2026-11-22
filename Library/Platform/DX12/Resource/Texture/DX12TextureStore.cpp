// SPDX-License-Identifier: MIT
#include "DX12TextureStore.h"
#include "DX12HalfFloat.h"
#include "DX12TextureCopy.h"
#include "DX12TextureFormat.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Math.h"

namespace
{
	// RGBA16F入力の4成分がすべて有限値か検査する。
	bool IsFiniteColor( const FColor &C )
	{
		return NMath::IsFinite( C.r ) && NMath::IsFinite( C.g ) && NMath::IsFinite( C.b ) && NMath::IsFinite( C.a );
	}
} // namespace

CDX12TextureStore::~CDX12TextureStore()
{
	ResetAfterGPUIdle();
}

bool CDX12TextureStore::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12DescriptorPool &InPool )
{
	if ( Device || !InDevice || !InQueue || !InPool.GetHeap() || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice, HeapDevice; // 混在したデバイスへ資源を登録しない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || FAILED( InPool.GetHeap()->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || QueueDevice.Get() != InDevice || HeapDevice.Get() != InDevice )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	Pool = &InPool;
	if ( !Submitter.Initialize( Device.Get(), Queue.Get() ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	Creator.Connect( Device.Get(), &Submitter, Pool );
	Updater.Connect( Device.Get(), Queue.Get(), &Creator, Pool );
	Reader.Connect( Device.Get(), &Submitter );
	bCapabilitiesAvailable = NDX12TextureFormat::GetComputeCapabilities( Device.Get(), Capabilities );
	bRenderTargetCapabilitiesAvailable = NDX12TextureFormat::GetRenderTargetCapabilities( Device.Get(), RenderTargetCapabilities );
	return true;
}

void CDX12TextureStore::Destroy( FDX12TextureRecord &Texture )
{
	if ( Pool && Texture.Descriptor.Serial != 0 )
	{
		Pool->Release( Texture.Descriptor );
	}
	Texture = FDX12TextureRecord();
}

void CDX12TextureStore::CollectAfterGPUIdle()
{
	for ( auto &Texture : Retired )
	{
		Destroy( Texture );
	}
	Retired.Reset();
	for ( auto &Version : RetiredVersions )
	{
		if ( !Updater.Adopt( Version ) ) // 次の更新で再利用できる旧版は、資源と記述子を残す。
		{
			Destroy( Version );
		}
	}
	RetiredVersions.Reset();
	Updater.CollectAfterGPUIdle();
	Submitter.CollectAfterGPUIdle();
}

void CDX12TextureStore::ResetAfterGPUIdle()
{
	CollectAfterGPUIdle();
	for ( auto &Pair : Textures )
	{
		Destroy( Pair.Value );
	}
	Textures.Reset();
	Updater.Disconnect();
	Creator.Disconnect();
	Reader.Disconnect();
	Submitter.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	Pool = nullptr;
	EndRenderTargetPass();
	Capabilities = FComputeTextureCapabilities();
	RenderTargetCapabilities = FRenderTargetCapabilities();
	bCapabilitiesAvailable = false;
	bRenderTargetCapabilitiesAvailable = false;
}

bool CDX12TextureStore::CanCreate( int32 Width, int32 Height ) const
{
	return Submitter.IsHealthy() && Device && Pool && SUCCEEDED( Device->GetDeviceRemovedReason() ) && Width > 0 && Height > 0 && Width <= 16384 && Height <= 16384 && static_cast<int64>( Width ) * Height <= 64 * 1024 * 1024 && Textures.Num() + Retired.Num() + RetiredVersions.Num() < MaxResources;
}

FTextureResourceHandle CDX12TextureStore::Register( FDX12TextureRecord &Created )
{
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() ); // 既存の画像資源と重複しない通し番号（別の所有先・再初期化後とも重ならない）。
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

bool CDX12TextureStore::Upload( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, FDX12TextureRecord &Out, bool bMips )
{
	if ( !CanCreate( Width, Height ) || !Pixels || ( bWritable && ( !bCapabilitiesAvailable || !Capabilities.bRGBA8ReadWrite ) ) )
	{
		return false;
	}
	return Creator.CreateRGBA8( Width, Height, Pixels, bWritable, bMips, Out );
}

bool CDX12TextureStore::UploadRGBA16F( int32 Width, int32 Height, const FColor *Pixels, FDX12TextureRecord &Out )
{
	if ( !CanCreate( Width, Height ) || !Pixels || !bCapabilitiesAvailable || !Capabilities.bRGBA16FReadWrite )
	{
		return false;
	}
	const usize PixelCount = static_cast<usize>( Width ) * static_cast<usize>( Height );
	for ( usize i = 0; i < PixelCount; ++i )
	{
		if ( !IsFiniteColor( Pixels[i] ) )
		{
			return false;
		}
	}
	return Creator.CreateRGBA16F( Width, Height, Pixels, Out );
}

FTextureResourceHandle CDX12TextureStore::CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return Create( Width, Height, Pixels, false );
}

FTextureResourceHandle CDX12TextureStore::CreateTextureRGBA8Mipped( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return Create( Width, Height, Pixels, false, Look == ERenderLook::Standard );
}

bool CDX12TextureStore::GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const
{
	if ( !Submitter.IsHealthy() || !Device || !bCapabilitiesAvailable || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Out = Capabilities;
	return true;
}

FTextureResourceHandle CDX12TextureStore::CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return Create( Width, Height, Pixels, true );
}

FTextureResourceHandle CDX12TextureStore::CreateComputeTextureRGBA16F( int32 Width, int32 Height, const FColor *Pixels )
{
	FDX12TextureRecord Created; // 完成したRGBA16F compute画像だけを登録する。
	if ( !UploadRGBA16F( Width, Height, Pixels, Created ) )
	{
		return FTextureResourceHandle();
	}
	return Register( Created );
}

bool CDX12TextureStore::GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const
{
	if ( !Submitter.IsHealthy() || !Device || !bRenderTargetCapabilitiesAvailable || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Out = RenderTargetCapabilities;
	return true;
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor )
{
	if ( !CanCreate( Width, Height ) || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bRGBA8RenderTarget )
	{
		return FTextureResourceHandle();
	}
	const float32 ClearValues[4] = { ClearColor.r / 255.0f, ClearColor.g / 255.0f, ClearColor.b / 255.0f, ClearColor.a / 255.0f }; // 初期消去色と一致させる最適化値。
	FDX12TextureRecord Created; // 初期消去とSRV作成が完了するまで登録しない。
	if ( !Creator.CreateColorRenderTarget( Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, ClearValues, EDX12TextureKind::RenderTargetRGBA8, Created ) )
	{
		return FTextureResourceHandle();
	}
	Created.InitialClearColor = ClearColor;
	return Register( Created );
}

FTextureResourceHandle CDX12TextureStore::CreateFloatColorRenderTarget( int32 Width, int32 Height, DXGI_FORMAT Format, const float32 ClearValues[4], EDX12TextureKind Kind )
{
	if ( !CanCreate( Width, Height ) )
	{
		return FTextureResourceHandle();
	}
	FDX12TextureRecord Created; // 初期消去とSRV作成が完了するまで登録しない。
	if ( !Creator.CreateColorRenderTarget( Width, Height, Format, ClearValues, Kind, Created ) )
	{
		return FTextureResourceHandle();
	}
	Created.InitialClearColorF = FColor( ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3] );
	return Register( Created );
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetRGBA16F( int32 Width, int32 Height, FColor ClearColor )
{
	if ( !IsFiniteColor( ClearColor ) || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bRGBA16FRenderTarget )
	{
		return FTextureResourceHandle();
	}
	const float32 ClearValues[4] = { ClearColor.r, ClearColor.g, ClearColor.b, ClearColor.a };
	return CreateFloatColorRenderTarget( Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, ClearValues, EDX12TextureKind::RenderTargetRGBA16F );
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetR32Float( int32 Width, int32 Height, float32 ClearValue )
{
	if ( !NMath::IsFinite( ClearValue ) || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bR32FloatRenderTarget )
	{
		return FTextureResourceHandle();
	}
	const float32 ClearValues[4] = { ClearValue, ClearValue, ClearValue, ClearValue };
	return CreateFloatColorRenderTarget( Width, Height, DXGI_FORMAT_R32_FLOAT, ClearValues, EDX12TextureKind::RenderTargetR32Float );
}

FTextureResourceHandle CDX12TextureStore::CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth )
{
	if ( !CanCreate( Width, Height ) || !NMath::IsFinite( ClearDepth ) || ClearDepth < 0.0f || ClearDepth > 1.0f || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bReadableDepthD32 || !bCapabilitiesAvailable || !Capabilities.bR32FloatRead )
	{
		return FTextureResourceHandle();
	}
	FDX12TextureRecord Created; // 初期消去と遷移が完了するまで登録しない。
	if ( !Creator.CreateDepthD32( Width, Height, ClearDepth, Created ) )
	{
		return FTextureResourceHandle();
	}
	return Register( Created );
}

bool CDX12TextureStore::UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const
{
	return Device && Queue && Device.Get() == InDevice && Queue.Get() == InQueue;
}

bool CDX12TextureStore::IsActiveTarget( uint64 TextureValue ) const
{
	if ( TextureValue == 0 )
	{
		return false;
	}
	if ( ActiveDepthTarget != 0 && TextureValue == ActiveDepthTarget )
	{
		return true;
	}
	for ( uint32 i = 0; i < ActiveColorTargetCount; ++i )
	{
		if ( ActiveColorTargets[i] == TextureValue )
		{
			return true;
		}
	}
	return false;
}

bool CDX12TextureStore::AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out, EComputeTextureFormat Format ) const
{
	const FDX12TextureRecord *Found = Textures.Find( Texture.Value ); // 現在の版だけを新しい提出へ渡す。
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || !bCapabilitiesAvailable || IsActiveTarget( Texture.Value ) || ( Access != EComputeTextureAccess::Read && Access != EComputeTextureAccess::ReadWrite ) )
	{
		return false;
	}
	if ( Format == EComputeTextureFormat::RGBA8 )
	{
		if ( !NDX12TextureKind::IsRGBA8( Found->Kind ) || !Capabilities.bRGBA8Read || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA8ReadWrite || Found->Kind != EDX12TextureKind::ComputeRGBA8 ) ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::RGBA16F )
	{
		if ( !NDX12TextureKind::IsRGBA16F( Found->Kind ) || !Capabilities.bRGBA16FRead || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA16FReadWrite || Found->Kind != EDX12TextureKind::ComputeRGBA16F ) ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::R32Float )
	{
		if ( !NDX12TextureKind::IsR32Float( Found->Kind ) || Access != EComputeTextureAccess::Read || !Capabilities.bR32FloatRead )
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	Out = Found->MakeBinding( Format ); // 呼び出し元へ渡した後は登録解除・更新から独立して保持する。
	return true;
}

bool CDX12TextureStore::AcquireGraphicsTexture( FTextureResourceHandle Texture, EComputeTextureFormat Format, FDX12TextureBinding &Out ) const
{
	const FDX12TextureRecord *Found = Textures.Find( Texture.Value ); // パス書き込み中のターゲットや解放済み番号は読ませない。
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || IsActiveTarget( Texture.Value ) || Found->Descriptor.Serial == 0 || Found->Descriptor.Gpu.ptr == 0 )
	{
		return false;
	}
	if ( Format == EComputeTextureFormat::RGBA8 )
	{
		if ( !NDX12TextureKind::IsRGBA8( Found->Kind ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::RGBA16F )
	{
		if ( !NDX12TextureKind::IsRGBA16F( Found->Kind ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::R32Float )
	{
		if ( !NDX12TextureKind::IsR32Float( Found->Kind ) || !bCapabilitiesAvailable || !Capabilities.bR32FloatRead )
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	Out = Found->MakeBinding( Format ); // 描画終了のGPU完了まで保持する参照。
	return true;
}

bool CDX12TextureStore::BeginRenderTargetPass( TArrayView<const FTextureResourceHandle> ColorTargets, FTextureResourceHandle DepthTarget, TArray<FDX12TextureBinding> &OutColors, FDX12TextureBinding &OutDepth )
{
	const int32 TargetCount = ColorTargets.Num(); // 同時束縛する色描画先の枚数。
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || ActiveColorTargetCount != 0 || ActiveDepthTarget != 0 || !ColorTargets.GetData() || TargetCount <= 0 || TargetCount > static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) || !bRenderTargetCapabilitiesAvailable || static_cast<uint32>( TargetCount ) > RenderTargetCapabilities.MaxColorTargets )
	{
		return false;
	}
	TArray<FDX12TextureBinding> ColorBindings; // 全ターゲットを検証できるまで確定しない。
	ColorBindings.SetNum( TargetCount );
	int32 RefWidth = 0, RefHeight = 0; // すべての色・深度ターゲットで揃える画素寸法。
	for ( int32 i = 0; i < TargetCount; ++i )
	{
		const FTextureResourceHandle Handle = ColorTargets[i]; // i番目の色描画先。
		if ( !Handle.IsValid() || ( DepthTarget.IsValid() && Handle.Value == DepthTarget.Value ) )
		{
			return false;
		}
		for ( int32 Earlier = 0; Earlier < i; ++Earlier )
		{
			if ( ColorTargets[Earlier].Value == Handle.Value )
			{
				return false;
			}
		}
		const FDX12TextureRecord *Color = Textures.Find( Handle.Value ); // 所有するRGBA8・RGBA16F・R32Floatカラー描画先か検査する（DepthD32は不可）。
		const bool bIsRGBA8 = Color && Color->Kind == EDX12TextureKind::RenderTargetRGBA8 && RenderTargetCapabilities.bRGBA8RenderTarget;
		const bool bIsRGBA16F = Color && Color->Kind == EDX12TextureKind::RenderTargetRGBA16F && RenderTargetCapabilities.bRGBA16FRenderTarget;
		const bool bIsR32Float = Color && Color->Kind == EDX12TextureKind::RenderTargetR32Float && RenderTargetCapabilities.bR32FloatRenderTarget;
		if ( !Color || ( !bIsRGBA8 && !bIsRGBA16F && !bIsR32Float ) || !Color->Resource || !Color->TargetHeap )
		{
			return false;
		}
		if ( i == 0 )
		{
			RefWidth = Color->Width;
			RefHeight = Color->Height;
		}
		else if ( Color->Width != RefWidth || Color->Height != RefHeight )
		{
			return false;
		}
		ColorBindings[i] = Color->MakeBinding( bIsRGBA16F ? EComputeTextureFormat::RGBA16F : ( bIsR32Float ? EComputeTextureFormat::R32Float : EComputeTextureFormat::RGBA8 ) );
	}
	FDX12TextureBinding DepthBinding; // 任意の読み取れる深度。
	if ( DepthTarget.IsValid() )
	{
		const FDX12TextureRecord *Depth = Textures.Find( DepthTarget.Value ); // 書き込み先の深度。
		if ( !Depth || Depth->Kind != EDX12TextureKind::DepthD32 || !Depth->Resource || !Depth->TargetHeap || Depth->Width != RefWidth || Depth->Height != RefHeight )
		{
			return false;
		}
		DepthBinding = Depth->MakeBinding( EComputeTextureFormat::R32Float );
	}
	ActiveColorTargetCount = static_cast<uint32>( TargetCount );
	for ( int32 i = 0; i < TargetCount; ++i )
	{
		ActiveColorTargets[i] = ColorTargets[i].Value;
	}
	ActiveDepthTarget = DepthTarget.IsValid() ? DepthTarget.Value : 0;
	OutColors = MoveTemp( ColorBindings );
	OutDepth = MoveTemp( DepthBinding );
	return true;
}

bool CDX12TextureStore::BeginRenderTargetPass( FTextureResourceHandle ColorTarget, FTextureResourceHandle DepthTarget, FDX12TextureBinding &OutColor, FDX12TextureBinding &OutDepth )
{
	TArray<FDX12TextureBinding> Colors; // 既存の単一ターゲット版はMRT版へ委譲する。
	const FTextureResourceHandle Single[1] = { ColorTarget };
	if ( !BeginRenderTargetPass( TArrayView<const FTextureResourceHandle>( Single, 1 ), DepthTarget, Colors, OutDepth ) )
	{
		return false;
	}
	OutColor = MoveTemp( Colors[0] );
	return true;
}

void CDX12TextureStore::EndRenderTargetPass()
{
	for ( uint32 i = 0; i < ActiveColorTargetCount; ++i )
	{
		ActiveColorTargets[i] = 0;
	}
	ActiveColorTargetCount = 0;
	ActiveDepthTarget = 0;
}

bool CDX12TextureStore::IsRenderTargetPassActive() const
{
	return ActiveColorTargetCount != 0 || ActiveDepthTarget != 0;
}

bool CDX12TextureStore::IsActiveTarget( FTextureResourceHandle Texture ) const
{
	return Texture.IsValid() && IsActiveTarget( Texture.Value );
}

FTextureResourceHandle CDX12TextureStore::Create( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, bool bMips )
{
	FDX12TextureRecord Created; // 完成した画像だけを登録する。
	// 読み取り用の段なし画像は、完了を待たずに積む（毎フレーム新しい文字列の画像を作るような使い方を軽くする。使い終えた資源・記述子も使い回す）。待てない・予算超過なら、完了を待つ経路へ。
	const bool bQueued = !bWritable && !bMips && Pixels && CanCreate( Width, Height ) && Updater.Update( Width, Height, Pixels, Created );
	if ( !bQueued && !Upload( Width, Height, Pixels, bWritable, Created, bMips ) )
	{
		return FTextureResourceHandle();
	}
	return Register( Created );
}

bool CDX12TextureStore::UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels )
{
	auto *Current = Textures.Find( Texture.Value ); // 有効な同寸法の通常画像またはcompute画像だけを更新する。
	if ( !Current || ( Current->Kind != EDX12TextureKind::StandardRGBA8 && Current->Kind != EDX12TextureKind::ComputeRGBA8 ) || Current->Width != Width || Current->Height != Height )
	{
		return false;
	}
	FDX12TextureRecord Created; // 失敗時は現在の内容とSRVを保つ。
	// 読み取り用の段なし画像は、完了を待たずに積む（資源・記述子・転送領域を使い回す）。待てない・予算超過なら、完了を待つ経路へ。
	const bool bQueued = Current->Kind == EDX12TextureKind::StandardRGBA8 && Current->MipLevels == 1 && CanCreate( Width, Height ) && Updater.Update( Width, Height, Pixels, Created );
	if ( !bQueued && !Upload( Width, Height, Pixels, Current->Kind == EDX12TextureKind::ComputeRGBA8, Created, Current->MipLevels > 1 ) ) // ミップマップを持つ画像は、ミップマップも作り直す
	{
		return false;
	}
	RetiredVersions.Add( MoveTemp( *Current ) );
	*Current = MoveTemp( Created );
	return true;
}

void CDX12TextureStore::ReleaseTexture( FTextureResourceHandle Texture )
{
	if ( auto *Current = Textures.Find( Texture.Value ) )
	{
		if ( Current->Kind == EDX12TextureKind::StandardRGBA8 && Current->MipLevels == 1 )
		{
			RetiredVersions.Add( MoveTemp( *Current ) ); // GPU完了後に、次の作成・更新で使い回せる。
		}
		else
		{
			Retired.Add( MoveTemp( *Current ) );
		}
		Textures.Remove( Texture.Value );
	}
}

bool CDX12TextureStore::GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const
{
	const auto *Found = Textures.Find( Texture.Value ); // 解放済み・深度・現在書き込み中の番号は描画viewとして貸さない。
	if ( !Found || Found->Kind == EDX12TextureKind::DepthD32 || Found->Descriptor.Serial == 0 || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	Out = { Found->Descriptor.Gpu.ptr, Found->Width, Found->Height };
	return true;
}

bool CDX12TextureStore::FindTextureCopyPair( FTextureResourceHandle Source, FTextureResourceHandle Destination, const FDX12TextureRecord *&OutSource, const FDX12TextureRecord *&OutDestination ) const
{
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Source.IsValid() || !Destination.IsValid() || Source == Destination || IsActiveTarget( Source ) || IsActiveTarget( Destination ) )
	{
		return false;
	}
	const FDX12TextureRecord *Src = Textures.Find( Source.Value ); // コピー元の定常状態の画像。
	const FDX12TextureRecord *Dst = Textures.Find( Destination.Value ); // 同じ寸法と色形式の書き込み先。
	if ( !Src || !Dst || !Src->Resource || !Dst->Resource || Src->Kind == EDX12TextureKind::DepthD32 || Dst->Kind == EDX12TextureKind::DepthD32 || Src->Width != Dst->Width || Src->Height != Dst->Height || Src->Resource->GetDesc().Format != Dst->Resource->GetDesc().Format )
	{
		return false;
	}
	OutSource = Src;
	OutDestination = Dst;
	return true;
}

bool CDX12TextureStore::RecordTextureCopy( ID3D12GraphicsCommandList *FrameCommands, FTextureResourceHandle Source, FTextureResourceHandle Destination ) const
{
	const FDX12TextureRecord *Src = nullptr, *Dst = nullptr; // 解除された後もフレーム完了まではStoreの遅延解放が保持する。
	constexpr D3D12_RESOURCE_STATES ShaderResource = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // パス間の定常状態。
	return FrameCommands && FindTextureCopyPair( Source, Destination, Src, Dst ) && NDX12TextureCopy::RecordRegionCopy( FrameCommands, Src->Resource.Get(), ShaderResource, Dst->Resource.Get(), 0, 0, Src->Width, Src->Height );
}

bool CDX12TextureStore::CopyTexture( FTextureResourceHandle Source, FTextureResourceHandle Destination )
{
	const FDX12TextureRecord *Src = nullptr, *Dst = nullptr; // 同期提出が終わるまで保持する組。
	if ( !FindTextureCopyPair( Source, Destination, Src, Dst ) )
	{
		return false;
	}
	ID3D12GraphicsCommandList *Commands = Submitter.Begin();
	if ( !Commands )
	{
		return false;
	}
	constexpr D3D12_RESOURCE_STATES ShaderResource = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // パス間の定常状態。
	if ( !NDX12TextureCopy::RecordRegionCopy( Commands, Src->Resource.Get(), ShaderResource, Dst->Resource.Get(), 0, 0, Src->Width, Src->Height ) )
	{
		Submitter.Abort();
		return false;
	}
	return Submitter.Submit( Src->Resource.Get(), Dst->Resource.Get() );
}

bool CDX12TextureStore::ReadTextureRGBA8( FTextureResourceHandle Texture, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FDX12TextureRecord *Found = Textures.Find( Texture.Value );
	if ( !Found || !NDX12TextureKind::IsRGBA8( Found->Kind ) )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !Reader.ReadRows( *Found, DXGI_FORMAT_R8G8B8A8_UNORM, 4, Rows, RowBytes ) )
	{
		return false;
	}
	TArray<FColor32> Pixels;
	Pixels.SetNum( Found->Width * Found->Height );
	FMemory::Memcpy( Pixels.GetData(), Rows.GetData(), static_cast<usize>( Pixels.Num() ) * sizeof( FColor32 ) );
	OutPixels = MoveTemp( Pixels );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}

bool CDX12TextureStore::ReadTextureRGBA16F( FTextureResourceHandle Texture, TArray<FColor> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FDX12TextureRecord *Found = Textures.Find( Texture.Value );
	if ( !Found || !NDX12TextureKind::IsRGBA16F( Found->Kind ) )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !Reader.ReadRows( *Found, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, Rows, RowBytes ) )
	{
		return false;
	}
	const int32 TotalPixels = Found->Width * Found->Height;
	const uint16 *HalfWords = reinterpret_cast<const uint16 *>( Rows.GetData() );
	TArray<FColor> Pixels;
	Pixels.SetNum( TotalPixels );
	for ( int32 i = 0; i < TotalPixels; ++i )
	{
		Pixels[i].r = NDX12HalfFloat::ToFloat32( HalfWords[i * 4 + 0] );
		Pixels[i].g = NDX12HalfFloat::ToFloat32( HalfWords[i * 4 + 1] );
		Pixels[i].b = NDX12HalfFloat::ToFloat32( HalfWords[i * 4 + 2] );
		Pixels[i].a = NDX12HalfFloat::ToFloat32( HalfWords[i * 4 + 3] );
	}
	OutPixels = MoveTemp( Pixels );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}

bool CDX12TextureStore::ReadTextureR32Float( FTextureResourceHandle Texture, TArray<float32> &OutValues, int32 &OutWidth, int32 &OutHeight )
{
	if ( !Submitter.IsHealthy() || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FDX12TextureRecord *Found = Textures.Find( Texture.Value );
	if ( !Found || Found->Kind != EDX12TextureKind::RenderTargetR32Float )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !Reader.ReadRows( *Found, DXGI_FORMAT_R32_FLOAT, 4, Rows, RowBytes ) )
	{
		return false;
	}
	TArray<float32> Values;
	Values.SetNum( Found->Width * Found->Height );
	FMemory::Memcpy( Values.GetData(), Rows.GetData(), static_cast<usize>( Values.Num() ) * sizeof( float32 ) );
	OutValues = MoveTemp( Values );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}
