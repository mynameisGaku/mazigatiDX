// SPDX-License-Identifier: MIT
#include "DX12ComputePipelineStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>

#pragma comment( lib, "d3dcompiler.lib" )

FDX12ComputePipelineStore::~FDX12ComputePipelineStore()
{
	ResetAfterGPUIdle();
}

bool FDX12ComputePipelineStore::Initialize( ID3D12Device* InDevice )
{
	if ( Device || !InDevice )
	{
		return false;
	}
	Device = InDevice;
	return true;
}

void FDX12ComputePipelineStore::CollectAfterGPUIdle()
{
	Retired.Reset();
}

void FDX12ComputePipelineStore::ResetAfterGPUIdle()
{
	Retired.Reset();
	Pipelines.Reset();
	Device.Reset();
}

bool FDX12ComputePipelineStore::Build( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, FDX12ComputePipelineBinding& Out )
{
	if ( !Device || !Bytecode.GetData() || Bytecode.Num() < 32 || Bytecode.Num() > MaxBytecodeBytes || FMemory::Memcmp( Bytecode.GetData(), "DXBC", 4 ) != 0 || !Slots.GetData() || Slots.Num() <= 0 || Slots.Num() > MaxSlots )
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
		const FComputeBufferSlot& Slot = Slots[Index]; // 今回検査する宣言。
		if ( ( Slot.Access != ERenderBufferShaderAccess::Read && Slot.Access != ERenderBufferShaderAccess::ReadWrite ) || Slot.Binding >= ( Slot.Access == ERenderBufferShaderAccess::Read ? 128u : 64u ) )
		{
			return false;
		}
		for ( int32 Earlier = 0; Earlier < Index; ++Earlier )
		{
			if ( Slots[Earlier].Access == Slot.Access && Slots[Earlier].Binding == Slot.Binding )
			{
				return false;
			}
		}
	}
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection; // バイナリに残っている段と使用資源の情報。
	D3D12_SHADER_DESC Shader = {}; // バイナリが要求するcomputeの段と資源数。
	if ( FAILED( D3DReflect( Bytecode.GetData(), static_cast<SIZE_T>( Bytecode.Num() ), IID_PPV_ARGS( &Reflection ) ) ) || FAILED( Reflection->GetDesc( &Shader ) ) || D3D12_SHVER_GET_TYPE( Shader.Version ) != D3D12_SHVER_COMPUTE_SHADER || D3D12_SHVER_GET_MAJOR( Shader.Version ) != 5 || D3D12_SHVER_GET_MINOR( Shader.Version ) > 1 || Shader.BoundResources != static_cast<UINT>( Slots.Num() ) )
	{
		return false;
	}
	UINT ThreadsX = 0, ThreadsY = 0, ThreadsZ = 0; // 一グループが使うスレッド数。
	const UINT Threads = Reflection->GetThreadGroupSize( &ThreadsX, &ThreadsY, &ThreadsZ ); // GPUへ渡す前に上限も照合する。
	if ( Threads == 0 || Threads > 1024 || ThreadsX == 0 || ThreadsX > 1024 || ThreadsY == 0 || ThreadsY > 1024 || ThreadsZ == 0 || ThreadsZ > 64 )
	{
		return false;
	}
	for ( UINT Index = 0; Index < Shader.BoundResources; ++Index )
	{
		D3D12_SHADER_INPUT_BIND_DESC Binding = {}; // 実際に使用するレジスターと種類。
		if ( FAILED( Reflection->GetResourceBindingDesc( Index, &Binding ) ) || Binding.Space != 0 || Binding.BindCount != 1 || Binding.Dimension != D3D_SRV_DIMENSION_BUFFER || ( Binding.Type != D3D_SIT_BYTEADDRESS && Binding.Type != D3D_SIT_UAV_RWBYTEADDRESS ) )
		{
			return false;
		}
		bool bMatched = false; // 順序ではなく種類とレジスターで照合する。
		for ( const FComputeBufferSlot& Slot : Slots )
		{
			bMatched = bMatched || ( Slot.Binding == Binding.BindPoint && ( Slot.Access == ERenderBufferShaderAccess::Read ? Binding.Type == D3D_SIT_BYTEADDRESS : Binding.Type == D3D_SIT_UAV_RWBYTEADDRESS ) );
		}
		if ( !bMatched )
		{
			return false;
		}
	}
	TArray<D3D12_DESCRIPTOR_RANGE> Ranges; // 各slotを一つのサイズ付きSRV/UAVへ対応させる。
	TArray<D3D12_ROOT_PARAMETER> Parameters; // 配列の順をdispatchのバッファ列へ対応させる。
	Ranges.SetNum( Slots.Num() );
	Parameters.SetNum( Slots.Num() );
	for ( int32 Index = 0; Index < Slots.Num(); ++Index )
	{
		Ranges[Index].RangeType = Slots[Index].Access == ERenderBufferShaderAccess::Read ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		Ranges[Index].NumDescriptors = 1;
		Ranges[Index].BaseShaderRegister = Slots[Index].Binding;
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
	Out = MoveTemp( Created );
	return true;
}

FComputePipelineHandle FDX12ComputePipelineStore::CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots )
{
	if ( Pipelines.Num() + Retired.Num() >= MaxPipelines )
	{
		return FComputePipelineHandle();
	}
	FDX12ComputePipelineBinding Created; // バイナリの検査後に完成した実体。
	if ( !Build( Bytecode, Slots, Created ) )
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

void FDX12ComputePipelineStore::ReleaseComputePipeline( FComputePipelineHandle Pipeline )
{
	if ( FDX12ComputePipelineBinding* Found = Pipelines.Find( Pipeline.Value ) )
	{
		Retired.Add( MoveTemp( *Found ) );
		Pipelines.Remove( Pipeline.Value );
	}
}

bool FDX12ComputePipelineStore::UsesDevice( ID3D12Device* InDevice ) const
{
	return Device && Device.Get() == InDevice;
}

bool FDX12ComputePipelineStore::AcquirePipeline( FComputePipelineHandle Pipeline, FDX12ComputePipelineBinding& Out ) const
{
	const FDX12ComputePipelineBinding* Found = Pipelines.Find( Pipeline.Value ); // 現在も登録されているプログラム。
	if ( !Found )
	{
		return false;
	}
	Out = *Found;
	return true;
}
