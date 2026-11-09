// SPDX-License-Identifier: MIT
#include "DX12AssetBackendSubsystem.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Core/File/File.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "VendorModules/ImGui/imgui_impl_dx12.h"
#include "Library/Platform/Windows/Image/WicImageDecoder.h"

REGISTER_SUBSYSTEM( CDX12AssetBackendSubsystem )

bool CDX12AssetBackendSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CDX12AssetBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Platform = Collection.InitializeDependency<IPlatform>();
}

void CDX12AssetBackendSubsystem::DestroyTexture( ImTextureData* Texture )
{
	Texture->WantDestroyNextFrame = true;
	Texture->UnusedFrames = 2;
	Texture->SetStatus( ImTextureStatus_WantDestroy );
	ImGui_ImplDX12_UpdateTexture( Texture );
	delete Texture;
}

void CDX12AssetBackendSubsystem::Deinitialize()
{
	// フレームの提出後でも資源を安全に解放できるよう、プラットフォームの待ち窓口を使う。
	if ( auto* Frame = dynamic_cast<IRenderSurface*>( Platform ) )
	{
		Frame->WaitForGPU();
	}
	for ( auto& Pair : Textures )
	{
		DestroyTexture( Pair.Value );
	}
	for ( auto& Item : Retired )
	{
		DestroyTexture( Item.Texture );
	}
	Textures.Reset();
	Retired.Reset();
	Platform = nullptr;
}

void CDX12AssetBackendSubsystem::Tick( float32 )
{
	// BeginFrameはGPU完了を待ってから入力・アセットのTickへ進む。
	for ( int32 Index = Retired.Num() - 1; Index >= 0; --Index )
	{
		if ( Platform && Retired[Index].Frame < Platform->GetFrameCount() )
		{
			DestroyTexture( Retired[Index].Texture );
			Retired.RemoveAt( Index );
		}
	}
}

int32 CDX12AssetBackendSubsystem::LoadTexture( const FString& Path, bool )
{
	TArray<uint8> Bytes; // ファイルの内容。
	return NFile::ReadAllBytes( Path, Bytes ) ? CreateTextureFromMemory( Bytes.GetData(), Bytes.Num() ) : NAsset::InvalidHandle;
}

int32 CDX12AssetBackendSubsystem::CreateTextureFromMemory( const void* FileImage, usize Size )
{
	int32 Width = 0, Height = 0; // 展開した大きさ。
	TArray<FColor32> Pixels; // 展開したRGBA。
	return DecodeImage( FileImage, Size, Width, Height, Pixels ) ? CreateTextureFromPixels( Width, Height, Pixels.GetData() ) : NAsset::InvalidHandle;
}

int32 CDX12AssetBackendSubsystem::CreateTextureFromPixels( int32 Width, int32 Height, const FColor32* Pixels )
{
	if ( !Platform || !Platform->IsInitialized() || !Pixels || Width < 1 || Height < 1 || Width > 16384 || Height > 16384
		|| static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || Textures.Num() + Retired.Num() >= MaxTextureHandles || NextHandle == TNumericLimits<int32>::Max() )
	{
		return NAsset::InvalidHandle;
	}
	auto* Texture = new ImTextureData(); // 自前で所有し、ImGuiのフォント一覧には登録しない。
	Texture->Create( ImTextureFormat_RGBA32, Width, Height );
	FMemory::Memcpy( Texture->Pixels, Pixels, static_cast<usize>( Width ) * Height * sizeof( FColor32 ) );
	ImGui_ImplDX12_UpdateTexture( Texture );
	if ( Texture->Status != ImTextureStatus_OK )
	{
		DestroyTexture( Texture );
		return NAsset::InvalidHandle;
	}
	const int32 Handle = NextHandle++; // この登録だけに使う番号。
	Textures.Add( Handle, Texture );
	return Handle;
}

bool CDX12AssetBackendSubsystem::UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels )
{
	ImTextureData* Image = FindTexture( Texture ); // 更新先。
	if ( !Image || !Pixels || Width != Image->Width || Height != Image->Height )
	{
		return false;
	}
	FMemory::Memcpy( Image->Pixels, Pixels, static_cast<usize>( Width ) * Height * sizeof( FColor32 ) );
	Image->UpdateRect = { 0, 0, static_cast<unsigned short>( Width ), static_cast<unsigned short>( Height ) };
	Image->Updates.resize( 0 );
	Image->Updates.push_back( Image->UpdateRect );
	Image->SetStatus( ImTextureStatus_WantUpdates );
	ImGui_ImplDX12_UpdateTexture( Image );
	return Image->Status == ImTextureStatus_OK;
}

bool CDX12AssetBackendSubsystem::GetTextureDrawView( int32 Handle, FTextureDrawView& Out )
{
	ImTextureData* Image = FindTexture( Handle ); // 登録済みの画像。
	if ( !Image )
	{
		return false;
	}
	Out = { Image->GetTexID(), Image->Width, Image->Height };
	return true;
}

ImTextureData* CDX12AssetBackendSubsystem::FindTexture( int32 Handle )
{
	ImTextureData** Found = Textures.Find( Handle ); // 登録済みの画像。
	return Found ? *Found : nullptr;
}

