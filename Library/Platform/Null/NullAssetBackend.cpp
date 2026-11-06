// SPDX-License-Identifier: MIT
#include "NullAssetBackend.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogNullAsset, Log )

REGISTER_SUBSYSTEM( CNullAssetBackend )

namespace
{
	// 画像の幅・高さの上限（DxLib / Direct3D 11 と同じ）
	constexpr int32 MaxTextureSize = 16384;
	// 画素の総数の上限（FPixelImage と同じ）
	constexpr int64 MaxPixelCount = 64 * 1024 * 1024;
	// ヘッダーを読めない形式の画像の大きさ
	constexpr int32 FallbackTextureSize = 64;
	// 音の長さが分からないときの秒数
	constexpr float32 FallbackSoundLength = 1.0f;
	// フォントの大きさの上限
	constexpr int32 MaxFontSize = 4096;
	// この実装が「ある」と答えるフォントの名前
	const char* const NullFontName = "NullSans";

	uint32 ReadBe16( const uint8* Data )
	{
		return ( static_cast<uint32>( Data[0] ) << 8 ) | static_cast<uint32>( Data[1] );
	}

	uint32 ReadBe32( const uint8* Data )
	{
		return ( static_cast<uint32>( Data[0] ) << 24 ) | ( static_cast<uint32>( Data[1] ) << 16 ) | ( static_cast<uint32>( Data[2] ) << 8 ) | static_cast<uint32>( Data[3] );
	}

	uint32 ReadLe16( const uint8* Data )
	{
		return static_cast<uint32>( Data[0] ) | ( static_cast<uint32>( Data[1] ) << 8 );
	}

	uint32 ReadLe32( const uint8* Data )
	{
		return static_cast<uint32>( Data[0] ) | ( static_cast<uint32>( Data[1] ) << 8 ) | ( static_cast<uint32>( Data[2] ) << 16 ) | ( static_cast<uint32>( Data[3] ) << 24 );
	}

	bool IsValidTextureSize( int64 Width, int64 Height )
	{
		return Width >= 1 && Width <= MaxTextureSize && Height >= 1 && Height <= MaxTextureSize && Width * Height <= MaxPixelCount;
	}

	// PNG のヘッダー（IHDR）から大きさを読む
	bool ReadPngSize( const uint8* Data, usize Size, int64& OutWidth, int64& OutHeight )
	{
		static const uint8 Signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
		if ( Size < 24 || FMemory::Memcmp( Data, Signature, 8 ) != 0 || FMemory::Memcmp( Data + 12, "IHDR", 4 ) != 0 )
		{
			return false;
		}
		OutWidth = ReadBe32( Data + 16 );
		OutHeight = ReadBe32( Data + 20 );
		return true;
	}

	// BMP のヘッダーから大きさを読む（高さの符号は、上下の向きなので捨てる）
	bool ReadBmpSize( const uint8* Data, usize Size, int64& OutWidth, int64& OutHeight )
	{
		if ( Size < 26 || Data[0] != 'B' || Data[1] != 'M' )
		{
			return false;
		}
		const uint32 HeaderSize = ReadLe32( Data + 14 );
		if ( HeaderSize == 12 )
		{
			OutWidth = ReadLe16( Data + 18 );
			OutHeight = ReadLe16( Data + 20 );
			return true;
		}
		if ( HeaderSize < 40 || Size < 26 )
		{
			return false;
		}
		const int32 SignedHeight = static_cast<int32>( ReadLe32( Data + 22 ) );
		OutWidth = static_cast<int32>( ReadLe32( Data + 18 ) );
		OutHeight = SignedHeight < 0 ? -static_cast<int64>( SignedHeight ) : static_cast<int64>( SignedHeight );
		return true;
	}

