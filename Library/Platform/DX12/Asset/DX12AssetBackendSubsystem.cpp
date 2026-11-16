// SPDX-License-Identifier: MIT
#include "Library/Platform/Render/Texture/TgaDecoder.h"
#include "DX12AssetBackendSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Core/File/File.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/Windows/Image/WicImageDecoder.h"
#include "Library/Platform/Windows/Image/WicImageEncoder.h"
#include "Library/Framework/Asset/Model/Import/FbxImporter.h"
#include "Library/Framework/Asset/Model/Import/ModelFormats.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Core/Log/Log.h"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/Core/Interfaces/Render/IGpuMemoryInfo.h"
#include "Library/Platform/Windows/Audio/WindowsSoundStoreSubsystem.h"

#include <windows.h>
#include <psapi.h>

REGISTER_SUBSYSTEM( CDX12AssetBackendSubsystem )

namespace
{
	CDX12AssetBackendSubsystem* GImGuiTextureOwner = nullptr; // ImGui の画像の番号を作るときに、画像の登録を引く所有先。

	// 画像の番号を、ImGui::Image / AddImage に渡せる値（画像のGPU記述子。ImGuiの描画と同じheapにある）にする。無効な番号は 0
	ImTextureID GetDX12ImGuiTexture( int32 GraphHandle )
	{
		FTextureDrawView View;
		return GImGuiTextureOwner && GImGuiTextureOwner->GetTextureDrawView( GraphHandle, View ) && View.Binding != 0 ? static_cast<ImTextureID>( View.Binding ) : static_cast<ImTextureID>( 0 );
	}
}

bool CDX12AssetBackendSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CDX12AssetBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	if ( auto* Resources = Collection.InitializeDependency<IRenderTextureResources>() )
	{
		Textures.Initialize( *Resources );
	}
	GpuMemory = Collection.InitializeDependency<IGpuMemoryInfo>();
	SoundStore = Collection.InitializeDependency<CWindowsSoundStoreSubsystem>(); // 音の登録（DX12のとき作られる）。
	ShaderService.Initialize( Collection.InitializeDependency<IGraphicsPipelineResources>(), Collection.InitializeDependency<IRenderBufferResources>(), &Textures );
	GImGuiTextureOwner = this; // エディタのプレビューなどが、画像を ImGui へ表示できるようにする。
	NDebugUI::FImGuiTextureCallbacks Callbacks;
	Callbacks.GetTexture = &GetDX12ImGuiTexture;
	NDebugUI::SetImGuiTextureCallbacks( Callbacks );
}

void CDX12AssetBackendSubsystem::Deinitialize()
{
	NDebugUI::SetImGuiTextureCallbacks( NDebugUI::FImGuiTextureCallbacks() );
	GImGuiTextureOwner = nullptr;
	SoundStore = nullptr;
	GpuMemory = nullptr;
	ShaderService.Shutdown(); // パイプラインと空の画像を、先に手放す。
	Models.Reset(); // 共有モデルが所有する画像を、先に登録から外す。
	Textures.Shutdown(); // 資源の所有者がGPUの完了と解放を行う。
	Buffers.Reset();
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
	return Textures.Create( Width, Height, Pixels );
}

int32 CDX12AssetBackendSubsystem::CreateRenderTarget( const FRenderTargetDesc& Desc )
{
	if ( !Desc.IsValid() )
	{
		return NAsset::InvalidHandle;
	}
	// 8ビットのRGBA、16ビット浮動小数点のRGBA（HDR）、32ビット浮動小数点の1チャンネルだけ作れる。
	ERenderTargetColorFormat Format = ERenderTargetColorFormat::RGBA8;
	if ( Desc.Format == ERenderTargetFormat::Float16 && Desc.Channels == 4 )
	{
		Format = ERenderTargetColorFormat::RGBA16F;
	}
	else if ( Desc.Format == ERenderTargetFormat::Float32 && Desc.Channels == 1 )
	{
		Format = ERenderTargetColorFormat::R32Float;
	}
	else if ( Desc.Format != ERenderTargetFormat::Color8 )
	{
		NBackendSupport::ReportUnsupported( "DX12", "CreateRenderTarget (this floating point format)", "Only Float16 x 4 channels and Float32 x 1 channel are supported" );
		return NAsset::InvalidHandle;
	}
	if ( Desc.MultiSamples > 0 )
	{
		NBackendSupport::ReportUnsupported( "DX12", "CreateRenderTarget (multisampling)", "The render target is created without multisampling" );
	}
	// 透明度を持たない描画先は、不透明な黒から始める（描いていない所も、不透明）。
	return Textures.CreateRenderTarget( Desc.Width, Desc.Height, Desc.bDepth, Format, Desc.bAlpha ? FColor32( 0, 0, 0, 0 ) : FColor32( 0, 0, 0, 255 ) );
}

