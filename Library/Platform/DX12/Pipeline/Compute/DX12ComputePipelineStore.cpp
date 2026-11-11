// SPDX-License-Identifier: MIT
#include "DX12ComputePipelineStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureFormat.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>

#pragma comment( lib, "d3dcompiler.lib" )

CDX12ComputePipelineStore::~CDX12ComputePipelineStore()
{
	ResetAfterGPUIdle();
}

bool CDX12ComputePipelineStore::Initialize( ID3D12Device *InDevice )
{
	if ( Device || !InDevice )
	{
		return false;
	}
	Device = InDevice;
	return true;
}

void CDX12ComputePipelineStore::CollectAfterGPUIdle()
{
	Retired.Reset();
}

void CDX12ComputePipelineStore::ResetAfterGPUIdle()
{
	Retired.Reset();
	Pipelines.Reset();
	Device.Reset();
}

bool CDX12ComputePipelineStore::Build( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, TArrayView<const FComputeTextureSlot> TextureSlots, FDX12ComputePipelineBinding &Out )
{
	if ( !Device || !Bytecode.GetData() || Bytecode.Num() < 32 || Bytecode.Num() > MaxBytecodeBytes || FMemory::Memcmp( Bytecode.GetData(), "DXBC", 4 ) != 0 || Slots.Num() < 0 || TextureSlots.Num() < 0 || Slots.Num() > MaxSlots || TextureSlots.Num() > MaxSlots || Slots.Num() + TextureSlots.Num() <= 0 || Slots.Num() + TextureSlots.Num() > MaxSlots || ( Slots.Num() != 0 && !Slots.GetData() ) || ( TextureSlots.Num() != 0 && !TextureSlots.GetData() ) )
	{
		return false;
	}
	uint32 ContainerBytes = 0; // DXBCのヘッダーが宣言する全体の大きさ。
	FMemory::Memcpy( &ContainerBytes, Bytecode.GetData() + 24, sizeof( ContainerBytes ) );
	if ( ContainerBytes != static_cast<uint32>( Bytecode.Num() ) )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		const FComputeBufferSlot &Slot = Slots[Index]; // 明示した配置と、その場所を検査する。
		const bool bConstant = Slot.Layout == EComputeBufferLayout::Constant; // 定数は独立したbの名前空間を使う。
		const uint32 Limit = bConstant ? 14u : Slot.Access == ERenderBufferShaderAccess::Read ? 128u
																							  : 64u; // 初期DX12の場所の上限。
		if ( Slot.Layout > EComputeBufferLayout::Constant || ( Slot.Access != ERenderBufferShaderAccess::Read && Slot.Access != ERenderBufferShaderAccess::ReadWrite ) || Slot.Binding >= Limit || ( bConstant && Slot.Access != ERenderBufferShaderAccess::Read ) || ( Slot.Layout == EComputeBufferLayout::Structured ? Slot.ElementStride < 4 || Slot.ElementStride > 2048 || Slot.ElementStride % 4 != 0 : Slot.ElementStride != 0 ) )
		{
			return false;
		}
		for ( int32 Earlier = 0; Earlier < Index; ++Earlier )
		{
			const FComputeBufferSlot &Previous = Slots[Earlier]; // RAWと構造体配列は同じt／uの場所を占める。
			if ( Previous.Access == Slot.Access && ( Previous.Layout == EComputeBufferLayout::Constant ) == bConstant && Previous.Binding == Slot.Binding )
			{
				return false;
			}
		}
	}
	FComputeTextureCapabilities Capabilities; // 画像を使うプログラムだけ、実行先の対応能力を確認する。
	if ( TextureSlots.Num() != 0 && ( !NDX12TextureFormat::GetComputeCapabilities( Device.Get(), Capabilities ) || !Capabilities.bRGBA8Read ) )
	{
		return false;
	}
	for ( int32 Index = 0; Index < TextureSlots.Num(); ++Index )
	{
		const FComputeTextureSlot &Slot = TextureSlots[Index]; // バッファと共有するt／uの場所。
		if ( ( Slot.Access != EComputeTextureAccess::Read && Slot.Access != EComputeTextureAccess::ReadWrite ) || Slot.Binding >= ( Slot.Access == EComputeTextureAccess::Read ? 128u : 64u ) || ( Slot.Access == EComputeTextureAccess::ReadWrite && !Capabilities.bRGBA8ReadWrite ) )
		{
			return false;
		}
		for ( int32 Earlier = 0; Earlier < Index; ++Earlier )
		{
			if ( TextureSlots[Earlier].Access == Slot.Access && TextureSlots[Earlier].Binding == Slot.Binding )
			{
				return false;
			}
		}
		for ( const FComputeBufferSlot &Buffer : Slots )
		{
			const bool bRead = Buffer.Access == ERenderBufferShaderAccess::Read; // 定数のbは画像と競合しない。
			if ( Buffer.Layout != EComputeBufferLayout::Constant && bRead == ( Slot.Access == EComputeTextureAccess::Read ) && Buffer.Binding == Slot.Binding )
			{
				return false;
			}
		}
	}
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection; // バイナリに残っている段と使用資源の情報。
	D3D12_SHADER_DESC Shader = {}; // バイナリが要求するcomputeの段と資源数。
	if ( FAILED( D3DReflect( Bytecode.GetData(), static_cast<SIZE_T>( Bytecode.Num() ), IID_PPV_ARGS( &Reflection ) ) ) || FAILED( Reflection->GetDesc( &Shader ) ) || D3D12_SHVER_GET_TYPE( Shader.Version ) != D3D12_SHVER_COMPUTE_SHADER || D3D12_SHVER_GET_MAJOR( Shader.Version ) != 5 || D3D12_SHVER_GET_MINOR( Shader.Version ) > 1 || Shader.BoundResources != static_cast<UINT>( Slots.Num() + TextureSlots.Num() ) )
	{
		return false;
	}
	UINT ThreadsX = 0, ThreadsY = 0, ThreadsZ = 0; // 一グループが使うスレッド数。
	const UINT Threads = Reflection->GetThreadGroupSize( &ThreadsX, &ThreadsY, &ThreadsZ ); // GPUへ渡す前に上限も照合する。
	if ( Threads == 0 || Threads > 1024 || ThreadsX == 0 || ThreadsX > 1024 || ThreadsY == 0 || ThreadsY > 1024 || ThreadsZ == 0 || ThreadsZ > 64 )
	{
		return false;
	}
	TArray<uint32> MinimumBytes; // 定数の最小範囲を、slotの並びへ対応させる。
	MinimumBytes.SetNum( Slots.Num() );
	for ( UINT Index = 0; Index < Shader.BoundResources; ++Index )
	{
		D3D12_SHADER_INPUT_BIND_DESC Binding = {}; // バイナリが実際に使う種類と場所。
		if ( FAILED( Reflection->GetResourceBindingDesc( Index, &Binding ) ) || Binding.Space != 0 || Binding.BindCount != 1 )
		{
			return false;
		}
		const bool bTextureRead = Binding.Type == D3D_SIT_TEXTURE; // samplerを使わないTexture2D.Loadだけを受け付ける。
		const bool bTextureWrite = Binding.Type == D3D_SIT_UAV_RWTYPED; // RGBA8の型付き読み書き。
		if ( bTextureRead || bTextureWrite )
		{
			if ( Binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || ( Binding.ReturnType != D3D_RETURN_TYPE_FLOAT && Binding.ReturnType != D3D_RETURN_TYPE_UNORM ) || ( Binding.uFlags & D3D_SIF_TEXTURE_COMPONENTS ) != D3D_SIF_TEXTURE_COMPONENTS )
			{
				return false;
			}
			const EComputeTextureAccess Access = bTextureWrite ? EComputeTextureAccess::ReadWrite : EComputeTextureAccess::Read; // 画像の用途を宣言と照合する。
			bool bMatched = false; // 列挙順でなく、種類と場所で照合する。
			for ( const FComputeTextureSlot &Slot : TextureSlots )
			{
				bMatched |= Slot.Access == Access && Slot.Binding == Binding.BindPoint;
			}
			if ( !bMatched )
			{
				return false;
			}
			continue;
		}
		const bool bConstant = Binding.Type == D3D_SIT_CBUFFER; // counter付きの配列は今回の契約に含めない。
		const bool bStructured = Binding.Type == D3D_SIT_STRUCTURED || Binding.Type == D3D_SIT_UAV_RWSTRUCTURED; // 型付き配列とは区別する。
		const bool bRaw = Binding.Type == D3D_SIT_BYTEADDRESS || Binding.Type == D3D_SIT_UAV_RWBYTEADDRESS; // 従来のバイト列。
		if ( ( !bConstant && !bStructured && !bRaw ) || ( !bConstant && Binding.Dimension != D3D_SRV_DIMENSION_BUFFER ) )
		{
			return false;
		}
		const EComputeBufferLayout Layout = bConstant ? EComputeBufferLayout::Constant : bStructured ? EComputeBufferLayout::Structured
																									 : EComputeBufferLayout::Raw; // API共通の配置へ照合する。
		const ERenderBufferShaderAccess Access = Binding.Type == D3D_SIT_UAV_RWSTRUCTURED || Binding.Type == D3D_SIT_UAV_RWBYTEADDRESS ? ERenderBufferShaderAccess::ReadWrite : ERenderBufferShaderAccess::Read; // 実際の書き込みの有無。
		bool bMatched = false; // バイナリの列挙順に依存させない。
		for ( int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex )
		{
			const FComputeBufferSlot &Slot = Slots[SlotIndex]; // dispatchで渡す順番の宣言。
			if ( Slot.Binding != Binding.BindPoint || Slot.Access != Access || Slot.Layout != Layout )
			{
				continue;
			}
			if ( bStructured && Slot.ElementStride != Binding.NumSamples )
			{
				return false;
			}
			if ( bConstant )
			{
				D3D12_SHADER_BUFFER_DESC Buffer = {}; // コンパイラーが宣言した定数の必要範囲。
				ID3D12ShaderReflectionConstantBuffer *Constants = Reflection->GetConstantBufferByName( Binding.Name ); // Reflectionが所有する借用先。
				if ( !Constants || FAILED( Constants->GetDesc( &Buffer ) ) || Buffer.Type != D3D_CT_CBUFFER || Buffer.Size == 0 || Buffer.Size > 65536 )
				{
					return false;
				}
				MinimumBytes[SlotIndex] = Buffer.Size;
			}
			bMatched = true;
		}
		if ( !bMatched )
		{
			return false;
		}
	}
	TArray<D3D12_DESCRIPTOR_RANGE> Ranges; // 各slotを一つのサイズ付きCBV／SRV／UAVへ対応させる。
	TArray<D3D12_ROOT_PARAMETER> Parameters; // 配列の順をdispatchのバッファ列へ対応させる。
	Ranges.SetNum( Slots.Num() + TextureSlots.Num() );
	Parameters.SetNum( Ranges.Num() );
	for ( int32 Index = 0; Index < Ranges.Num(); ++Index )
	{
		if ( Index < Slots.Num() )
		{
			Ranges[Index].RangeType = Slots[Index].Layout == EComputeBufferLayout::Constant ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV : Slots[Index].Access == ERenderBufferShaderAccess::Read ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																																													   : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			Ranges[Index].BaseShaderRegister = Slots[Index].Binding;
		}
		else
		{
			const FComputeTextureSlot &Slot = TextureSlots[Index - Slots.Num()]; // バッファの後ろに画像を配置する。
			Ranges[Index].RangeType = Slot.Access == EComputeTextureAccess::Read ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			Ranges[Index].BaseShaderRegister = Slot.Binding;
		}
		Ranges[Index].NumDescriptors = 1;
		Parameters[Index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[Index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		Parameters[Index].DescriptorTable.NumDescriptorRanges = 1;
		Parameters[Index].DescriptorTable.pDescriptorRanges = &Ranges[Index];
	}
	D3D12_ROOT_SIGNATURE_DESC Root = {}; // プログラム専用の配置。UIの配置を使い回さない。
	Root.NumParameters = static_cast<UINT>( Parameters.Num() );
	Root.pParameters = Parameters.GetData();
	Microsoft::WRL::ComPtr<ID3DBlob> Serialized; // 作成中だけ保持する配置のバイナリ。
	FDX12ComputePipelineBinding Created; // 完成前に登録表へ公開しない実体。
	if ( FAILED( D3D12SerializeRootSignature( &Root, D3D_ROOT_SIGNATURE_VERSION_1, &Serialized, nullptr ) ) || FAILED( Device->CreateRootSignature( 0, Serialized->GetBufferPointer(), Serialized->GetBufferSize(), IID_PPV_ARGS( &Created.Root ) ) ) )
	{
		return false;
	}
	D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {}; // 検査を終えたバイナリと配置。
	Desc.pRootSignature = Created.Root.Get();
	Desc.CS = { Bytecode.GetData(), static_cast<SIZE_T>( Bytecode.Num() ) };
	if ( FAILED( Device->CreateComputePipelineState( &Desc, IID_PPV_ARGS( &Created.Pipeline ) ) ) )
	{
		return false;
	}
	Created.Slots = Slots.ToArray();
	Created.TextureSlots = TextureSlots.ToArray();
	Created.MinimumBytes = MoveTemp( MinimumBytes );
	Out = MoveTemp( Created );
	return true;
}

FComputePipelineHandle CDX12ComputePipelineStore::CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots )
{
	return CreateComputePipelineWithTextures( Bytecode, Slots, {} );
}