	// JPEG の SOF（フレームの始まり）から大きさを読む
	bool ReadJpegSize( const uint8* Data, usize Size, int64& OutWidth, int64& OutHeight )
	{
		if ( Size < 4 || Data[0] != 0xFF || Data[1] != 0xD8 )
		{
			return false;
		}
		usize Position = 2;
		while ( Position + 4 <= Size )
		{
			if ( Data[Position] != 0xFF )
			{
				++Position;
				continue;
			}
			const uint8 Marker = Data[Position + 1];
			if ( Marker == 0xFF )
			{
				// 詰め物
				++Position;
				continue;
			}
			if ( Marker == 0x00 || Marker == 0x01 || Marker == 0xD8 || ( Marker >= 0xD0 && Marker <= 0xD7 ) )
			{
				// 長さを持たない印
				Position += 2;
				continue;
			}
			const bool bFrameStart = Marker >= 0xC0 && Marker <= 0xCF && Marker != 0xC4 && Marker != 0xC8 && Marker != 0xCC;
			if ( bFrameStart )
			{
				if ( Position + 9 > Size )
				{
					return false;
				}
				OutHeight = ReadBe16( Data + Position + 5 );
				OutWidth = ReadBe16( Data + Position + 7 );
				return true;
			}
			const usize Length = ReadBe16( Data + Position + 2 );
			if ( Length < 2 )
			{
				return false;
			}
			Position += 2 + Length;
		}
		return false;
	}

	// PNG・BMP・JPEG のどれかのヘッダーから、大きさを読む。どれでもなければ false
	bool ReadImageSize( const uint8* Data, usize Size, int64& OutWidth, int64& OutHeight )
	{
		return ReadPngSize( Data, Size, OutWidth, OutHeight ) || ReadBmpSize( Data, Size, OutWidth, OutHeight ) || ReadJpegSize( Data, Size, OutWidth, OutHeight );
	}

	// WAV のヘッダーから、長さ（秒）を読む。読めなければ FallbackSoundLength
	float32 ReadSoundLength( const uint8* Data, usize Size )
	{
		if ( Size < 12 || FMemory::Memcmp( Data, "RIFF", 4 ) != 0 || FMemory::Memcmp( Data + 8, "WAVE", 4 ) != 0 )
		{
			return FallbackSoundLength;
		}
		uint32 ByteRate = 0;
		usize Position = 12;
		while ( Position + 8 <= Size )
		{
			const uint32 ChunkSize = ReadLe32( Data + Position + 4 );
			const uint8* Chunk = Data + Position + 8;
			if ( FMemory::Memcmp( Data + Position, "fmt ", 4 ) == 0 && Position + 8 + 12 <= Size )
			{
				ByteRate = ReadLe32( Chunk + 8 );
			}
			else if ( FMemory::Memcmp( Data + Position, "data", 4 ) == 0 )
			{
				if ( ByteRate == 0 )
				{
					return FallbackSoundLength;
				}
				return static_cast<float32>( ChunkSize ) / static_cast<float32>( ByteRate );
			}
			Position += 8 + static_cast<usize>( ChunkSize ) + ( ChunkSize & 1u );
		}
		return FallbackSoundLength;
	}

	// UTF-8 の 1 文字を読む。戻り値は、読んだバイト数（1 以上）。壊れていれば、U+FFFD を 1 文字として、読めたところまでを返す。Text は '\0' で終わる
	int32 DecodeUtf8( const char* Text, uint32& OutCodePoint )
	{
		const uint8 First = static_cast<uint8>( Text[0] );
		if ( First < 0x80 )
		{
			OutCodePoint = First;
			return 1;
		}
		int32 NumContinuation = 0;
		uint32 CodePoint = 0;
		if ( ( First & 0xE0 ) == 0xC0 )
		{
			NumContinuation = 1;
			CodePoint = First & 0x1Fu;
		}
		else if ( ( First & 0xF0 ) == 0xE0 )
		{
			NumContinuation = 2;
			CodePoint = First & 0x0Fu;
		}
		else if ( ( First & 0xF8 ) == 0xF0 )
		{
			NumContinuation = 3;
			CodePoint = First & 0x07u;
		}
		else
		{
			OutCodePoint = 0xFFFD;
			return 1;
		}
		for ( int32 Index = 1; Index <= NumContinuation; ++Index )
		{
			const uint8 Next = static_cast<uint8>( Text[Index] );
			if ( ( Next & 0xC0 ) != 0x80 )
			{
				OutCodePoint = 0xFFFD;
				return Index;
			}
			CodePoint = ( CodePoint << 6 ) | ( Next & 0x3Fu );
		}
		OutCodePoint = CodePoint;
		return NumContinuation + 1;
	}
}

bool CNullAssetBackend::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

void CNullAssetBackend::Deinitialize()
{
	LOG( LogNullAsset, Log, "Asset handles at shutdown: %d texture(s), %d vertex buffer(s), %d index buffer(s), %d shadow map(s), %d shader(s), %d font(s), %d sound(s)",
		 Textures.Num(), VertexBuffers.Num(), IndexBuffers.Num(), ShadowMaps.Num(), Shaders.Num(), Fonts.Num(), Sounds.Num() );
	if ( GetNumLiveHandles() > 0 )
	{
		LOG( LogNullAsset, Warning, "%d asset handle(s) were not released before shutdown", GetNumLiveHandles() );
	}
}