bool CDX12AssetBackendSubsystem::ReadRenderTargetPixels( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels )
{
	TArray<FColor32> All; // 描画先の全体。
	int32 FullWidth = 0, FullHeight = 0; // 描画先の大きさ。
	if ( X < 0 || Y < 0 || Width <= 0 || Height <= 0 || !Textures.ReadRenderTarget( Target, All, FullWidth, FullHeight ) || static_cast<int64>( X ) + Width > FullWidth || static_cast<int64>( Y ) + Height > FullHeight )
	{
		return false;
	}
	TArray<FColor32> Pixels; // 矩形の中だけ。
	Pixels.SetNum( Width * Height );
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		for ( int32 Column = 0; Column < Width; ++Column )
		{
			Pixels[Row * Width + Column] = All[( Y + Row ) * FullWidth + X + Column];
		}
	}
	OutPixels = MoveTemp( Pixels );
	return true;
}

bool CDX12AssetBackendSubsystem::SaveRenderTarget( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options )
{
	TArray<FColor32> Pixels; // 保存する矩形。
	return ReadRenderTargetPixels( Target, X, Y, Width, Height, Pixels ) && SavePixels( Width, Height, Pixels.GetData(), Path, Options );
}

bool CDX12AssetBackendSubsystem::GetRenderTargetDrawView( int32 Handle, FRenderTargetDrawView& Out )
{
	return Textures.GetRenderTargetDrawView( Handle, Out );
}

bool CDX12AssetBackendSubsystem::AcquireScratchTarget( int32 Width, int32 Height, int32& OutHandle )
{
	return Textures.AcquireScratchTarget( Width, Height, OutHandle );
}

bool CDX12AssetBackendSubsystem::UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels )
{
	return Textures.Update( Texture, Width, Height, Pixels );
}

bool CDX12AssetBackendSubsystem::GetTextureDrawView( int32 Handle, FTextureDrawView& Out )
{
	return Textures.GetTextureDrawView( Handle, Out );
}

int32 CDX12AssetBackendSubsystem::CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	return Textures.CreateRegion( Source, X, Y, Width, Height );
}

bool CDX12AssetBackendSubsystem::GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] )
{
	return Textures.GetRegionUvBounds( Texture, OutUV );
}

bool CDX12AssetBackendSubsystem::GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight )
{
	return Textures.GetSize( Texture, OutWidth, OutHeight );
}

void CDX12AssetBackendSubsystem::DeleteTexture( int32 Texture )
{
	Textures.Delete( Texture );
}

bool CDX12AssetBackendSubsystem::DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	TArray<uint8> Bytes; // 読んだ内容。
	return NFile::ReadAllBytes( Path, Bytes ) && DecodeImage( Bytes.GetData(), Bytes.Num(), OutWidth, OutHeight, OutPixels );
}

bool CDX12AssetBackendSubsystem::DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	// WICはTGAを読めないので、WICで読めなかったときだけ、TGAとして読む（エフェクトの組み込みの丸など）。
	return FWicImageDecoder::Decode( FileImage, Size, OutWidth, OutHeight, OutPixels ) || NTgaDecoder::Decode( FileImage, Size, OutWidth, OutHeight, OutPixels );
}

FBackendMemoryInfo CDX12AssetBackendSubsystem::GetMemoryInfo()
{
	FBackendMemoryInfo Info; // GPU容量は不明（0）。このプロセスの物理メモリと登録数は、実測して返す。
	PROCESS_MEMORY_COUNTERS Counters = {};
	Counters.cb = sizeof( Counters );
	if ( GetProcessMemoryInfo( GetCurrentProcess(), &Counters, sizeof( Counters ) ) )
	{
		Info.ProcessWorkingSet = static_cast<int64>( Counters.WorkingSetSize );
	}
	Info.NumTextureHandles = Textures.Num();
	Info.NumModelHandles = Models.Num();
	int64 Used = 0;
	int64 Budget = 0;
	if ( GpuMemory && GpuMemory->QueryGpuMemory( Used, Budget ) && Budget > 0 )
	{
		Info.VideoMemoryUsed = Used;
		Info.VideoMemoryTotal = Budget;
	}
	Info.NumSoundHandles = SoundStore ? SoundStore->Num() : 0;
	return Info;
}

