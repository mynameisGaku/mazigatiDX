// SPDX-License-Identifier: MIT
#include "DX12GraphicsPipelineStore.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/CString.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureFormat.h"
#include <d3d12shader.h>
#include <d3dcompiler.h>

#pragma comment( lib, "d3dcompiler.lib" )

namespace
{
	// DXBCコンテナの先頭ヘッダ長（4バイト単位で8項目）。
	constexpr uint32 DxbcMinHeaderBytes = 32;

	// DXBCの識別子と全長を検証する。
	bool ValidateDXBCHeader( TArrayView<const uint8> Bytecode, uint32 MaxBytes )
	{
		if ( !Bytecode.GetData() || Bytecode.Num() < static_cast<int32>( DxbcMinHeaderBytes ) || static_cast<uint32>( Bytecode.Num() ) > MaxBytes )
		{
			return false;
		}
		uint32 Words[8] = {}; // 未整列な先頭アドレスでも安全に先頭ヘッダを読む。
		FMemory::Memcpy( Words, Bytecode.GetData(), DxbcMinHeaderBytes );
		return Words[0] == 0x43425844u && Words[5] == 1u && Words[6] == static_cast<uint32>( Bytecode.Num() ) && Words[7] >= 1u;
	}

	// DXBCバイナリからSM5.0/5.1の指定ステージreflectionを取得する。
	bool ReflectShaderStage( TArrayView<const uint8> Bytecode, uint32 MaxBytes, UINT ExpectedType, Microsoft::WRL::ComPtr<ID3D12ShaderReflection> &OutReflection, D3D12_SHADER_DESC &OutDesc )
	{
		if ( !ValidateDXBCHeader( Bytecode, MaxBytes ) )
		{
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection; // 成功時まで出力を書き換えない。
		if ( FAILED( D3DReflect( Bytecode.GetData(), static_cast<SIZE_T>( Bytecode.Num() ), IID_PPV_ARGS( &Reflection ) ) ) )
		{
			return false;
		}
		D3D12_SHADER_DESC Desc = {}; // ステージとシェーダーモデルを確認する。
		if ( FAILED( Reflection->GetDesc( &Desc ) ) )
		{
			return false;
		}
		const UINT ShaderType = D3D12_SHVER_GET_TYPE( Desc.Version ); // VSかPSかの種別。
		const UINT Major = D3D12_SHVER_GET_MAJOR( Desc.Version ); // 対応はShader Model 5のみ。
		const UINT Minor = D3D12_SHVER_GET_MINOR( Desc.Version ); // 5.0と5.1を許可する。
		if ( ShaderType != ExpectedType || Major != 5 || Minor > 1 )
		{
			return false;
		}
		OutReflection = MoveTemp( Reflection );
		OutDesc = Desc;
		return true;
	}

	// HLSLセマンティック名として妥当なASCII識別子（システム値除外可）か判定する。
	bool IsValidVertexSemanticName( const char *Name )
	{
		if ( !Name )
		{
			return false;
		}
		const usize Length = NCString::Length( Name ); // セマンティック名の長さ。
		if ( Length == 0 || Length > 31 || NCString::CompareNIgnoreCase( Name, "SV_", 3 ) == 0 )
		{
			return false;
		}
		const char First = Name[0]; // 先頭文字は英字か下線に限定する。
		if ( !( ( First >= 'A' && First <= 'Z' ) || ( First >= 'a' && First <= 'z' ) || First == '_' ) )
		{
			return false;
		}
		for ( usize i = 1; i < Length; ++i )
		{
			const char C = Name[i]; // 後続は英数字か下線のみ。
			if ( !( ( C >= 'A' && C <= 'Z' ) || ( C >= 'a' && C <= 'z' ) || ( C >= '0' && C <= '9' ) || C == '_' ) )
			{
				return false;
			}
		}
		return true;
	}

	// 頂点属性形式からDXGI形式・バイト幅・許容成分マスクを求める。
	bool MapVertexElementFormat( EGraphicsVertexAttributeFormat Format, DXGI_FORMAT &OutDxgi, uint32 &OutBytes, BYTE &OutAllowedMask )
	{
		switch ( Format )
		{
		case EGraphicsVertexAttributeFormat::Float1:
			OutDxgi = DXGI_FORMAT_R32_FLOAT;
			OutBytes = 4;
			OutAllowedMask = 0x1;
			return true;
		case EGraphicsVertexAttributeFormat::Float2:
			OutDxgi = DXGI_FORMAT_R32G32_FLOAT;
			OutBytes = 8;
			OutAllowedMask = 0x3;
			return true;
		case EGraphicsVertexAttributeFormat::Float3:
			OutDxgi = DXGI_FORMAT_R32G32B32_FLOAT;
			OutBytes = 12;
			OutAllowedMask = 0x7;
			return true;
		case EGraphicsVertexAttributeFormat::Float4:
			OutDxgi = DXGI_FORMAT_R32G32B32A32_FLOAT;
			OutBytes = 16;
			OutAllowedMask = 0xF;
			return true;
		case EGraphicsVertexAttributeFormat::UByte4Norm:
			OutDxgi = DXGI_FORMAT_R8G8B8A8_UNORM;
			OutBytes = 4;
			OutAllowedMask = 0xF;
			return true;
		default:
			return false;
		}
	}

	// 比較演算の設定をD3D12列挙へ変換する。
	bool MapComparisonFunc( EGeometryCompare Compare, D3D12_COMPARISON_FUNC &Out )
	{
		switch ( Compare )
		{
		case EGeometryCompare::Never:
			Out = D3D12_COMPARISON_FUNC_NEVER;
			return true;
		case EGeometryCompare::Less:
			Out = D3D12_COMPARISON_FUNC_LESS;
			return true;
		case EGeometryCompare::Equal:
			Out = D3D12_COMPARISON_FUNC_EQUAL;
			return true;
		case EGeometryCompare::LessEqual:
			Out = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			return true;
		case EGeometryCompare::Greater:
			Out = D3D12_COMPARISON_FUNC_GREATER;
			return true;
		case EGeometryCompare::NotEqual:
			Out = D3D12_COMPARISON_FUNC_NOT_EQUAL;
			return true;
		case EGeometryCompare::GreaterEqual:
			Out = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			return true;
		case EGeometryCompare::Always:
			Out = D3D12_COMPARISON_FUNC_ALWAYS;
			return true;
		default:
			return false;
		}
	}

	// カリング設定をD3D12列挙へ変換する。
	bool MapCullMode( EGeometryCulling Culling, D3D12_CULL_MODE &Out )
	{
		switch ( Culling )
		{
		case EGeometryCulling::None:
			Out = D3D12_CULL_MODE_NONE;
			return true;
		case EGeometryCulling::BackFaces:
			Out = D3D12_CULL_MODE_BACK;
			return true;
		case EGeometryCulling::FrontFaces:
			Out = D3D12_CULL_MODE_FRONT;
			return true;
		default:
			return false;
		}
	}

	// トポロジ種別をPSO用の分類へ変換する。
	bool MapTopologyType( EGeometryTopology Topology, D3D12_PRIMITIVE_TOPOLOGY_TYPE &Out )
	{
		switch ( Topology )
		{
		case EGeometryTopology::Points:
			Out = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
			return true;
		case EGeometryTopology::Lines:
			Out = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			return true;
		case EGeometryTopology::Triangles:
			Out = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			return true;
		default:
			return false;
		}
	}

	// テクスチャ範囲外参照をD3D12列挙へ変換する。
	bool MapAddressMode( EGeometryTextureAddress Address, D3D12_TEXTURE_ADDRESS_MODE &Out )
	{
		switch ( Address )
		{
		case EGeometryTextureAddress::Wrap:
			Out = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			return true;
		case EGeometryTextureAddress::Mirror:
			Out = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			return true;
		case EGeometryTextureAddress::Clamp:
			Out = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			return true;
		case EGeometryTextureAddress::Border:
			Out = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			return true;
		default:
			return false;
		}
	}

	// 頂点入力定義を検証し、VS入力署名と1:1で照合する。
	bool ValidateVertexInputs( const FGraphicsPipelineDesc &Desc, ID3D12ShaderReflection *VsReflection, const D3D12_SHADER_DESC &VsDesc, TArray<FString> &OutNames, TArray<D3D12_INPUT_ELEMENT_DESC> &OutElements )
	{
		const int32 Count = Desc.InputElements.Num(); // 頂点属性の宣言数。
		TArray<FString> Names; // D3D12_INPUT_ELEMENT_DESCが参照する文字列の所有者。
		TArray<D3D12_INPUT_ELEMENT_DESC> Elements; // PSO構築へ渡す入力要素列。
		TArray<BYTE> ElementMasks; // シェーダー入力マスクの上限照合に使う。
		TArray<bool> ElementMatched; // 宣言済み要素がすべてVSで参照されるか。
		Names.Reserve( Count );
		Elements.SetNum( Count );
		ElementMasks.SetNum( Count );
		ElementMatched.SetNum( Count );
		for ( int32 i = 0; i < Count; ++i )
		{
			const FGraphicsInputElement &Src = Desc.InputElements[i]; // 呼出側が渡した属性定義。
			DXGI_FORMAT DxgiFormat = DXGI_FORMAT_UNKNOWN;
			uint32 ElemBytes = 0;
			BYTE AllowedMask = 0;
			if ( !IsValidVertexSemanticName( Src.SemanticName ) || ( Src.AlignedByteOffset % 4 ) != 0 || !MapVertexElementFormat( Src.Format, DxgiFormat, ElemBytes, AllowedMask ) || Src.AlignedByteOffset > Desc.VertexStride || ElemBytes > Desc.VertexStride - Src.AlignedByteOffset )
			{
				return false;
			}
			for ( int32 Earlier = 0; Earlier < i; ++Earlier )
			{
				if ( Desc.InputElements[Earlier].SemanticIndex == Src.SemanticIndex && NCString::CompareIgnoreCase( *Names[Earlier], Src.SemanticName ) == 0 )
				{
					return false;
				}
			}
			Names.Add( FString( Src.SemanticName ) );
			Elements[i].SemanticName = nullptr;
			Elements[i].SemanticIndex = Src.SemanticIndex;
			Elements[i].Format = DxgiFormat;
			Elements[i].InputSlot = 0;
			Elements[i].AlignedByteOffset = Src.AlignedByteOffset;
			Elements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			Elements[i].InstanceDataStepRate = 0;
			ElementMasks[i] = AllowedMask;
			ElementMatched[i] = false;
		}
		// Names配列の拡張がすべて終わった後で、不変になったC文字列ポインタを結ぶ。
		for ( int32 i = 0; i < Count; ++i )
		{
			Elements[i].SemanticName = *Names[i];
		}
		for ( UINT i = 0; i < VsDesc.InputParameters; ++i )
		{
			D3D12_SIGNATURE_PARAMETER_DESC Param = {}; // VSの1入力署名。
			if ( FAILED( VsReflection->GetInputParameterDesc( i, &Param ) ) )
			{
				return false;
			}
			if ( Param.SystemValueType != D3D_NAME_UNDEFINED )
			{
				if ( Param.SystemValueType != D3D_NAME_VERTEX_ID )
				{
					return false;
				}
				continue;
			}
			if ( Param.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32 )
			{
				return false;
			}
			int32 Found = -1; // 宣言済み要素の一致先。
			for ( int32 k = 0; k < Count; ++k )
			{
				if ( Elements[k].SemanticIndex == Param.SemanticIndex && NCString::CompareIgnoreCase( *Names[k], Param.SemanticName ) == 0 )
				{
					Found = k;
					break;
				}
			}
			if ( Found < 0 || ElementMatched[Found] || ( Param.Mask & ~ElementMasks[Found] ) != 0 )
			{
				return false;
			}
			ElementMatched[Found] = true;
		}
		for ( bool bUsed : ElementMatched )
		{
			if ( !bUsed )
			{
				return false;
			}
		}
		OutNames = MoveTemp( Names );
		OutElements = MoveTemp( Elements );
		return true;
	}

	// VS出力・PS入力の受け渡し整合性と、PSのSV_Target0..N-1出力一致を検証する。
	bool ValidateStageOutputs( const FGraphicsPipelineDesc &Desc, ID3D12ShaderReflection *VsReflection, const D3D12_SHADER_DESC &VsDesc, ID3D12ShaderReflection *PsReflection, const D3D12_SHADER_DESC &PsDesc )
	{
		TArray<D3D12_SIGNATURE_PARAMETER_DESC> VsOutputs; // VSの全出力署名。
		VsOutputs.SetNum( static_cast<int32>( VsDesc.OutputParameters ) );
		bool bHasPosition = false; // ラスタライズに必要なSV_Positionの有無。
		for ( UINT i = 0; i < VsDesc.OutputParameters; ++i )
		{
			if ( FAILED( VsReflection->GetOutputParameterDesc( i, &VsOutputs[static_cast<int32>( i )] ) ) )
			{
				return false;
			}
			if ( VsOutputs[static_cast<int32>( i )].SystemValueType == D3D_NAME_POSITION && VsOutputs[static_cast<int32>( i )].SemanticIndex == 0 )
			{
				bHasPosition = true;
			}
		}
		if ( !bHasPosition )
		{
			return false;
		}
		for ( UINT i = 0; i < PsDesc.InputParameters; ++i )
		{
			D3D12_SIGNATURE_PARAMETER_DESC PsIn = {}; // PSの1入力署名。
			if ( FAILED( PsReflection->GetInputParameterDesc( i, &PsIn ) ) )
			{
				return false;
			}
			if ( PsIn.SystemValueType == D3D_NAME_POSITION || PsIn.SystemValueType == D3D_NAME_IS_FRONT_FACE )
			{
				continue;
			}
			if ( PsIn.SystemValueType != D3D_NAME_UNDEFINED )
			{
				return false;
			}
			bool bMatched = false; // 対応するVS出力が見つかったか。
			for ( const D3D12_SIGNATURE_PARAMETER_DESC &VsOut : VsOutputs )
			{
				if ( VsOut.SystemValueType == D3D_NAME_UNDEFINED && VsOut.SemanticIndex == PsIn.SemanticIndex && VsOut.ComponentType == PsIn.ComponentType && ( VsOut.Mask & PsIn.Mask ) == PsIn.Mask && NCString::CompareIgnoreCase( VsOut.SemanticName, PsIn.SemanticName ) == 0 )
				{
					bMatched = true;
					break;
				}
			}
			if ( !bMatched )
			{
				return false;
			}
		}
		bool TargetSeen[FGraphicsPipelineDesc::MaxRenderTargets] = {}; // 各SV_TargetNが過不足なく存在するか。
		for ( UINT i = 0; i < PsDesc.OutputParameters; ++i )
		{
			D3D12_SIGNATURE_PARAMETER_DESC PsOut = {}; // PSの1出力署名。
			if ( FAILED( PsReflection->GetOutputParameterDesc( i, &PsOut ) ) || PsOut.SystemValueType != D3D_NAME_TARGET || PsOut.SemanticIndex >= Desc.NumRenderTargets || TargetSeen[PsOut.SemanticIndex] || PsOut.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32 || PsOut.Mask != 0xF )
			{
				return false;
			}
			TargetSeen[PsOut.SemanticIndex] = true;
		}
		for ( uint32 Target = 0; Target < Desc.NumRenderTargets; ++Target )
		{
			if ( !TargetSeen[Target] )
			{
				return false;
			}
		}
		return true;
	}

	// 1ステージ分のb/t/s束縛を検査し、宣言スロットの使用状態とCB最小サイズを更新する。
	bool ValidateStageBindings( ID3D12ShaderReflection *Reflection, const D3D12_SHADER_DESC &ShaderDesc, TArrayView<const FGraphicsBufferSlot> BufferSlots, TArrayView<const FGraphicsTextureSlot> TextureSlots, TArray<bool> &BufferMatched, TArray<bool> &TextureMatched, TArray<uint32> &MinimumBytes )
	{
		for ( UINT i = 0; i < ShaderDesc.BoundResources; ++i )
		{
			D3D12_SHADER_INPUT_BIND_DESC Bind = {}; // シェーダーが参照する1つの資源束縛。
			if ( FAILED( Reflection->GetResourceBindingDesc( i, &Bind ) ) || Bind.Space != 0 || Bind.BindCount != 1 )
			{
				return false;
			}
			if ( Bind.Type == D3D_SIT_SAMPLER )
			{
				if ( Bind.BindPoint != 0 )
				{
					return false;
				}
				continue;
			}
			if ( Bind.Type == D3D_SIT_CBUFFER )
			{
				int32 SlotIndex = -1; // 一致するbスロット位置。
				for ( int32 k = 0; k < BufferSlots.Num(); ++k )
				{
					if ( BufferSlots[k].Layout == EGraphicsBufferLayout::Constant && BufferSlots[k].Binding == Bind.BindPoint )
					{
						SlotIndex = k;
						break;
					}
				}
				if ( SlotIndex < 0 )
				{
					return false;
				}
				ID3D12ShaderReflectionConstantBuffer *Cb = Reflection->GetConstantBufferByName( Bind.Name ); // 定数ブロックの宣言長。
				D3D12_SHADER_BUFFER_DESC CbDesc = {};
				if ( !Cb || FAILED( Cb->GetDesc( &CbDesc ) ) || CbDesc.Size == 0 || CbDesc.Size > 65536 )
				{
					return false;
				}
				if ( CbDesc.Size > MinimumBytes[SlotIndex] )
				{
					MinimumBytes[SlotIndex] = static_cast<uint32>( CbDesc.Size );
				}
				BufferMatched[SlotIndex] = true;
				continue;
			}
			if ( Bind.Type == D3D_SIT_BYTEADDRESS || Bind.Type == D3D_SIT_STRUCTURED )
			{
				const EGraphicsBufferLayout Expected = Bind.Type == D3D_SIT_BYTEADDRESS ? EGraphicsBufferLayout::Raw : EGraphicsBufferLayout::Structured; // バッファ配置の種別。
				int32 SlotIndex = -1;
				for ( int32 k = 0; k < BufferSlots.Num(); ++k )
				{
					if ( BufferSlots[k].Layout == Expected && BufferSlots[k].Binding == Bind.BindPoint && ( Expected == EGraphicsBufferLayout::Raw || BufferSlots[k].ElementStride == Bind.NumSamples ) )
					{
						SlotIndex = k;
						break;
					}
				}
				if ( SlotIndex < 0 )
				{
					return false;
				}
				BufferMatched[SlotIndex] = true;
				continue;
			}
			if ( Bind.Type == D3D_SIT_TEXTURE )
			{
				if ( Bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || ( Bind.ReturnType != D3D_RETURN_TYPE_FLOAT && Bind.ReturnType != D3D_RETURN_TYPE_UNORM ) )
				{
					return false;
				}
				int32 SlotIndex = -1;
				for ( int32 k = 0; k < TextureSlots.Num(); ++k )
				{
					if ( TextureSlots[k].Binding == Bind.BindPoint )
					{
						SlotIndex = k;
						break;
					}
				}
				if ( SlotIndex < 0 )
				{
					return false;
				}
				const bool bScalar = ( Bind.uFlags & D3D_SIF_TEXTURE_COMPONENTS ) == 0; // Texture2D<float>はスカラー、Texture2D<float4>は4成分。
				if ( ( TextureSlots[SlotIndex].Format == EComputeTextureFormat::R32Float ) != bScalar || ( bScalar && Bind.ReturnType != D3D_RETURN_TYPE_FLOAT ) )
				{
					return false;
				}
				TextureMatched[SlotIndex] = true;
				continue;
			}
			return false;
		}
		return true;
	}

	// BufferSlots／TextureSlotsからRootSignatureを構築する。
	bool CreatePipelineRootSignature( ID3D12Device *Device, const FGraphicsPipelineDesc &Desc, D3D12_TEXTURE_ADDRESS_MODE AddressMode, Microsoft::WRL::ComPtr<ID3D12RootSignature> &OutRoot )
	{
		const int32 TotalSlots = Desc.BufferSlots.Num() + Desc.TextureSlots.Num(); // バッファ群の後ろへ画像群を並べる。
		TArray<D3D12_ROOT_PARAMETER> Params; // ルート引数配列。
		TArray<D3D12_DESCRIPTOR_RANGE> TextureRanges; // 各画像スロット用の1記述子テーブル範囲。
		Params.SetNum( TotalSlots );
		TextureRanges.SetNum( Desc.TextureSlots.Num() );
		for ( int32 i = 0; i < Desc.BufferSlots.Num(); ++i )
		{
			const FGraphicsBufferSlot &Slot = Desc.BufferSlots[i]; // b定数はRoot CBV、tはRoot SRVで結合する。
			Params[i] = {};
			Params[i].ParameterType = Slot.Layout == EGraphicsBufferLayout::Constant ? D3D12_ROOT_PARAMETER_TYPE_CBV : D3D12_ROOT_PARAMETER_TYPE_SRV;
			Params[i].Descriptor.ShaderRegister = Slot.Binding;
			Params[i].Descriptor.RegisterSpace = 0;
			Params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		for ( int32 i = 0; i < Desc.TextureSlots.Num(); ++i )
		{
			TextureRanges[i] = {};
			TextureRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			TextureRanges[i].NumDescriptors = 1;
			TextureRanges[i].BaseShaderRegister = Desc.TextureSlots[i].Binding;
			TextureRanges[i].RegisterSpace = 0;
			TextureRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			const int32 RootIndex = Desc.BufferSlots.Num() + i; // バッファスロットの後ろの引数番号。
			Params[RootIndex] = {};
			Params[RootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			Params[RootIndex].DescriptorTable.NumDescriptorRanges = 1;
			Params[RootIndex].DescriptorTable.pDescriptorRanges = &TextureRanges[i];
			Params[RootIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_STATIC_SAMPLER_DESC StaticSampler = {}; // register(s0)で使える静的サンプラ。
		StaticSampler.Filter = Desc.bLinearSampler ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
		StaticSampler.AddressU = AddressMode;
		StaticSampler.AddressV = AddressMode;
		StaticSampler.AddressW = AddressMode;
		StaticSampler.MaxAnisotropy = 1;
		StaticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		StaticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		StaticSampler.MaxLOD = D3D12_FLOAT32_MAX;
		StaticSampler.ShaderRegister = 0;
		StaticSampler.RegisterSpace = 0;
		StaticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC RootDesc = {}; // 頂点入力レイアウトを許可し未使用ステージを閉じる。
		RootDesc.NumParameters = static_cast<UINT>( Params.Num() );
		RootDesc.pParameters = Params.Num() > 0 ? Params.GetData() : nullptr;
		RootDesc.NumStaticSamplers = 1;
		RootDesc.pStaticSamplers = &StaticSampler;
		RootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		Microsoft::WRL::ComPtr<ID3DBlob> Serialized, Errors; // 直列化とdevice生成の両方に成功したときだけ公開する。
		if ( FAILED( D3D12SerializeRootSignature( &RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &Serialized, &Errors ) ) )
		{
			return false;
		}
		return SUCCEEDED( Device->CreateRootSignature( 0, Serialized->GetBufferPointer(), Serialized->GetBufferSize(), IID_PPV_ARGS( &OutRoot ) ) );
	}
} // namespace

CDX12GraphicsPipelineStore::~CDX12GraphicsPipelineStore()
{
	ResetAfterGPUIdle();
}

bool CDX12GraphicsPipelineStore::Initialize( ID3D12Device *InDevice )
{
	if ( Device || !InDevice || FAILED( InDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Device = InDevice;
	return true;
}

void CDX12GraphicsPipelineStore::CollectAfterGPUIdle()
{
	Retired.Reset();
}

void CDX12GraphicsPipelineStore::ResetAfterGPUIdle()
{
	Retired.Reset();
	Pipelines.Reset();
	Device.Reset();
}

bool CDX12GraphicsPipelineStore::UsesDevice( ID3D12Device *InDevice ) const
{
	return Device && Device.Get() == InDevice;
}

FGraphicsPipelineHandle CDX12GraphicsPipelineStore::CreateGraphicsPipeline( const FGraphicsPipelineDesc &Desc )
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) || Pipelines.Num() + Retired.Num() >= MaxPipelines )
	{
		return {};
	}
	if ( Desc.VertexStride < 4 || Desc.VertexStride > MaxVertexStride || ( Desc.VertexStride % 4 ) != 0 || !Desc.InputElements.GetData() || Desc.InputElements.Num() <= 0 || Desc.InputElements.Num() > MaxInputElements || ( Desc.BufferSlots.Num() != 0 && !Desc.BufferSlots.GetData() ) || ( Desc.TextureSlots.Num() != 0 && !Desc.TextureSlots.GetData() ) || Desc.BufferSlots.Num() + Desc.TextureSlots.Num() > MaxSlots )
	{
		return {};
	}
	if ( Desc.NumRenderTargets < 1 || Desc.NumRenderTargets > FGraphicsPipelineDesc::MaxRenderTargets )
	{
		return {};
	}
	bool bNeedsRGBA16F = false; // RGBA16Fを含む場合はデバイス能力を照会する。
	bool bNeedsR32Float = false; // R32Floatを含む場合はデバイス能力を照会する。
	for ( uint32 Target = 0; Target < Desc.NumRenderTargets; ++Target )
	{
		if ( Desc.RenderTargetFormats[Target] == ERenderTargetColorFormat::RGBA16F )
		{
			bNeedsRGBA16F = true;
		}
		else if ( Desc.RenderTargetFormats[Target] == ERenderTargetColorFormat::R32Float )
		{
			bNeedsR32Float = true;
		}
		else if ( Desc.RenderTargetFormats[Target] != ERenderTargetColorFormat::RGBA8 )
		{
			return {};
		}
	}
	if ( bNeedsRGBA16F || bNeedsR32Float )
	{
		FRenderTargetCapabilities RtCaps;
		if ( !NDX12TextureFormat::GetRenderTargetCapabilities( Device.Get(), RtCaps ) || ( bNeedsRGBA16F && !RtCaps.bRGBA16FRenderTarget ) || ( bNeedsR32Float && ( !RtCaps.bR32FloatRenderTarget || ( Desc.bAlphaBlend && !RtCaps.bR32FloatBlendable ) ) ) )
		{
			return {};
		}
	}
	if ( ( Desc.DepthFormat != EGraphicsDepthFormat::None && Desc.DepthFormat != EGraphicsDepthFormat::D32Float ) || ( Desc.DepthFormat == EGraphicsDepthFormat::None && ( Desc.bDepthTest || Desc.bDepthWrite ) ) || ( !Desc.bDepthTest && Desc.bDepthWrite ) )
	{
		return {};
	}
	D3D12_COMPARISON_FUNC DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	D3D12_CULL_MODE CullMode = D3D12_CULL_MODE_NONE;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	D3D12_TEXTURE_ADDRESS_MODE AddressMode = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	if ( !MapComparisonFunc( Desc.DepthCompare, DepthFunc ) || !MapCullMode( Desc.Culling, CullMode ) || !MapTopologyType( Desc.Topology, TopologyType ) || !MapAddressMode( Desc.SamplerAddress, AddressMode ) )
	{
		return {};
	}
	bool UsedB[16] = {}; // space 0のb番号重複を防ぐ。
	bool UsedT[128] = {}; // space 0のt番号重複（バッファと画像両方）を防ぐ。
	for ( const FGraphicsBufferSlot &Slot : Desc.BufferSlots )
	{
		if ( Slot.Layout == EGraphicsBufferLayout::Constant )
		{
			if ( Slot.Binding >= 16 || Slot.ElementStride != 0 || UsedB[Slot.Binding] )
			{
				return {};
			}
			UsedB[Slot.Binding] = true;
		}
		else if ( Slot.Layout == EGraphicsBufferLayout::Raw || Slot.Layout == EGraphicsBufferLayout::Structured )
		{
			const bool bValidStride = Slot.Layout == EGraphicsBufferLayout::Raw ? ( Slot.ElementStride == 0 ) : ( Slot.ElementStride >= 4 && Slot.ElementStride <= 2048 && ( Slot.ElementStride % 4 ) == 0 );
			if ( Slot.Binding >= 128 || !bValidStride || UsedT[Slot.Binding] )
			{
				return {};
			}
			UsedT[Slot.Binding] = true;
		}
		else
		{
			return {};
		}
	}
	for ( const FGraphicsTextureSlot &Slot : Desc.TextureSlots )
	{
		if ( Slot.Binding >= 128 || ( Slot.Format != EComputeTextureFormat::RGBA8 && Slot.Format != EComputeTextureFormat::R32Float && Slot.Format != EComputeTextureFormat::RGBA16F ) || UsedT[Slot.Binding] )
		{
			return {};
		}
		UsedT[Slot.Binding] = true;
	}

	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> VsReflection, PsReflection; // VSとPSの両方をSM5.0/5.1で解析する。
	D3D12_SHADER_DESC VsDesc = {}, PsDesc = {};
	if ( !ReflectShaderStage( Desc.VertexShaderBytecode, MaxBytecodeBytes, D3D11_SHVER_VERTEX_SHADER, VsReflection, VsDesc ) || !ReflectShaderStage( Desc.PixelShaderBytecode, MaxBytecodeBytes, D3D11_SHVER_PIXEL_SHADER, PsReflection, PsDesc ) )
	{
		return {};
	}
	TArray<FString> SemanticNames; // PSO作成中だけ指すセマンティック文字列の保管先。
	TArray<D3D12_INPUT_ELEMENT_DESC> InputElements; // IAへ渡す頂点入力レイアウト。
	if ( !ValidateVertexInputs( Desc, VsReflection.Get(), VsDesc, SemanticNames, InputElements ) || !ValidateStageOutputs( Desc, VsReflection.Get(), VsDesc, PsReflection.Get(), PsDesc ) )
	{
		return {};
	}
	TArray<bool> BufferMatched, TextureMatched; // 宣言したすべてのスロットが使われているか。
	TArray<uint32> MinimumBytes; // VS/PSのうち大きい方のCB最小バイト数。
	BufferMatched.SetNum( Desc.BufferSlots.Num() );
	TextureMatched.SetNum( Desc.TextureSlots.Num() );
	MinimumBytes.SetNum( Desc.BufferSlots.Num() );
	for ( int32 i = 0; i < Desc.BufferSlots.Num(); ++i )
	{
		BufferMatched[i] = false;
		MinimumBytes[i] = 0;
	}
	for ( int32 i = 0; i < Desc.TextureSlots.Num(); ++i )
	{
		TextureMatched[i] = false;
	}
	if ( !ValidateStageBindings( VsReflection.Get(), VsDesc, Desc.BufferSlots, Desc.TextureSlots, BufferMatched, TextureMatched, MinimumBytes ) || !ValidateStageBindings( PsReflection.Get(), PsDesc, Desc.BufferSlots, Desc.TextureSlots, BufferMatched, TextureMatched, MinimumBytes ) )
	{
		return {};
	}
	for ( bool bUsed : BufferMatched )
	{
		if ( !bUsed )
		{
			return {};
		}
	}
	for ( bool bUsed : TextureMatched )
	{
		if ( !bUsed )
		{
			return {};
		}
	}

	FDX12GraphicsPipelineBinding Created; // PSOとRootSignatureが完成してから登録する。
	if ( !CreatePipelineRootSignature( Device.Get(), Desc, AddressMode, Created.Root ) )
	{
		return {};
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc = {}; // 検証済みのシェーダーと状態をそのままPSOへ設定する。
	PsoDesc.pRootSignature = Created.Root.Get();
	PsoDesc.VS = { Desc.VertexShaderBytecode.GetData(), static_cast<SIZE_T>( Desc.VertexShaderBytecode.Num() ) };
	PsoDesc.PS = { Desc.PixelShaderBytecode.GetData(), static_cast<SIZE_T>( Desc.PixelShaderBytecode.Num() ) };
	PsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if ( Desc.bAlphaBlend )
	{
		PsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
		PsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		PsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		PsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		PsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		PsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		PsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}
	PsoDesc.SampleMask = UINT_MAX;
	PsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	PsoDesc.RasterizerState.CullMode = CullMode;
	PsoDesc.RasterizerState.DepthClipEnable = TRUE;
	PsoDesc.DepthStencilState.DepthEnable = Desc.bDepthTest ? TRUE : FALSE;
	PsoDesc.DepthStencilState.DepthWriteMask = Desc.bDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	PsoDesc.DepthStencilState.DepthFunc = DepthFunc;
	PsoDesc.InputLayout = { InputElements.GetData(), static_cast<UINT>( InputElements.Num() ) };
	PsoDesc.PrimitiveTopologyType = TopologyType;
	PsoDesc.NumRenderTargets = Desc.NumRenderTargets;
	for ( uint32 Target = 0; Target < Desc.NumRenderTargets; ++Target )
	{
		PsoDesc.RTVFormats[Target] = Desc.RenderTargetFormats[Target] == ERenderTargetColorFormat::RGBA16F
			? DXGI_FORMAT_R16G16B16A16_FLOAT
			: ( Desc.RenderTargetFormats[Target] == ERenderTargetColorFormat::R32Float ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM );
		Created.RenderTargetFormats[Target] = Desc.RenderTargetFormats[Target];
	}
	PsoDesc.DSVFormat = Desc.DepthFormat == EGraphicsDepthFormat::D32Float ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
	PsoDesc.SampleDesc.Count = 1;
	if ( FAILED( Device->CreateGraphicsPipelineState( &PsoDesc, IID_PPV_ARGS( &Created.Pipeline ) ) ) )
	{
		return {};
	}
	Created.BufferSlots.SetNum( Desc.BufferSlots.Num() );
	for ( int32 i = 0; i < Desc.BufferSlots.Num(); ++i )
	{
		Created.BufferSlots[i] = Desc.BufferSlots[i];
	}
	Created.TextureSlots.SetNum( Desc.TextureSlots.Num() );
	for ( int32 i = 0; i < Desc.TextureSlots.Num(); ++i )
	{
		Created.TextureSlots[i] = Desc.TextureSlots[i];
	}
	Created.MinimumBytes = MoveTemp( MinimumBytes );
	Created.VertexStride = Desc.VertexStride;
	Created.NumRenderTargets = Desc.NumRenderTargets;
	Created.DepthFormat = Desc.DepthFormat;
	Created.Topology = Desc.Topology;

	const FGraphicsPipelineHandle Handle( NRenderResourceSerial::Allocate() ); // 再利用しない通し番号。
	if ( !Handle.IsValid() )
	{
		return {};
	}
	Pipelines.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

void CDX12GraphicsPipelineStore::ReleaseGraphicsPipeline( FGraphicsPipelineHandle Pipeline )
{
	if ( FDX12GraphicsPipelineBinding *Found = Pipelines.Find( Pipeline.Value ) )
	{
		Retired.Add( MoveTemp( *Found ) );
		Pipelines.Remove( Pipeline.Value );
	}
}

bool CDX12GraphicsPipelineStore::AcquirePipeline( FGraphicsPipelineHandle Pipeline, FDX12GraphicsPipelineBinding &Out ) const
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	const FDX12GraphicsPipelineBinding *Found = Pipelines.Find( Pipeline.Value ); // 解放済み・別所有先の番号を除外する。
	if ( !Found || !Found->Pipeline || !Found->Root )
	{
		return false;
	}
	Out = *Found;
	return true;
}