int32 CNullAssetBackend::AllocateHandle()
{
	return NextHandle++;
}

int32 CNullAssetBackend::AddTexture( const FTextureRecord& Record )
{
	const int32 Handle = AllocateHandle();
	Textures.Add( Handle, Record );
	return Handle;
}

#pragma region Textures

int32 CNullAssetBackend::LoadTexture( const FString& Path, bool bAsync )
{
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Path, Bytes ) || Bytes.Num() == 0 )
	{
		return NAsset::InvalidHandle;
	}
	int64 Width = FallbackTextureSize;
	int64 Height = FallbackTextureSize;
	ReadImageSize( Bytes.GetData(), static_cast<usize>( Bytes.Num() ), Width, Height );
	if ( !IsValidTextureSize( Width, Height ) )
	{
		return NAsset::InvalidHandle;
	}
	FTextureRecord Record;
	Record.Width = static_cast<int32>( Width );
	Record.Height = static_cast<int32>( Height );
	return AddTexture( Record );
}

int32 CNullAssetBackend::CreateTextureFromMemory( const void* FileImage, usize Size )
{
	int64 Width = 0;
	int64 Height = 0;
	if ( FileImage == nullptr || !ReadImageSize( static_cast<const uint8*>( FileImage ), Size, Width, Height ) || !IsValidTextureSize( Width, Height ) )
	{
		return NAsset::InvalidHandle;
	}
	FTextureRecord Record;
	Record.Width = static_cast<int32>( Width );
	Record.Height = static_cast<int32>( Height );
	return AddTexture( Record );
}

int32 CNullAssetBackend::CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	const FTextureRecord* Parent = Textures.Find( Source );
	if ( Parent == nullptr || X < 0 || Y < 0 || Width <= 0 || Height <= 0 )
	{
		return NAsset::InvalidHandle;
	}
	if ( static_cast<int64>( X ) + Width > Parent->Width || static_cast<int64>( Y ) + Height > Parent->Height )
	{
		return NAsset::InvalidHandle;
	}
	// 派生画像の派生画像は、いちばん元の画像からの位置にして覚える
	FTextureRecord Record;
	Record.Width = Width;
	Record.Height = Height;
	Record.bRegion = true;
	Record.Parent = Parent->bRegion ? Parent->Parent : Source;
	Record.RegionX = X + ( Parent->bRegion ? Parent->RegionX : 0 );
	Record.RegionY = Y + ( Parent->bRegion ? Parent->RegionY : 0 );
	return AddTexture( Record );
}

bool CNullAssetBackend::GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] )
{
	const FTextureRecord* Region = Textures.Find( Texture );
	if ( Region == nullptr || !Region->bRegion )
	{
		return false;
	}
	const FTextureRecord* Root = Textures.Find( Region->Parent );
	if ( Root == nullptr || Root->Width <= 0 || Root->Height <= 0 )
	{
		return false;
	}
	// 半 texel だけ内側
	OutUV[0] = ( static_cast<float32>( Region->RegionX ) + 0.5f ) / static_cast<float32>( Root->Width );
	OutUV[1] = ( static_cast<float32>( Region->RegionY ) + 0.5f ) / static_cast<float32>( Root->Height );
	OutUV[2] = ( static_cast<float32>( Region->RegionX + Region->Width ) - 0.5f ) / static_cast<float32>( Root->Width );
	OutUV[3] = ( static_cast<float32>( Region->RegionY + Region->Height ) - 0.5f ) / static_cast<float32>( Root->Height );
	return true;
}

bool CNullAssetBackend::GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight )
{
	const FTextureRecord* Record = Textures.Find( Texture );
	if ( Record == nullptr )
	{
		return false;
	}
	OutWidth = Record->Width;
	OutHeight = Record->Height;
	return true;
}

void CNullAssetBackend::DeleteTexture( int32 Texture )
{
	Textures.Remove( Texture );
}

int32 CNullAssetBackend::CreateRenderTarget( const FRenderTargetDesc& Desc )
{
	if ( !Desc.IsValid() )
	{
		return NAsset::InvalidHandle;
	}
	FTextureRecord Record;
	Record.Width = Desc.Width;
	Record.Height = Desc.Height;
	Record.bRenderTarget = true;
	Record.TargetDesc = Desc;
	return AddTexture( Record );
}