bool CDX12AssetBackendSubsystem::SavePixels( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options )
{
	return FWicImageEncoder::Save( Width, Height, Pixels, Path, Options );
}

int64 CDX12AssetBackendSubsystem::EstimateModelBytes( int32 Model )
{
	return Models.EstimateBytes( Model );
}

int64 CDX12AssetBackendSubsystem::EstimateSoundBytes( int32 Sound, ESoundLoadMode /*Mode*/ )
{
	return SoundStore ? SoundStore->EstimateBytes( Sound ) : 0;
}

bool CDX12AssetBackendSubsystem::IsLoading( int32 /*Handle*/ )
{
	return false;
}

int32 CDX12AssetBackendSubsystem::GetNumLoading()
{
	return 0;
}

int32 CDX12AssetBackendSubsystem::LoadModel( const FString& Path, bool /*bAsync*/ )
{
	const int64 Size = NFileSystem::GetFileSize( Path ); // 解析前に元ファイルの大きさを制限する。
	TArray<uint8> Bytes; // 同期で読み取る元ファイル。
	if ( !CanLoadModelSource( Path ) || Size <= 0 || Size > 64 * 1024 * 1024 || !NFile::ReadAllBytes( Path, Bytes ) || Bytes.Num() > 64 * 1024 * 1024 )
	{
		LOG( LogDX12, Error, "Cannot read static FBX/OBJ model '%s' (unsupported format or file size)", *Path );
		return NAsset::InvalidHandle;
	}
	FImportedScene Scene; // 既存の中間形式を使い、GPUをインポーターへ渡さない。
	FString Error; // 失敗理由を読み込み元と一緒に出す。
	FImportOptions Options; // 動くモデルを静的な形へ誤変換しない。
	Options.bRequireStatic = true;
	Options.bImportAnimations = false;
	if ( !NModelImport::ImportFromMemory( Bytes.GetData(), Bytes.Num(), Path, Options, Scene, Error ) )
	{
		LOG( LogDX12, Error, "Cannot import static model '%s': %s", *Path, *Error );
		return NAsset::InvalidHandle;
	}
	NImportedScene::ConvertToLeftHanded( Scene );
	const int32 Handle = Models.Create( Scene, Path, *this, Error ); // 失敗時は途中の画像も片付ける。
	if ( Handle < 0 )
	{
		LOG( LogDX12, Error, "Cannot prepare static model '%s': %s", *Path, *Error );
	}
	return Handle;
}

int32 CDX12AssetBackendSubsystem::DuplicateModel( int32 Source )
{
	return Models.Duplicate( Source );
}