FComputePipelineHandle CDX12ComputePipelineStore::CreateComputePipelineWithTextures( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, TArrayView<const FComputeTextureSlot> TextureSlots )
{
	if ( Pipelines.Num() + Retired.Num() >= MaxPipelines )
	{
		return FComputePipelineHandle();
	}
	FDX12ComputePipelineBinding Created; // バイナリの検査後に完成した実体。
	if ( !Build( Bytecode, Slots, TextureSlots, Created ) )
	{
		return FComputePipelineHandle();
	}
	const FComputePipelineHandle Handle( NRenderResourceSerial::Allocate() ); // 再初期化や別の所有先と重ならない番号。
	if ( !Handle.IsValid() )
	{
		return FComputePipelineHandle();
	}
	Pipelines.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

void CDX12ComputePipelineStore::ReleaseComputePipeline( FComputePipelineHandle Pipeline )
{
	if ( FDX12ComputePipelineBinding *Found = Pipelines.Find( Pipeline.Value ) )
	{
		Retired.Add( MoveTemp( *Found ) );
		Pipelines.Remove( Pipeline.Value );
	}
}

bool CDX12ComputePipelineStore::UsesDevice( ID3D12Device *InDevice ) const
{
	return Device && Device.Get() == InDevice;
}

bool CDX12ComputePipelineStore::AcquirePipeline( FComputePipelineHandle Pipeline, FDX12ComputePipelineBinding &Out ) const
{
	const FDX12ComputePipelineBinding *Found = Pipelines.Find( Pipeline.Value ); // 現在も登録されているプログラム。
	if ( !Found )
	{
		return false;
	}
	Out = *Found;
	return true;
}