bool CNullAssetBackend::ReadRenderTargetPixels( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels )
{
	const FTextureRecord* Record = Textures.Find( Target );
	if ( Record == nullptr || !Record->bRenderTarget || X < 0 || Y < 0 || Width <= 0 || Height <= 0 )
	{
		return false;
	}
	if ( static_cast<int64>( X ) + Width > Record->Width || static_cast<int64>( Y ) + Height > Record->Height || static_cast<int64>( Width ) * Height > MaxPixelCount )
	{
		return false;
	}
	// 何も描かれていない状態（アルファを持たない描画先は、不透明な黒）
	OutPixels.Reset();
	OutPixels.SetNum( Width * Height, FColor32( 0, 0, 0, Record->TargetDesc.bAlpha ? 0 : 255 ) );
	return true;
}

bool CNullAssetBackend::SaveRenderTarget( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options )
{
	// 画像のファイルを作る機能を持たない
	return false;
}

int32 CNullAssetBackend::CreateTextureFromPixels( int32 Width, int32 Height, const FColor32* Pixels )
{
	if ( Pixels == nullptr || !IsValidTextureSize( Width, Height ) )
	{
		return NAsset::InvalidHandle;
	}
	FTextureRecord Record;
	Record.Width = Width;
	Record.Height = Height;
	Record.bHasPixels = true;
	const int32 NumPixels = Width * Height;
	Record.Pixels.SetNum( NumPixels );
	FMemory::Memcpy( Record.Pixels.GetData(), Pixels, static_cast<usize>( NumPixels ) * sizeof( FColor32 ) );
	return AddTexture( Record );
}

bool CNullAssetBackend::UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels )
{
	FTextureRecord* Record = Textures.Find( Texture );
	if ( Record == nullptr || !Record->bHasPixels || Pixels == nullptr || Record->Width != Width || Record->Height != Height )
	{
		return false;
	}
	FMemory::Memcpy( Record->Pixels.GetData(), Pixels, static_cast<usize>( Record->Pixels.Num() ) * sizeof( FColor32 ) );
	return true;
}

bool CNullAssetBackend::DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	int64 Width = 0;
	int64 Height = 0;
	if ( FileImage == nullptr || !ReadImageSize( static_cast<const uint8*>( FileImage ), Size, Width, Height ) || !IsValidTextureSize( Width, Height ) )
	{
		return false;
	}
	OutWidth = static_cast<int32>( Width );
	OutHeight = static_cast<int32>( Height );
	// 画素は読まず、全部を灰色にする
	OutPixels.Reset();
	OutPixels.SetNum( OutWidth * OutHeight, FColor32( 128, 128, 128, 255 ) );
	return true;
}

bool CNullAssetBackend::DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Path, Bytes ) )
	{
		return false;
	}
	return DecodeImage( Bytes.GetData(), static_cast<usize>( Bytes.Num() ), OutWidth, OutHeight, OutPixels );
}

bool CNullAssetBackend::SavePixels( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options )
{
	// 画像のファイルを作る機能を持たない
	return false;
}

#pragma endregion

#pragma region Models

int32 CNullAssetBackend::LoadModel( const FString& Path, bool bAsync )
{
	if ( !bWarnedModel )
	{
		bWarnedModel = true;
		LOG( LogNullAsset, Warning, "The null backend cannot load models (first request: %s). Models are skipped.", *Path );
	}
	return NAsset::InvalidHandle;
}

int32 CNullAssetBackend::DuplicateModel( int32 Source )
{
	return NAsset::InvalidHandle;
}

void CNullAssetBackend::DeleteModel( int32 Model )
{
}

int32 CNullAssetBackend::GetModelAnimCount( int32 Model )
{
	return 0;
}

FString CNullAssetBackend::GetModelAnimName( int32 Model, int32 AnimIndex )
{
	return FString();
}

float32 CNullAssetBackend::GetModelAnimLength( int32 Model, int32 AnimIndex )
{
	return 0.0f;
}

int32 CNullAssetBackend::AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel )
{
	return -1;
}

void CNullAssetBackend::DetachModelAnim( int32 Model, int32 AttachIndex )
{
}

int32 CNullAssetBackend::GetModelMeshCount( int32 Model )
{
	return 0;
}