void CDX12AssetBackendSubsystem::DeleteModel( int32 Model )
{
	Models.Delete( Model );
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

int32 CDX12AssetBackendSubsystem::GetModelMeshCount( int32 Model )
{
	const auto* Definition = Models.GetDefinition( Model ); // 準備済みの静的定義。
	return Definition ? Definition->NumMeshes : 0;
}

int32 CDX12AssetBackendSubsystem::GetModelFrameCount( int32 Model )
{
	const auto* Definition = Models.GetDefinition( Model ); // 準備済みの静的定義。
	return Definition ? Definition->Nodes.Num() : 0;
}

FString CDX12AssetBackendSubsystem::GetModelFrameName( int32 Model, int32 FrameIndex )
{
	const auto* Definition = Models.GetDefinition( Model ); // 準備済みの静的定義。
	return Definition && FrameIndex >= 0 && FrameIndex < Definition->Nodes.Num() ? Definition->Nodes[FrameIndex].Name : FString();
}

bool CDX12AssetBackendSubsystem::SaveModelToMV1( int32 /*Model*/, const FString& /*Path*/, const FModelSaveOptions& /*Options*/, FString& /*OutWarning*/ )
{
	NBackendSupport::ReportUnsupported( "DX12", "SaveModelToMV1" );
	return false;
}

int32 CDX12AssetBackendSubsystem::MakeShader( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	return ShaderService.Make( Stage, Code, NumBytes );
}

void CDX12AssetBackendSubsystem::DeleteShader( int32 Shader )
{
	ShaderService.Delete( Shader );
}

bool CDX12AssetBackendSubsystem::CompileShader( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage )
{
	return ShaderService.Compile( Stage, Source, NumBytes, SourceName, EntryPoint, ShaderModel, OutCode, OutMessage );
}

bool CDX12AssetBackendSubsystem::IsShaderValid( int32 Shader, EShaderStage Stage ) const
{
	return ShaderService.IsShaderValid( Shader, Stage );
}

bool CDX12AssetBackendSubsystem::DrawShader( const FShaderDrawRequest& Request, IRenderGeometry3D& Geometry )
{
	return ShaderService.DrawShader( Request, Geometry );
}

void CDX12AssetBackendSubsystem::SetShadowPass( int32 ShadowMap, const FMatrix4x4& LightViewProjection, float32 AdjustDepth, int32 Width, int32 Height )
{
	ShaderService.SetShadowPass( ShadowMap, LightViewProjection, AdjustDepth, Width, Height );
}

bool CDX12AssetBackendSubsystem::GetShadowPass( int32 ShadowMap, FMatrix4x4& OutLightViewProjection, float32& OutAdjustDepth, int32& OutWidth, int32& OutHeight ) const
{
	return ShaderService.GetShadowPass( ShadowMap, OutLightViewProjection, OutAdjustDepth, OutWidth, OutHeight );
}

int32 CDX12AssetBackendSubsystem::MakeShadowMap( int32 Width, int32 Height )
{
	if ( Width < 1 || Height < 1 || Width > NDrawShadowMap::MaxSize || Height > NDrawShadowMap::MaxSize )
	{
		return NAsset::InvalidHandle;
	}
	// 深度（光から見た距離）を、R32Floatの色として書く。いちばん遠い（1）で始める。
	return Textures.CreateRenderTarget( Width, Height, true, ERenderTargetColorFormat::R32Float, FColor32( 255, 255, 255, 255 ) );
}

void CDX12AssetBackendSubsystem::DeleteShadowMap( int32 ShadowMap )
{
	ShaderService.ForgetShadowMap( ShadowMap );
	Textures.Delete( ShadowMap );
}

bool CDX12AssetBackendSubsystem::GetShadowMapViewProjection( int32 ShadowMap, float32 ( &OutMatrix )[16] )
{
	FMatrix4x4 Matrix;
	float32 Adjust = 0.0f;
	int32 Width = 0, Height = 0;
	if ( !ShaderService.GetShadowPass( ShadowMap, Matrix, Adjust, Width, Height ) )
	{
		return false;
	}
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 4; ++Column )
		{
			OutMatrix[Row * 4 + Column] = Matrix.m[Row][Column];
		}
	}
	return true;
}

int32 CDX12AssetBackendSubsystem::MakeFont( const FFontDesc& Desc )
{
	return Fonts.MakeFont( Desc );
}

void CDX12AssetBackendSubsystem::DeleteFont( int32 Font )
{
	Fonts.DeleteFont( Font );
}

int32 CDX12AssetBackendSubsystem::MeasureText( int32 Font, const char* Text )
{
	return Fonts.MeasureText( Font, Text );
}

bool CDX12AssetBackendSubsystem::GetFontMetrics( int32 Font, FFontMetrics& OutMetrics )
{
	return Fonts.GetFontMetrics( Font, OutMetrics );
}

bool CDX12AssetBackendSubsystem::MeasureTextSize( int32 Font, const char* Text, FTextSize& OutSize )
{
	return Fonts.MeasureTextSize( Font, Text, OutSize );
}

bool CDX12AssetBackendSubsystem::GetTextGlyphs( int32 Font, const char* Text, TArray<FTextGlyph>& OutGlyphs )
{
	return Fonts.GetTextGlyphs( Font, Text, OutGlyphs );
}

bool CDX12AssetBackendSubsystem::IsFontAvailable( const FString& Face )
{
	return Fonts.IsFontAvailable( Face );
}

void CDX12AssetBackendSubsystem::GetFontNames( TArray<FString>& OutNames )
{
	Fonts.GetFontNames( OutNames );
}

bool CDX12AssetBackendSubsystem::AddFontFile( const FString& Path )
{
	return Fonts.AddFontFile( Path );
}

void CDX12AssetBackendSubsystem::RemoveFontFile( const FString& Path )
{
	Fonts.RemoveFontFile( Path );
}

bool CDX12AssetBackendSubsystem::AddFontCharImage( int32 Font, const char* Character, int32 Image, int32 DrawX, int32 DrawY, int32 Advance )
{
	int32 Width = 0, Height = 0;
	return Textures.GetSize( Image, Width, Height ) && Fonts.AddCharImage( Font, Character, Image, DrawX, DrawY, Advance );
}