bool CDX12AssetBackendSubsystem::GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight )
{
	ImTextureData* Image = FindTexture( Texture ); // 登録済みの画像。
	if ( !Image )
	{
		return false;
	}
	OutWidth = Image->Width;
	OutHeight = Image->Height;
	return true;
}

void CDX12AssetBackendSubsystem::DeleteTexture( int32 Texture )
{
	if ( ImTextureData* Image = FindTexture( Texture ) )
	{
		Retired.Add( { Image, Platform ? Platform->GetFrameCount() : 0 } );
		Textures.Remove( Texture );
	}
}

bool CDX12AssetBackendSubsystem::DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	TArray<uint8> Bytes; // 読んだ内容。
	return NFile::ReadAllBytes( Path, Bytes ) && DecodeImage( Bytes.GetData(), Bytes.Num(), OutWidth, OutHeight, OutPixels );
}

bool CDX12AssetBackendSubsystem::DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	return FWicImageDecoder::Decode( FileImage, Size, OutWidth, OutHeight, OutPixels );
}

FBackendMemoryInfo CDX12AssetBackendSubsystem::GetMemoryInfo()
{
	FBackendMemoryInfo Info; // GPU容量は不明、登録数だけ正確に返す。
	Info.NumTextureHandles = Textures.Num();
	return Info;
}

int64 CDX12AssetBackendSubsystem::EstimateModelBytes( int32 /*Model*/ )
{
	return 0;
}

int64 CDX12AssetBackendSubsystem::EstimateSoundBytes( int32 /*Sound*/, ESoundLoadMode /*Mode*/ )
{
	return 0;
}

bool CDX12AssetBackendSubsystem::IsLoading( int32 /*Handle*/ )
{
	return false;
}

int32 CDX12AssetBackendSubsystem::GetNumLoading()
{
	return 0;
}

int32 CDX12AssetBackendSubsystem::LoadModel( const FString& /*Path*/, bool /*bAsync*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "LoadModel" );
	return NAsset::InvalidHandle;
}

int32 CDX12AssetBackendSubsystem::DuplicateModel( int32 /*Source*/ )
{
	return NAsset::InvalidHandle;
}

void CDX12AssetBackendSubsystem::DeleteModel( int32 /*Model*/ )
{
}

int32 CDX12AssetBackendSubsystem::GetModelAnimCount( int32 /*Model*/ )
{
	return 0;
}

FString CDX12AssetBackendSubsystem::GetModelAnimName( int32 /*Model*/, int32 /*AnimIndex*/ )
{
	return {};
}

float32 CDX12AssetBackendSubsystem::GetModelAnimLength( int32 /*Model*/, int32 /*AnimIndex*/ )
{
	return 0;
}

int32 CDX12AssetBackendSubsystem::AttachModelAnim( int32 /*Model*/, int32 /*AnimIndex*/, int32 /*SourceModel*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "AttachModelAnim" );
	return NAsset::InvalidHandle;
}

void CDX12AssetBackendSubsystem::DetachModelAnim( int32 /*Model*/, int32 /*AttachIndex*/ )
{
}

int32 CDX12AssetBackendSubsystem::GetModelMeshCount( int32 /*Model*/ )
{
	return 0;
}

int32 CDX12AssetBackendSubsystem::GetModelFrameCount( int32 /*Model*/ )
{
	return 0;
}

FString CDX12AssetBackendSubsystem::GetModelFrameName( int32 /*Model*/, int32 /*FrameIndex*/ )
{
	return {};
}

bool CDX12AssetBackendSubsystem::SaveModelToMV1( int32 /*Model*/, const FString& /*Path*/, const FModelSaveOptions& /*Options*/, FString& /*OutWarning*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "SaveModelToMV1" );
	return false;
}

int32 CDX12AssetBackendSubsystem::MakeFont( const FFontDesc& /*Desc*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "MakeFont" );
	return NAsset::InvalidHandle;
}

void CDX12AssetBackendSubsystem::DeleteFont( int32 /*Font*/ )
{
}

int32 CDX12AssetBackendSubsystem::MeasureText( int32 /*Font*/, const char* /*Text*/ )
{
	return 0;
}

bool CDX12AssetBackendSubsystem::AddFontFile( const FString& /*Path*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "AddFontFile" );
	return false;
}

void CDX12AssetBackendSubsystem::RemoveFontFile( const FString& /*Path*/ )
{
}

int32 CDX12AssetBackendSubsystem::LoadSound( const FString& /*Path*/, ESoundLoadMode /*Mode*/, bool /*bAsync*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "LoadSound" );
	return NAsset::InvalidHandle;
}

int32 CDX12AssetBackendSubsystem::DuplicateSound( int32 /*Source*/ )
{
	return NAsset::InvalidHandle;
}

void CDX12AssetBackendSubsystem::DeleteSound( int32 /*Sound*/ )
{
}

float32 CDX12AssetBackendSubsystem::GetSoundLength( int32 /*Sound*/ )
{
	return 0;
}