int32 CNullAssetBackend::GetModelFrameCount( int32 Model )
{
	return 0;
}

FString CNullAssetBackend::GetModelFrameName( int32 Model, int32 FrameIndex )
{
	return FString();
}

bool CNullAssetBackend::SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning )
{
	return false;
}

#pragma endregion

#pragma region Buffers

bool CNullAssetBackend::CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer )
{
	if ( Vertices == nullptr || Indices == nullptr || NumVertices <= 0 || NumIndices <= 0 )
	{
		return false;
	}
	OutVertexBuffer = AllocateHandle();
	VertexBuffers.Add( OutVertexBuffer, NumVertices );
	OutIndexBuffer = AllocateHandle();
	FIndexBufferRecord Record;
	Record.NumIndices = NumIndices;
	Record.b32Bit = true;
	IndexBuffers.Add( OutIndexBuffer, Record );
	return true;
}

void CNullAssetBackend::DeleteVertexBuffer( int32 VertexBuffer )
{
	VertexBuffers.Remove( VertexBuffer );
}

void CNullAssetBackend::DeleteIndexBuffer( int32 IndexBuffer )
{
	IndexBuffers.Remove( IndexBuffer );
}

int32 CNullAssetBackend::MakeVertexBuffer( int32 NumVertices )
{
	if ( NumVertices <= 0 )
	{
		return NAsset::InvalidHandle;
	}
	const int32 Handle = AllocateHandle();
	VertexBuffers.Add( Handle, NumVertices );
	return Handle;
}

bool CNullAssetBackend::WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices )
{
	const int32* Size = VertexBuffers.Find( VertexBuffer );
	if ( Size == nullptr || Vertices == nullptr || StartIndex < 0 || NumVertices <= 0 )
	{
		return false;
	}
	return static_cast<int64>( StartIndex ) + NumVertices <= *Size;
}

int32 CNullAssetBackend::MakeIndexBuffer( int32 NumIndices, bool b32Bit )
{
	if ( NumIndices <= 0 )
	{
		return NAsset::InvalidHandle;
	}
	const int32 Handle = AllocateHandle();
	FIndexBufferRecord Record;
	Record.NumIndices = NumIndices;
	Record.b32Bit = b32Bit;
	IndexBuffers.Add( Handle, Record );
	return Handle;
}

bool CNullAssetBackend::WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices )
{
	const FIndexBufferRecord* Record = IndexBuffers.Find( IndexBuffer );
	if ( Record == nullptr || Indices == nullptr || StartIndex < 0 || NumIndices <= 0 )
	{
		return false;
	}
	return static_cast<int64>( StartIndex ) + NumIndices <= Record->NumIndices;
}

int32 CNullAssetBackend::MakeShadowMap( int32 Width, int32 Height )
{
	if ( Width < 1 || Width > NDrawShadowMap::MaxSize || Height < 1 || Height > NDrawShadowMap::MaxSize )
	{
		return NAsset::InvalidHandle;
	}
	const int32 Handle = AllocateHandle();
	FShadowMapRecord Record;
	Record.Width = Width;
	Record.Height = Height;
	ShadowMaps.Add( Handle, Record );
	return Handle;
}

void CNullAssetBackend::DeleteShadowMap( int32 ShadowMap )
{
	ShadowMaps.Remove( ShadowMap );
}

int32 CNullAssetBackend::MakeShader( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	if ( Stage != EShaderStage::Vertex && Stage != EShaderStage::Pixel )
	{
		return NAsset::InvalidHandle;
	}
	// コンパイル済みのシェーダーは、DXBC の印で始まる
	if ( Code == nullptr || NumBytes < 4 || FMemory::Memcmp( Code, "DXBC", 4 ) != 0 )
	{
		return NAsset::InvalidHandle;
	}
	const int32 Handle = AllocateHandle();
	Shaders.Add( Handle, Stage );
	return Handle;
}

void CNullAssetBackend::DeleteShader( int32 Shader )
{
	Shaders.Remove( Shader );
}