void CDX12AssetBackendSubsystem::RemoveFontCharImage( int32 Font, const char* Character )
{
	Fonts.RemoveCharImage( Font, Character );
}

bool CDX12AssetBackendSubsystem::GetFontImagePlacements( int32 Font, const char* Text, int32 Length, TArray<FFontImagePlacement>& Out )
{
	return Fonts.GetFontImagePlacements( Font, Text, Length, Out );
}

bool CDX12AssetBackendSubsystem::GetFontDrawView( int32 Font, FRenderTextFont& Out )
{
	return Fonts.GetFontDrawView( Font, Out );
}

bool CDX12AssetBackendSubsystem::MeasureFontText( int32 Font, const char* Text, int32 Length, FTextSize& Out )
{
	return Fonts.MeasureFontText( Font, Text, Length, Out );
}

int32 CDX12AssetBackendSubsystem::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	return SoundStore ? SoundStore->LoadSound( Path, Mode, bAsync ) : NAsset::InvalidHandle;
}

int32 CDX12AssetBackendSubsystem::DuplicateSound( int32 Source )
{
	return SoundStore ? SoundStore->DuplicateSound( Source ) : NAsset::InvalidHandle;
}

void CDX12AssetBackendSubsystem::DeleteSound( int32 Sound )
{
	if ( SoundStore )
	{
		SoundStore->DeleteSound( Sound );
	}
}

float32 CDX12AssetBackendSubsystem::GetSoundLength( int32 Sound )
{
	return SoundStore ? SoundStore->GetSoundLength( Sound ) : -1.0f;
}

bool CDX12AssetBackendSubsystem::GetSoundInfo( int32 Sound, FSoundInfo& Out )
{
	return SoundStore && SoundStore->GetSoundInfo( Sound, Out );
}

int32 CDX12AssetBackendSubsystem::CreateSoundFromPcm( const FPcmBuffer& Pcm )
{
	return SoundStore ? SoundStore->CreateSoundFromPcm( Pcm ) : NAsset::InvalidHandle;
}

bool CDX12AssetBackendSubsystem::DecodeSoundFile( const FString& Path, FPcmBuffer& Out )
{
	return SoundStore && SoundStore->DecodeSoundFile( Path, Out );
}

TArray<FString> CDX12AssetBackendSubsystem::GetReadableSoundExtensions() const
{
	return SoundStore ? SoundStore->GetReadableSoundExtensions() : TArray<FString>();
}

bool CDX12AssetBackendSubsystem::CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer )
{
	if ( !Vertices || !Indices || NumVertices <= 0 || NumVertices > 1048576 || NumIndices <= 0 || NumIndices > 16777216 || NumIndices % 3 != 0 || static_cast<uint64>( NumVertices ) * sizeof( FDrawVertex3D ) + static_cast<uint64>( NumIndices ) * sizeof( uint32 ) > 64 * 1024 * 1024 )
	{
		return false;
	}
	TArray<FDrawVertex3D> Converted; // 編集メッシュの値を共通頂点へ移す。
	Converted.SetNum( NumVertices );
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		FMemory::Memcpy( Converted[Index].Position, Vertices[Index].Position, sizeof( Vertices[Index].Position ) );
		FMemory::Memcpy( Converted[Index].Normal, Vertices[Index].Normal, sizeof( Vertices[Index].Normal ) );
		FMemory::Memcpy( Converted[Index].Diffuse, Vertices[Index].Color, sizeof( Vertices[Index].Color ) );
		Converted[Index].U = Vertices[Index].U;
		Converted[Index].V = Vertices[Index].V;
	}
	return Buffers.CreateMesh( Converted.GetData(), NumVertices, Indices, NumIndices, OutVertexBuffer, OutIndexBuffer );
}

int32 CDX12AssetBackendSubsystem::MakeVertexBuffer( int32 NumVertices )
{
	return Buffers.MakeVertexBuffer( NumVertices );
}

bool CDX12AssetBackendSubsystem::WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices )
{
	return Buffers.WriteVertexBuffer( VertexBuffer, StartIndex, Vertices, NumVertices );
}

void CDX12AssetBackendSubsystem::DeleteVertexBuffer( int32 VertexBuffer )
{
	Buffers.DeleteVertexBuffer( VertexBuffer );
}