bool CNullAssetBackend::CompileShader( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage )
{
	OutCode.Reset();
	OutMessage.Empty();
	if ( Stage != EShaderStage::Vertex && Stage != EShaderStage::Pixel )
	{
		OutMessage = "Invalid shader stage.";
		return false;
	}
	if ( Source == nullptr || NumBytes < 1 )
	{
		OutMessage = "Shader source is empty.";
		return false;
	}
	if ( EntryPoint == nullptr || EntryPoint[0] == '\0' )
	{
		OutMessage = "Entry point is empty.";
		return false;
	}
	if ( ShaderModel == nullptr || ( NCString::Compare( ShaderModel, "4_0" ) != 0 && NCString::Compare( ShaderModel, "4_1" ) != 0 && NCString::Compare( ShaderModel, "5_0" ) != 0 ) )
	{
		OutMessage = "Unsupported shader model.";
		return false;
	}
	// MakeShader が受け取れるように、DXBC の 4 文字だけを返す
	OutCode.Add( static_cast<uint8>( 'D' ) );
	OutCode.Add( static_cast<uint8>( 'X' ) );
	OutCode.Add( static_cast<uint8>( 'B' ) );
	OutCode.Add( static_cast<uint8>( 'C' ) );
	return true;
}

#pragma endregion

#pragma region Fonts

int32 CNullAssetBackend::GetAdvance( const FFontRecord& Font, uint32 CodePoint ) const
{
	if ( const int32* Custom = Font.CharAdvances.Find( CodePoint ) )
	{
		return *Custom;
	}
	// 半角（ASCII と半角カナ）は大きさの半分、それ以外は大きさと同じ
	const bool bHalfWidth = CodePoint < 0x80 || ( CodePoint >= 0xFF61 && CodePoint <= 0xFF9F );
	const int32 Base = bHalfWidth ? Font.Desc.Size / 2 : Font.Desc.Size;
	return NMath::Max( Base + Font.Desc.LetterSpacing + 2 * Font.Desc.EdgeSize, 0 );
}

void CNullAssetBackend::Layout( const FFontRecord& Font, const char* Text, FTextSize& OutSize, TArray<FTextGlyph>* OutGlyphs ) const
{
	OutSize = FTextSize();
	if ( OutGlyphs != nullptr )
	{
		OutGlyphs->Reset();
	}
	if ( Text == nullptr || Text[0] == '\0' )
	{
		return;
	}

	int32 Line = 0;
	int32 X = 0;
	int32 LongestWidth = 0;
	int32 Offset = 0;
	OutSize.NumLines = 1;
	while ( Text[Offset] != '\0' )
	{
		uint32 CodePoint = 0;
		const int32 NumBytes = DecodeUtf8( Text + Offset, CodePoint );
		FTextGlyph Glyph;
		Glyph.Offset = Offset;
		Glyph.Bytes = NumBytes;
		Glyph.X = X;
		Glyph.Y = Line * Font.Metrics.LineHeight;
		if ( CodePoint == '\n' )
		{
			LongestWidth = NMath::Max( LongestWidth, X );
			Glyph.Width = 0;
			++Line;
			X = 0;
			++OutSize.NumLines;
		}
		else
		{
			Glyph.Width = GetAdvance( Font, CodePoint );
			X += Glyph.Width;
		}
		if ( OutGlyphs != nullptr )
		{
			OutGlyphs->Add( Glyph );
		}
		Offset += NumBytes;
	}
	LongestWidth = NMath::Max( LongestWidth, X );
	OutSize.Width = LongestWidth;
	OutSize.Height = ( OutSize.NumLines - 1 ) * Font.Metrics.LineHeight + Font.Metrics.GlyphHeight;
}

int32 CNullAssetBackend::MakeFont( const FFontDesc& Desc )
{
	if ( Desc.Size < 1 || Desc.Size > MaxFontSize || Desc.EdgeSize < 0 || Desc.EdgeSize > MaxFontSize )
	{
		return NAsset::InvalidHandle;
	}
	FFontRecord Record;
	Record.Desc = Desc;
	Record.Metrics.Size = Desc.Size;
	Record.Metrics.EdgeSize = Desc.EdgeSize;
	Record.Metrics.LetterSpacing = Desc.LetterSpacing;
	Record.Metrics.GlyphHeight = Desc.Size + 2 * Desc.EdgeSize;
	Record.Metrics.LineHeight = Desc.LineHeight >= 0 ? Desc.LineHeight : Record.Metrics.GlyphHeight;
	Record.Metrics.Ascent = Desc.Size * 4 / 5 + Desc.EdgeSize;
	Record.Metrics.MaxWidth = NMath::Max( Desc.Size + Desc.LetterSpacing + 2 * Desc.EdgeSize, 0 );
	const int32 Handle = AllocateHandle();
	Fonts.Add( Handle, Record );
	return Handle;
}

void CNullAssetBackend::DeleteFont( int32 Font )
{
	Fonts.Remove( Font );
}

int32 CNullAssetBackend::MeasureText( int32 Font, const char* Text )
{
	FTextSize Size;
	return MeasureTextSize( Font, Text, Size ) ? Size.Width : 0;
}

bool CNullAssetBackend::GetFontMetrics( int32 Font, FFontMetrics& OutMetrics )
{
	const FFontRecord* Record = Fonts.Find( Font );
	if ( Record == nullptr )
	{
		return false;
	}
	OutMetrics = Record->Metrics;
	return true;
}

bool CNullAssetBackend::MeasureTextSize( int32 Font, const char* Text, FTextSize& OutSize )
{
	const FFontRecord* Record = Fonts.Find( Font );
	if ( Record == nullptr )
	{
		return false;
	}
	Layout( *Record, Text, OutSize, nullptr );
	return true;
}

bool CNullAssetBackend::GetTextGlyphs( int32 Font, const char* Text, TArray<FTextGlyph>& OutGlyphs )
{
	const FFontRecord* Record = Fonts.Find( Font );
	if ( Record == nullptr )
	{
		return false;
	}
	FTextSize Size;
	Layout( *Record, Text, Size, &OutGlyphs );
	return true;
}

bool CNullAssetBackend::IsFontAvailable( const FString& Face )
{
	return Face == NullFontName;
}

void CNullAssetBackend::GetFontNames( TArray<FString>& OutNames )
{
	OutNames.Reset();
	OutNames.Add( FString( NullFontName ) );
}

bool CNullAssetBackend::AddFontCharImage( int32 Font, const char* Character, int32 Image, int32 DrawX, int32 DrawY, int32 Advance )
{
	FFontRecord* Record = Fonts.Find( Font );
	if ( Record == nullptr || Character == nullptr || Character[0] == '\0' || !Textures.Contains( Image ) || Advance < 0 )
	{
		return false;
	}
	// 1 文字だけを受け付ける
	uint32 CodePoint = 0;
	const int32 NumBytes = DecodeUtf8( Character, CodePoint );
	if ( Character[NumBytes] != '\0' )
	{
		return false;
	}
	Record->CharAdvances.Add( CodePoint, Advance );
	return true;
}

void CNullAssetBackend::RemoveFontCharImage( int32 Font, const char* Character )
{
	FFontRecord* Record = Fonts.Find( Font );
	if ( Record == nullptr || Character == nullptr || Character[0] == '\0' )
	{
		return;
	}
	uint32 CodePoint = 0;
	DecodeUtf8( Character, CodePoint );
	Record->CharAdvances.Remove( CodePoint );
}

bool CNullAssetBackend::AddFontFile( const FString& Path )
{
	if ( !NFileSystem::FileExists( Path ) )
	{
		return false;
	}
	FontFiles.AddUnique( Path );
	return true;
}

void CNullAssetBackend::RemoveFontFile( const FString& Path )
{
	FontFiles.Remove( Path );
}

#pragma endregion

#pragma region Sounds

int32 CNullAssetBackend::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	TArray<uint8> Bytes;
	if ( !NFile::ReadAllBytes( Path, Bytes ) || Bytes.Num() == 0 )
	{
		return NAsset::InvalidHandle;
	}
	FSoundRecord Record;
	Record.Length = ReadSoundLength( Bytes.GetData(), static_cast<usize>( Bytes.Num() ) );
	Record.Mode = Mode;
	const int32 Handle = AllocateHandle();
	Sounds.Add( Handle, Record );
	return Handle;
}

int32 CNullAssetBackend::DuplicateSound( int32 Source )
{
	const FSoundRecord* Record = Sounds.Find( Source );
	// 複製できるのは、Memory で読み込んだものだけ
	if ( Record == nullptr || Record->Mode != ESoundLoadMode::Memory )
	{
		return NAsset::InvalidHandle;
	}
	const FSoundRecord Copy = *Record;
	const int32 Handle = AllocateHandle();
	Sounds.Add( Handle, Copy );
	return Handle;
}

void CNullAssetBackend::DeleteSound( int32 Sound )
{
	Sounds.Remove( Sound );
}

float32 CNullAssetBackend::GetSoundLength( int32 Sound )
{
	const FSoundRecord* Record = Sounds.Find( Sound );
	return Record != nullptr ? Record->Length : -1.0f;
}