int32 CDX12AssetBackendSubsystem::MakeIndexBuffer( int32 NumIndices, bool b32Bit )
{
	return Buffers.MakeIndexBuffer( NumIndices, b32Bit );
}

bool CDX12AssetBackendSubsystem::WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices )
{
	return Buffers.WriteIndexBuffer( IndexBuffer, StartIndex, Indices, NumIndices );
}

void CDX12AssetBackendSubsystem::DeleteIndexBuffer( int32 IndexBuffer )
{
	Buffers.DeleteIndexBuffer( IndexBuffer );
}

bool CDX12AssetBackendSubsystem::GetVertexBufferView( int32 Handle, FGeometryVertexBufferView& Out )
{
	return Buffers.GetVertexBufferView( Handle, Out );
}

bool CDX12AssetBackendSubsystem::GetIndexBufferView( int32 Handle, FGeometryIndexBufferView& Out )
{
	return Buffers.GetIndexBufferView( Handle, Out );
}

bool CDX12AssetBackendSubsystem::CanLoadModelSource( const FString& Path ) const
{
	return NModelFormat::Classify( Path ) == EModelFileKind::Imported;
}

bool CDX12AssetBackendSubsystem::GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out )
{
	return Models.GetStaticModelDrawView( Handle, Out );
}

bool CDX12AssetBackendSubsystem::GetStaticModelSurface( int32 Model, FStaticModelSurfaceData& Out )
{
	return Models.GetSurface( Model, Out );
}

bool CDX12AssetBackendSubsystem::GetModelBounds( int32 Model, float32 ( &OutMin )[3], float32 ( &OutMax )[3] )
{
	const auto* Definition = Models.GetDefinition( Model ); // モデル座標での箱。
	if ( !Definition )
	{
		return false;
	}
	OutMin[0] = Definition->BoundsMin.x;
	OutMin[1] = Definition->BoundsMin.y;
	OutMin[2] = Definition->BoundsMin.z;
	OutMax[0] = Definition->BoundsMax.x;
	OutMax[1] = Definition->BoundsMax.y;
	OutMax[2] = Definition->BoundsMax.z;
	return true;
}

bool CDX12AssetBackendSubsystem::GetModelCollisionMesh( int32 Model, TArray<float32>& OutPositions, TArray<uint32>& OutIndices )
{
	const auto* Definition = Models.GetDefinition( Model ); // 描画と同じ静的面。
	if ( !Definition )
	{
		return false;
	}
	TArray<float32> Positions; // 成功するまで出力を変えない。
	Positions.Reserve( Definition->Surface.Vertices.Num() * 3 );
	for ( const auto& Vertex : Definition->Surface.Vertices )
	{
		for ( float32 Value : Vertex.Position )
		{
			Positions.Add( Value );
		}
	}
	OutPositions = MoveTemp( Positions );
	OutIndices = Definition->Surface.Indices;
	return true;
}

bool CDX12AssetBackendSubsystem::GetModelOpacity( int32 Model, float32& Out )
{
	return Models.GetOpacity( Model, Out );
}

bool CDX12AssetBackendSubsystem::SetModelOpacity( int32 Model, float32 Opacity )
{
	return Models.SetOpacity( Model, Opacity );
}

bool CDX12AssetBackendSubsystem::GetModelFrameMatrix( int32 Model, int32 Frame, EModelFrameSpace Space, FMatrix4x4& Out )
{
	const auto* Definition = Models.GetDefinition( Model ); // 最後の描画配置を追跡しないためWorldは答えない。
	if ( !Definition || Frame < 0 || Frame >= Definition->Nodes.Num() || Space != EModelFrameSpace::Local )
	{
		return false;
	}
	FMemory::Memcpy( Out.m, Definition->Nodes[Frame].Local.M, sizeof( Out.m ) );
	return true;
}

bool CDX12AssetBackendSubsystem::GetModelStructure( int32 Model, FModelStructure& Out )
{
	const auto* Definition = Models.GetDefinition( Model ); // 静的な読み取り専用構造。
	if ( !Definition )
	{
		return false;
	}
	Out = Definition->Structure;
	return true;
}

bool CDX12AssetBackendSubsystem::GetModelMaterialColors( int32 Model, int32 Material, FModelMaterialColors& Out )
{
	const auto* Definition = Models.GetDefinition( Model ); // 形状へ色を反映済みの材質。
	if ( !Definition || Material < 0 || Material >= Definition->Structure.Materials.Num() )
	{
		return false;
	}
	Out = Definition->Structure.Materials[Material].Colors;
	return true;
}