bool CNullAssetBackend::GetSoundInfo( int32 Sound, FSoundInfo& Out )
{
	const FSoundRecord* Record = Sounds.Find( Sound );
	if ( Record == nullptr )
	{
		return false;
	}
	if ( Record->bHasInfo )
	{
		Out = Record->Info;
		return true;
	}
	// 分かるのは長さだけ
	FSoundInfo Info;
	Info.LengthSeconds = Record->Length;
	Out = Info;
	return true;
}

int32 CNullAssetBackend::CreateSoundFromPcm( const FPcmBuffer& Pcm )
{
	if ( !Pcm.IsValid() )
	{
		return NAsset::InvalidHandle;
	}
	FSoundRecord Record;
	Record.Length = Pcm.GetLengthSeconds();
	Record.Mode = ESoundLoadMode::Memory;
	Record.Info.LengthSeconds = Pcm.GetLengthSeconds();
	Record.Info.FrameCount = Pcm.GetFrameCount();
	Record.Info.SampleRate = Pcm.SampleRate;
	Record.Info.Channels = Pcm.Channels;
	Record.bHasInfo = true;
	const int32 Handle = AllocateHandle();
	Sounds.Add( Handle, Record );
	return Handle;
}

#pragma endregion

#pragma region Memory

FBackendMemoryInfo CNullAssetBackend::GetMemoryInfo()
{
	FBackendMemoryInfo Info;
	int64 Used = 0;
	for ( const auto& Pair : Textures )
	{
		const FTextureRecord& Record = Pair.Value;
		if ( Record.bRegion )
		{
			continue;
		}
		const int64 BytesPerPixel = Record.bRenderTarget ? Record.TargetDesc.GetBytesPerPixel() : 4;
		Used += static_cast<int64>( Record.Width ) * Record.Height * BytesPerPixel;
	}
	for ( const auto& Pair : VertexBuffers )
	{
		Used += static_cast<int64>( Pair.Value ) * static_cast<int64>( sizeof( FDrawVertex3D ) );
	}
	for ( const auto& Pair : IndexBuffers )
	{
		Used += static_cast<int64>( Pair.Value.NumIndices ) * ( Pair.Value.b32Bit ? 4 : 2 );
	}
	for ( const auto& Pair : ShadowMaps )
	{
		Used += static_cast<int64>( Pair.Value.Width ) * Pair.Value.Height * 4;
	}
	Info.VideoMemoryUsed = Used;
	Info.NumTextureHandles = Textures.Num();
	Info.NumSoundHandles = Sounds.Num();
	Info.NumFontHandles = Fonts.Num();
	return Info;
}

int64 CNullAssetBackend::EstimateModelBytes( int32 Model )
{
	return 0;
}

int64 CNullAssetBackend::EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode )
{
	const FSoundRecord* Record = Sounds.Find( Sound );
	if ( Record == nullptr )
	{
		return 0;
	}
	// Memory は、44.1 kHz・16 ビット・2 チャンネルで展開した大きさ。Stream は、読み込み用の領域
	return Mode == ESoundLoadMode::Memory ? static_cast<int64>( Record->Length * 44100.0f * 4.0f ) : 65536;
}

bool CNullAssetBackend::IsLoading( int32 Handle )
{
	return false;
}

int32 CNullAssetBackend::GetNumLoading()
{
	return 0;
}

#pragma endregion

#pragma region Counts

int32 CNullAssetBackend::GetNumTextures() const
{
	return Textures.Num();
}

int32 CNullAssetBackend::GetNumVertexBuffers() const
{
	return VertexBuffers.Num();
}

int32 CNullAssetBackend::GetNumIndexBuffers() const
{
	return IndexBuffers.Num();
}

int32 CNullAssetBackend::GetNumShadowMaps() const
{
	return ShadowMaps.Num();
}

int32 CNullAssetBackend::GetNumShaders() const
{
	return Shaders.Num();
}

int32 CNullAssetBackend::GetNumFonts() const
{
	return Fonts.Num();
}

int32 CNullAssetBackend::GetNumSounds() const
{
	return Sounds.Num();
}

int32 CNullAssetBackend::GetNumLiveHandles() const
{
	return Textures.Num() + VertexBuffers.Num() + IndexBuffers.Num() + ShadowMaps.Num() + Shaders.Num() + Fonts.Num() + Sounds.Num();
}

#pragma endregion
