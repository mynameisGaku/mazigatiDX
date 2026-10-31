#include "DxLibAssetBackend.h"


#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibVertex.h"

#include <psapi.h>   // windows.h（DxLib.h が読み込む）より後に
#include <d3dcompiler.h>

#include <climits>
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC( LogAssetBackend, Log )

REGISTER_SUBSYSTEM( CDxLibAssetBackend )

namespace
{
	// d3dcompiler_47.dll の D3DCompile。Windows に入っている DLL を、使うときに読む（リンクしないので、無い環境でもこのファイルは動き、コンパイルだけが失敗する）
	using FD3DCompileFunction = HRESULT( WINAPI* )( LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob** );

	FD3DCompileFunction FindD3DCompile()
	{
		static FD3DCompileFunction Function = nullptr;
		static bool bSearched = false;
		if ( !bSearched )
		{
			bSearched = true;
			HMODULE Module = LoadLibraryW( L"d3dcompiler_47.dll" );
			if ( Module != nullptr )
			{
				Function = reinterpret_cast<FD3DCompileFunction>( GetProcAddress( Module, "D3DCompile" ) );
			}
		}
		return Function;
	}

	// 非同期読み込みの設定を、この範囲だけ変える
	struct FAsyncScope
	{
		explicit FAsyncScope( bool bAsync ) { SetUseASyncLoadFlag( bAsync ? TRUE : FALSE ); }
		~FAsyncScope() { SetUseASyncLoadFlag( FALSE ); }
	};
}

void CDxLibAssetBackend::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogAssetBackend, Error, "No platform is available. Assets cannot be loaded." );
	}
}

//-----------------------------------------------------------------------------
// 画像
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadTexture( const FString& Path, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	return LoadGraph( *Path );
}

int32 CDxLibAssetBackend::CreateTextureFromMemory( const void* FileImage, usize Size )
{
	return CreateGraphFromMem( FileImage, static_cast<int>( Size ) );
}

int32 CDxLibAssetBackend::CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	if ( Source < 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 ) return NAsset::InvalidHandle;
	// 元グラフ上の指定矩形を収める寸法。
	int SourceWidth = 0;
	int SourceHeight = 0;
	if ( GetGraphSize( Source, &SourceWidth, &SourceHeight ) != 0 ) return NAsset::InvalidHandle;
	if ( static_cast<int64>( X ) + Width > SourceWidth || static_cast<int64>( Y ) + Height > SourceHeight ) return NAsset::InvalidHandle;
	return DerivationGraph( X, Y, Width, Height, Source );
}

bool CDxLibAssetBackend::GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] )
{
	if ( Texture < 0 ) return false;
	// 派生前のbase texture全体のpixel寸法。
	int TextureWidth = 0, TextureHeight = 0;
	// 派生グラフがbase texture上で占めるpixel矩形。
	int X = 0, Y = 0, Width = 0, Height = 0;
	if ( GetGraphTextureSize( Texture, &TextureWidth, &TextureHeight ) != 0 || TextureWidth <= 0 || TextureHeight <= 0 ) return false;
	if ( GetGraphUseBaseGraphArea( Texture, &X, &Y, &Width, &Height ) != 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 || static_cast<int64>( X ) + Width > TextureWidth || static_cast<int64>( Y ) + Height > TextureHeight ) return false;
	// 一pixel領域でも一点を正確に標本化できるよう、両端を中央へ寄せる。
	// 半pixelを正規化した横・縦のUV幅。
	const float32 HalfU = 0.5f / static_cast<float32>( TextureWidth );
	const float32 HalfV = 0.5f / static_cast<float32>( TextureHeight );
	OutUV[0] = ( static_cast<float32>( X ) + HalfU * TextureWidth ) / static_cast<float32>( TextureWidth );
	OutUV[1] = ( static_cast<float32>( Y ) + HalfV * TextureHeight ) / static_cast<float32>( TextureHeight );
	OutUV[2] = ( static_cast<float32>( X + Width ) - HalfU * TextureWidth ) / static_cast<float32>( TextureWidth );
	OutUV[3] = ( static_cast<float32>( Y + Height ) - HalfV * TextureHeight ) / static_cast<float32>( TextureHeight );
	return NMath::IsFinite( OutUV[0] ) && NMath::IsFinite( OutUV[1] ) && NMath::IsFinite( OutUV[2] ) && NMath::IsFinite( OutUV[3] );
}

bool CDxLibAssetBackend::GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight )
{
	int Width = 0;
	int Height = 0;
	if ( GetGraphSize( Texture, &Width, &Height ) != 0 ) return false;
	OutWidth = Width;
	OutHeight = Height;
	return true;
}

void CDxLibAssetBackend::DeleteTexture( int32 Texture )
{
	DeleteGraph( Texture );
}

int32 CDxLibAssetBackend::CreateRenderTarget( const FRenderTargetDesc& Desc )
{
	if ( !Desc.IsValid() )
	{
		return NAsset::InvalidHandle;
	}

	// 作るときの設定は DxLib の大域の状態なので、作る前の値を覚えて、作ったあとに戻す（マルチサンプルは取得関数がないので、既定の 0 に戻す）
	const int PreviousDepth = GetDrawValidGraphCreateZBufferFlag();
	SetDrawValidGraphCreateZBufferFlag( Desc.bDepth ? TRUE : FALSE );
	if ( Desc.MultiSamples > 0 )
	{
		SetCreateDrawValidGraphMultiSample( Desc.MultiSamples, 0 );
	}
	const int Handle = MakeScreen( Desc.Width, Desc.Height, Desc.bAlpha ? TRUE : FALSE );
	if ( Desc.MultiSamples > 0 )
	{
		SetCreateDrawValidGraphMultiSample( 0, 0 );
	}
	SetDrawValidGraphCreateZBufferFlag( PreviousDepth );
	return Handle >= 0 ? Handle : NAsset::InvalidHandle;
}

namespace
{
	// 描画先の矩形が、描画先の中に収まっているか
	bool IsInsideTarget( int32 Target, int32 X, int32 Y, int32 Width, int32 Height )
	{
		int TargetWidth = 0;
		int TargetHeight = 0;
		if ( Target < 0 || Width <= 0 || Height <= 0 || X < 0 || Y < 0 || GetGraphSize( Target, &TargetWidth, &TargetHeight ) != 0 )
		{
			return false;
		}
		return static_cast<int64>( X ) + Width <= TargetWidth && static_cast<int64>( Y ) + Height <= TargetHeight;
	}

	// 描画先を一時的に切り替える。使い終わると、前の描画先・描画範囲・「描画先を替えたときに設定をリセットするか」へ戻す
	// （描画先を替えると、描画範囲が描画先の全体になるので、読んだり保存したりするあいだだけ、全体にする）
	class FTemporaryTarget
	{
	public:
		explicit FTemporaryTarget( int32 Target )
			: PreviousScreen( GetDrawScreen() )
			, PreviousReset( GetUseSetDrawScreenSettingReset() )
		{
			GetDrawArea( &PreviousArea );
			SetUseSetDrawScreenSettingReset( FALSE );
			bSwitched = SetDrawScreen( Target ) == 0;
			if ( bSwitched )
			{
				SetDrawAreaFull();
			}
		}

		~FTemporaryTarget()
		{
			if ( bSwitched )
			{
				SetDrawScreen( PreviousScreen );
				SetDrawArea( PreviousArea.left, PreviousArea.top, PreviousArea.right, PreviousArea.bottom );
			}
			SetUseSetDrawScreenSettingReset( PreviousReset );
		}

		bool IsSwitched() const { return bSwitched; }

		FTemporaryTarget( const FTemporaryTarget& ) = delete;
		FTemporaryTarget& operator=( const FTemporaryTarget& ) = delete;

	private:
		int PreviousScreen;
		int PreviousReset;
		RECT PreviousArea = {};
		bool bSwitched = false;
	};
}

bool CDxLibAssetBackend::ReadRenderTargetPixels( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels )
{
	if ( !IsInsideTarget( Target, X, Y, Width, Height ) )
	{
		return false;
	}
	FTemporaryTarget Scope( Target );
	if ( !Scope.IsSwitched() )
	{
		return false;
	}
	const int Soft = MakeARGB8ColorSoftImage( Width, Height );
	if ( Soft < 0 )
	{
		return false;
	}
	const bool bRead = GetDrawScreenSoftImage( X, Y, X + Width, Y + Height, Soft ) == 0;
	if ( bRead )
	{
		OutPixels.SetNum( Width * Height );
		for ( int32 Row = 0; Row < Height; ++Row )
		{
			for ( int32 Column = 0; Column < Width; ++Column )
			{
				int Red = 0;
				int Green = 0;
				int Blue = 0;
				int Alpha = 0;
				GetPixelSoftImage( Soft, Column, Row, &Red, &Green, &Blue, &Alpha );
				OutPixels[Row * Width + Column] = FColor32( static_cast<uint8>( Red ), static_cast<uint8>( Green ), static_cast<uint8>( Blue ), static_cast<uint8>( Alpha ) );
			}
		}
	}
	DeleteSoftImage( Soft );
	return bRead;
}

bool CDxLibAssetBackend::SaveRenderTarget( int32 Target, int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options )
{
	if ( Path.IsEmpty() || !IsInsideTarget( Target, X, Y, Width, Height ) )
	{
		return false;
	}
	// DxLib の SaveDrawValidGraph は、描画先にできない画像（MakeGraph など）でも成功を返してしまうので、描画先にできるかを、先に確かめる
	FTemporaryTarget Scope( Target );
	if ( !Scope.IsSwitched() )
	{
		return false;
	}
	int SaveType = DX_IMAGESAVETYPE_PNG;
	switch ( Options.Format )
	{
	case EImageFileFormat::Bmp:
		SaveType = DX_IMAGESAVETYPE_BMP;
		break;
	case EImageFileFormat::Jpeg:
		SaveType = DX_IMAGESAVETYPE_JPEG;
		break;
	default:
		break;
	}
	return SaveDrawValidGraph( Target, X, Y, X + Width, Y + Height, *Path, SaveType, NMath::Clamp( Options.JpegQuality, 0, 100 ), TRUE, NMath::Clamp( Options.PngCompression, -1, 9 ) ) == 0;
}

namespace
{
	// 画素から作れる大きさか（呼ぶ側の確認が抜けても、DxLib に渡さない）
	bool IsValidPixelSize( int32 Width, int32 Height, const void* Pixels )
	{
		return Pixels != nullptr && Width >= 1 && Width <= FRenderTargetDesc::MaxSize && Height >= 1 && Height <= FRenderTargetDesc::MaxSize;
	}

	// 画素を、DxLib の ARGB8 の画像（BASEIMAGE）へ詰める。成功したら、使い終わりに ReleaseBaseImage を呼ぶこと
	bool FillBaseImage( BASEIMAGE& Base, int32 Width, int32 Height, const FColor32* Pixels )
	{
		if ( CreateARGB8ColorBaseImage( Width, Height, &Base ) != 0 )
		{
			return false;
		}
		for ( int32 Row = 0; Row < Height; ++Row )
		{
			uint32* Line = reinterpret_cast<uint32*>( static_cast<uint8*>( Base.GraphData ) + static_cast<usize>( Base.Pitch ) * static_cast<usize>( Row ) );
			for ( int32 Column = 0; Column < Width; ++Column )
			{
				const FColor32& Pixel = Pixels[Row * Width + Column];
				Line[Column] = ( static_cast<uint32>( Pixel.a ) << 24 ) | ( static_cast<uint32>( Pixel.r ) << 16 ) | ( static_cast<uint32>( Pixel.g ) << 8 ) | static_cast<uint32>( Pixel.b );
			}
		}
		return true;
	}

	// 4 バイトの、ビッグエンディアン / リトルエンディアンの値
	uint32 ReadBigEndian32( const uint8* Bytes )
	{
		return ( static_cast<uint32>( Bytes[0] ) << 24 ) | ( static_cast<uint32>( Bytes[1] ) << 16 ) | ( static_cast<uint32>( Bytes[2] ) << 8 ) | static_cast<uint32>( Bytes[3] );
	}

	uint32 ReadLittleEndian32( const uint8* Bytes )
	{
		return ( static_cast<uint32>( Bytes[3] ) << 24 ) | ( static_cast<uint32>( Bytes[2] ) << 16 ) | ( static_cast<uint32>( Bytes[1] ) << 8 ) | static_cast<uint32>( Bytes[0] );
	}

	// 画像のファイルの中身が、最後まであるように見えるか。PNG（印のあと、IHDR から IEND まで、チャンクが全部ある）・BMP（画素のデータが全部ある）・JPEG（終わりの印がある）を調べる。
	// 調べられない形式は、true（DxLib に任せる）。DxLib は、切れたデータで落ちるので、渡す前の確認に使う
	bool ImageDataLooksComplete( const uint8* Data, usize Size )
	{
		static const uint8 PngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
		if ( Data == nullptr || Size == 0 )
		{
			return false;
		}
		if ( Size >= 8 && std::memcmp( Data, PngSignature, 8 ) == 0 )
		{
			// 最初のチャンクは、長さ 13 の IHDR。以降のチャンクを、IEND まで、全部読める
			usize Position = 8;
			bool bFirst = true;
			while ( Position + 12 <= Size )
			{
				const uint32 Length = ReadBigEndian32( Data + Position );
				const uint8* Type = Data + Position + 4;
				if ( static_cast<uint64>( Position ) + 12 + Length > Size )
				{
					return false;
				}
				const bool bIsHeader = std::memcmp( Type, "IHDR", 4 ) == 0;
				if ( bFirst && ( !bIsHeader || Length != 13 ) )
				{
					return false;
				}
				bFirst = false;
				Position += 12 + static_cast<usize>( Length );
				if ( std::memcmp( Type, "IEND", 4 ) == 0 )
				{
					return true;
				}
			}
			return false;
		}
		if ( Size >= 2 && Data[0] == 'B' && Data[1] == 'M' )
		{
			// ファイルの頭 14 バイトと、情報の頭（大きさ・幅・高さ・ビット数・圧縮）が読めて、画素のデータが全部ある（圧縮なしのとき）
			if ( Size < 26 )
			{
				return false;
			}
			const uint32 DataOffset = ReadLittleEndian32( Data + 10 );
			if ( DataOffset >= Size )
			{
				return false;
			}
			const uint32 HeaderSize = ReadLittleEndian32( Data + 14 );
			if ( HeaderSize >= 40 && Size >= 14 + 40 )
			{
				const int32 Width = static_cast<int32>( ReadLittleEndian32( Data + 18 ) );
				const int32 Height = static_cast<int32>( ReadLittleEndian32( Data + 22 ) );
				const uint32 Bits = static_cast<uint32>( Data[28] ) | ( static_cast<uint32>( Data[29] ) << 8 );
				const uint32 Compression = ReadLittleEndian32( Data + 30 );
				if ( Width <= 0 || Height == 0 || Height == INT_MIN || Bits == 0 || Bits > 64 )
				{
					return false;
				}
				if ( Compression == 0 )
				{
					const uint64 RowBytes = ( ( static_cast<uint64>( Width ) * Bits + 31 ) / 32 ) * 4;
					const uint64 AbsHeight = static_cast<uint64>( Height < 0 ? -static_cast<int64>( Height ) : Height );
					if ( static_cast<uint64>( DataOffset ) + RowBytes * AbsHeight > Size )
					{
						return false;
					}
				}
			}
			return true;
		}
		if ( Size >= 2 && Data[0] == 0xFF && Data[1] == 0xD8 )
		{
			// 終わりの印（FF D9）が、末尾（0 の詰め物を除いて）にある
			usize End = Size;
			while ( End > 2 && Data[End - 1] == 0 )
			{
				--End;
			}
			return End >= 4 && Data[End - 2] == 0xFF && Data[End - 1] == 0xD9;
		}
		return true;
	}

	// ファイルの中身を全部読んで、ImageDataLooksComplete で確かめる。読めなければ false
	bool FileLooksComplete( const FString& Path )
	{
		const HANDLE File = CreateFileW( Path.ToWide().GetData(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( File == INVALID_HANDLE_VALUE )
		{
			return false;
		}
		LARGE_INTEGER FileSize = {};
		bool bComplete = false;
		if ( GetFileSizeEx( File, &FileSize ) && FileSize.QuadPart > 0 && FileSize.QuadPart <= static_cast<LONGLONG>( INT_MAX ) )
		{
			TArray<uint8> Bytes;
			Bytes.SetNum( static_cast<int32>( FileSize.QuadPart ) );
			DWORD NumRead = 0;
			if ( ReadFile( File, Bytes.GetData(), static_cast<DWORD>( FileSize.QuadPart ), &NumRead, nullptr ) && NumRead == static_cast<DWORD>( FileSize.QuadPart ) )
			{
				bComplete = ImageDataLooksComplete( Bytes.GetData(), static_cast<usize>( NumRead ) );
			}
		}
		CloseHandle( File );
		return bComplete;
	}

	// ソフトウェアイメージ（ARGB8）の全画素を読む。呼ぶ側が、ソフトウェアイメージを消す
	bool ReadSoftImage( int Soft, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
	{
		int Width = 0;
		int Height = 0;
		if ( Soft < 0 || GetSoftImageSize( Soft, &Width, &Height ) != 0 || Width < 1 || Height < 1 || Width > FRenderTargetDesc::MaxSize || Height > FRenderTargetDesc::MaxSize )
		{
			return false;
		}
		OutPixels.SetNum( Width * Height );
		for ( int Row = 0; Row < Height; ++Row )
		{
			for ( int Column = 0; Column < Width; ++Column )
			{
				int Red = 0;
				int Green = 0;
				int Blue = 0;
				int Alpha = 0;
				GetPixelSoftImage_Unsafe_ARGB8( Soft, Column, Row, &Red, &Green, &Blue, &Alpha );
				OutPixels[Row * Width + Column] = FColor32( static_cast<uint8>( Red ), static_cast<uint8>( Green ), static_cast<uint8>( Blue ), static_cast<uint8>( Alpha ) );
			}
		}
		OutWidth = Width;
		OutHeight = Height;
		return true;
	}
}

int32 CDxLibAssetBackend::CreateTextureFromPixels( int32 Width, int32 Height, const FColor32* Pixels )
{
	if ( !IsValidPixelSize( Width, Height, Pixels ) )
	{
		return NAsset::InvalidHandle;
	}
	BASEIMAGE Base = {};
	if ( !FillBaseImage( Base, Width, Height, Pixels ) )
	{
		return NAsset::InvalidHandle;
	}
	const int Handle = CreateGraphFromBaseImage( &Base );
	ReleaseBaseImage( &Base );
	return Handle >= 0 ? Handle : NAsset::InvalidHandle;
}

bool CDxLibAssetBackend::UpdateTexturePixels( int32 Texture, int32 Width, int32 Height, const FColor32* Pixels )
{
	int TextureWidth = 0;
	int TextureHeight = 0;
	if ( Texture < 0 || !IsValidPixelSize( Width, Height, Pixels ) || GetGraphSize( Texture, &TextureWidth, &TextureHeight ) != 0 || TextureWidth != Width || TextureHeight != Height )
	{
		return false;
	}
	BASEIMAGE Base = {};
	if ( !FillBaseImage( Base, Width, Height, Pixels ) )
	{
		return false;
	}
	const int Result = ReCreateGraphFromBaseImage( &Base, Texture );
	ReleaseBaseImage( &Base );
	return Result == 0;
}

bool CDxLibAssetBackend::DecodeImage( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	if ( FileImage == nullptr || Size == 0 || Size > static_cast<usize>( INT_MAX ) )
	{
		return false;
	}
	// DxLib は、切れた PNG などを渡すと、範囲外を読んで、アクセス違反で落ちる（実測。メモリからもファイルからも）。
	// 構造を確かめられる形式（PNG・BMP・JPEG）は、最後まであるかを、先に確かめる
	if ( !ImageDataLooksComplete( static_cast<const uint8*>( FileImage ), Size ) )
	{
		return false;
	}
	const int Soft = LoadARGB8ColorSoftImageToMem( FileImage, static_cast<int>( Size ) );
	const bool bRead = ReadSoftImage( Soft, OutWidth, OutHeight, OutPixels );
	if ( Soft >= 0 )
	{
		DeleteSoftImage( Soft );
	}
	return bRead;
}

bool CDxLibAssetBackend::DecodeImageFile( const FString& Path, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	if ( Path.IsEmpty() )
	{
		return false;
	}
	// 切れた画像で DxLib が落ちないよう、ファイルの中身を先に確かめる（DecodeImage と同じ理由）
	if ( !FileLooksComplete( Path ) )
	{
		return false;
	}
	const int Soft = LoadARGB8ColorSoftImage( *Path );
	const bool bRead = ReadSoftImage( Soft, OutWidth, OutHeight, OutPixels );
	if ( Soft >= 0 )
	{
		DeleteSoftImage( Soft );
	}
	return bRead;
}

bool CDxLibAssetBackend::SavePixels( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options )
{
	if ( Path.IsEmpty() || !IsValidPixelSize( Width, Height, Pixels ) )
	{
		return false;
	}
	const int Soft = MakeARGB8ColorSoftImage( Width, Height );
	if ( Soft < 0 )
	{
		return false;
	}
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		for ( int32 Column = 0; Column < Width; ++Column )
		{
			const FColor32& Pixel = Pixels[Row * Width + Column];
			DrawPixelSoftImage_Unsafe_ARGB8( Soft, Column, Row, Pixel.r, Pixel.g, Pixel.b, Pixel.a );
		}
	}
	int Result = -1;
	switch ( Options.Format )
	{
	case EImageFileFormat::Bmp:
		Result = SaveSoftImageToBmp( *Path, Soft );
		break;
	case EImageFileFormat::Jpeg:
		Result = SaveSoftImageToJpeg( *Path, Soft, NMath::Clamp( Options.JpegQuality, 0, 100 ), TRUE );
		break;
	default:
		Result = SaveSoftImageToPng( *Path, Soft, NMath::Clamp( Options.PngCompression, -1, 9 ) );
		break;
	}
	DeleteSoftImage( Soft );
	return Result == 0;
}

//-----------------------------------------------------------------------------
// モデル
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadModel( const FString& Path, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	return MV1LoadModel( *Path );
}

int32 CDxLibAssetBackend::DuplicateModel( int32 Source )
{
	return MV1DuplicateModel( Source );
}

void CDxLibAssetBackend::DeleteModel( int32 Model )
{
	MV1DeleteModel( Model );
}

int32 CDxLibAssetBackend::GetModelAnimCount( int32 Model )
{
	return MV1GetAnimNum( Model );
}

FString CDxLibAssetBackend::GetModelAnimName( int32 Model, int32 AnimIndex )
{
	const char* Name = MV1GetAnimName( Model, AnimIndex );
	return Name != nullptr ? FString( Name ) : FString();
}

float32 CDxLibAssetBackend::GetModelAnimLength( int32 Model, int32 AnimIndex )
{
	return MV1GetAnimTotalTime( Model, AnimIndex );
}

int32 CDxLibAssetBackend::AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel )
{
	return MV1AttachAnim( Model, AnimIndex, SourceModel, TRUE );
}

void CDxLibAssetBackend::DetachModelAnim( int32 Model, int32 AttachIndex )
{
	MV1DetachAnim( Model, AttachIndex );
}

int32 CDxLibAssetBackend::GetModelMeshCount( int32 Model )
{
	return MV1GetMeshNum( Model );
}

bool CDxLibAssetBackend::GetModelBounds( int32 Model, float32 ( &OutMin )[3], float32 ( &OutMax )[3] )
{
	const int32 NumMeshes = MV1GetMeshNum( Model );
	if ( NumMeshes <= 0 ) return false;

	// 描くときと同じ位置（フレームの変換を適用した頂点）の範囲。取れれば、これを使う
	if ( MV1SetupReferenceMesh( Model, -1, TRUE, TRUE ) >= 0 )
	{
		const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Model, -1, TRUE, TRUE );
		const bool bHasPolygons = Reference.PolygonNum > 0 && Reference.VertexNum > 0;
		if ( bHasPolygons )
		{
			OutMin[0] = Reference.MinPosition.x;
			OutMin[1] = Reference.MinPosition.y;
			OutMin[2] = Reference.MinPosition.z;
			OutMax[0] = Reference.MaxPosition.x;
			OutMax[1] = Reference.MaxPosition.y;
			OutMax[2] = Reference.MaxPosition.z;
		}
		MV1TerminateReferenceMesh( Model, -1, TRUE, TRUE );
		if ( bHasPolygons ) return true;
	}

	bool bAny = false;
	for ( int32 Mesh = 0; Mesh < NumMeshes; ++Mesh )
	{
		const VECTOR Low = MV1GetMeshMinPosition( Model, Mesh );
		const VECTOR High = MV1GetMeshMaxPosition( Model, Mesh );
		const float32 LowValues[3] = { Low.x, Low.y, Low.z };
		const float32 HighValues[3] = { High.x, High.y, High.z };
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			OutMin[Axis] = bAny ? ( LowValues[Axis] < OutMin[Axis] ? LowValues[Axis] : OutMin[Axis] ) : LowValues[Axis];
			OutMax[Axis] = bAny ? ( HighValues[Axis] > OutMax[Axis] ? HighValues[Axis] : OutMax[Axis] ) : HighValues[Axis];
		}
		bAny = true;
	}
	return bAny;
}

bool CDxLibAssetBackend::GetModelCollisionMesh( int32 Model, TArray<float32>& OutPositions, TArray<uint32>& OutIndices )
{
	if ( MV1GetMeshNum( Model ) <= 0 ) return false;
	if ( MV1SetupReferenceMesh( Model, -1, TRUE, TRUE ) < 0 ) return false;
	const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Model, -1, TRUE, TRUE );
	const bool bOk = Reference.PolygonNum > 0 && Reference.VertexNum > 0 && Reference.Polygons != nullptr && Reference.Vertexs != nullptr;
	if ( bOk )
	{
		OutPositions.Reset();
		OutIndices.Reset();
		OutPositions.Reserve( Reference.VertexNum * 3 );
		OutIndices.Reserve( Reference.PolygonNum * 3 );
		for ( int32 i = 0; i < Reference.VertexNum; ++i )
		{
			OutPositions.Add( Reference.Vertexs[i].Position.x );
			OutPositions.Add( Reference.Vertexs[i].Position.y );
			OutPositions.Add( Reference.Vertexs[i].Position.z );
		}
		for ( int32 i = 0; i < Reference.PolygonNum; ++i )
		{
			for ( int32 Corner = 0; Corner < 3; ++Corner ) OutIndices.Add( static_cast<uint32>( Reference.Polygons[i].VIndex[Corner] ) );
		}
	}
	MV1TerminateReferenceMesh( Model, -1, TRUE, TRUE );
	return bOk;
}

bool CDxLibAssetBackend::CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer )
{
	OutVertexBuffer = NAsset::InvalidHandle;
	OutIndexBuffer = NAsset::InvalidHandle;
	if ( Vertices == nullptr || Indices == nullptr || NumVertices < 3 || NumIndices < 3 ) return false;

	TArray<VERTEX3D> Converted;
	Converted.Reserve( NumVertices );
	const COLOR_U8 Specular = GetColorU8( 0, 0, 0, 0 );
	for ( int32 i = 0; i < NumVertices; ++i )
	{
		VERTEX3D Vertex{};
		Vertex.pos = VGet( Vertices[i].Position[0], Vertices[i].Position[1], Vertices[i].Position[2] );
		Vertex.norm = VGet( Vertices[i].Normal[0], Vertices[i].Normal[1], Vertices[i].Normal[2] );
		Vertex.dif = GetColorU8( Vertices[i].Color[0], Vertices[i].Color[1], Vertices[i].Color[2], Vertices[i].Color[3] );
		Vertex.spc = Specular;
		Vertex.u = Vertices[i].U;
		Vertex.v = Vertices[i].V;
		Converted.Add( Vertex );
	}
	const int VertexBuffer = CreateVertexBuffer( NumVertices, DX_VERTEX_TYPE_NORMAL_3D );
	if ( VertexBuffer < 0 ) return false;
	const int IndexBuffer = CreateIndexBuffer( NumIndices, DX_INDEX_TYPE_32BIT );
	if ( IndexBuffer < 0 )
	{
		DeleteVertexBuffer( VertexBuffer );
		return false;
	}
	if ( SetVertexBufferData( 0, Converted.GetData(), NumVertices, VertexBuffer ) < 0 || SetIndexBufferData( 0, Indices, NumIndices, IndexBuffer ) < 0 )
	{
		::DeleteVertexBuffer( VertexBuffer );
		::DeleteIndexBuffer( IndexBuffer );
		return false;
	}
	OutVertexBuffer = VertexBuffer;
	OutIndexBuffer = IndexBuffer;
	return true;
}

void CDxLibAssetBackend::DeleteVertexBuffer( int32 VertexBuffer )
{
	if ( VertexBuffer >= 0 ) ::DeleteVertexBuffer( VertexBuffer );
}

void CDxLibAssetBackend::DeleteIndexBuffer( int32 IndexBuffer )
{
	if ( IndexBuffer >= 0 ) ::DeleteIndexBuffer( IndexBuffer );
}

int32 CDxLibAssetBackend::MakeVertexBuffer( int32 NumVertices )
{
	return NumVertices >= 1 ? ::CreateVertexBuffer( NumVertices, DX_VERTEX_TYPE_NORMAL_3D ) : NAsset::InvalidHandle;
}

bool CDxLibAssetBackend::WriteVertexBuffer( int32 VertexBuffer, int32 StartIndex, const FDrawVertex3D* Vertices, int32 NumVertices )
{
	if ( VertexBuffer < 0 || Vertices == nullptr || StartIndex < 0 || NumVertices < 1 )
	{
		return false;
	}
	TArray<VERTEX3D> Converted;
	Converted.SetNum( NumVertices );
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		if ( !NDxLibVertex::ToDxVertex( Vertices[Index], Converted[Index] ) )
		{
			return false;
		}
	}
	return ::SetVertexBufferData( StartIndex, Converted.GetData(), NumVertices, VertexBuffer ) == 0;
}

int32 CDxLibAssetBackend::MakeIndexBuffer( int32 NumIndices, bool b32Bit )
{
	return NumIndices >= 1 ? ::CreateIndexBuffer( NumIndices, b32Bit ? DX_INDEX_TYPE_32BIT : DX_INDEX_TYPE_16BIT ) : NAsset::InvalidHandle;
}

bool CDxLibAssetBackend::WriteIndexBuffer( int32 IndexBuffer, int32 StartIndex, const void* Indices, int32 NumIndices )
{
	if ( IndexBuffer < 0 || Indices == nullptr || StartIndex < 0 || NumIndices < 1 )
	{
		return false;
	}
	return ::SetIndexBufferData( StartIndex, Indices, NumIndices, IndexBuffer ) == 0;
}

int32 CDxLibAssetBackend::MakeShader( EShaderStage Stage, const void* Code, int32 NumBytes )
{
	if ( Code == nullptr || NumBytes < 1 )
	{
		return NAsset::InvalidHandle;
	}
	// DxLib は、壊れたバイト列・途中で切れたバイト列・種類の違うもの（頂点用をピクセル用として読むなど）に -1 を返す（実測）
	const int Handle = Stage == EShaderStage::Pixel ? LoadPixelShaderFromMem( Code, NumBytes ) : LoadVertexShaderFromMem( Code, NumBytes );
	return Handle >= 0 ? Handle : NAsset::InvalidHandle;
}

void CDxLibAssetBackend::DeleteShader( int32 Shader )
{
	if ( Shader >= 0 )
	{
		::DeleteShader( Shader );
	}
}

bool CDxLibAssetBackend::CompileShader( EShaderStage Stage, const char* Source, int32 NumBytes, const char* SourceName, const char* EntryPoint, const char* ShaderModel, TArray<uint8>& OutCode, FString& OutMessage )
{
	OutCode.Reset();
	OutMessage = FString();
	if ( Source == nullptr || NumBytes < 1 || EntryPoint == nullptr || EntryPoint[0] == '\0' || ShaderModel == nullptr )
	{
		OutMessage = "the source, the entry point and the shader model are required";
		return false;
	}
	// DxLib が読めるのは 4.0 / 4.1 / 5.0（実測: 3.0 は読めない）
	if ( NCString::Compare( ShaderModel, "4_0" ) != 0 && NCString::Compare( ShaderModel, "4_1" ) != 0 && NCString::Compare( ShaderModel, "5_0" ) != 0 )
	{
		OutMessage = "the shader model must be 4_0, 4_1 or 5_0";
		return false;
	}
	const FD3DCompileFunction Compile = FindD3DCompile();
	if ( Compile == nullptr )
	{
		OutMessage = "d3dcompiler_47.dll is not available";
		return false;
	}
	char Target[16] = {};
	NCString::Format( Target, sizeof( Target ), "%s_%s", Stage == EShaderStage::Pixel ? "ps" : "vs", ShaderModel );

	ID3DBlob* Code = nullptr;
	ID3DBlob* Errors = nullptr;
	// ソースの名前は、メッセージと、#include を探すフォルダー（ファイルの名前のとき）に使われる
	const HRESULT Result = Compile( Source, static_cast<SIZE_T>( NumBytes ), SourceName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoint, Target, 0, 0, &Code, &Errors );
	if ( Errors != nullptr )
	{
		OutMessage = FString( static_cast<const char*>( Errors->GetBufferPointer() ), static_cast<int32>( Errors->GetBufferSize() ) );
		Errors->Release();
	}
	if ( FAILED( Result ) || Code == nullptr )
	{
		if ( Code != nullptr )
		{
			Code->Release();
		}
		if ( OutMessage.IsEmpty() )
		{
			OutMessage = "the shader cannot be compiled";
		}
		return false;
	}
	OutCode.Append( static_cast<const uint8*>( Code->GetBufferPointer() ), static_cast<int32>( Code->GetBufferSize() ) );
	Code->Release();
	return true;
}

int32 CDxLibAssetBackend::MakeShadowMap( int32 Width, int32 Height )
{
	// DxLib は 0 以下や大きすぎる大きさを断る（実測: 1〜16384 が作れる）が、渡す前にも確かめる
	if ( Width < 1 || Height < 1 || Width > NDrawShadowMap::MaxSize || Height > NDrawShadowMap::MaxSize )
	{
		return NAsset::InvalidHandle;
	}
	return ::MakeShadowMap( Width, Height );
}

void CDxLibAssetBackend::DeleteShadowMap( int32 ShadowMap )
{
	if ( ShadowMap >= 0 )
	{
		::DeleteShadowMap( ShadowMap );
	}
}

bool CDxLibAssetBackend::GetShadowMapViewProjection( int32 ShadowMap, float32 ( &OutMatrix )[16] )
{
	MATRIX Matrix = {};
	if ( ShadowMap < 0 || ::GetShadowMapViewProjectionMatrix( ShadowMap, &Matrix ) != 0 )
	{
		return false;
	}
	// まだ一度も影を描いていないシャドウマップは、成功して、全部 0 の行列を返す（実測）。行列の右下は、描いたあとは必ず 1 になる
	if ( Matrix.m[3][3] == 0.0f )
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

int32 CDxLibAssetBackend::GetModelFrameCount( int32 Model )
{
	return MV1GetFrameNum( Model );
}

FString CDxLibAssetBackend::GetModelFrameName( int32 Model, int32 FrameIndex )
{
	const char* Name = MV1GetFrameName( Model, FrameIndex );
	return Name != nullptr ? FString( Name ) : FString();
}

bool CDxLibAssetBackend::SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning )
{
	int SaveType = MV1_SAVETYPE_NORMAL;
	if ( Options.Type == EModelSaveType::MeshOnly ) SaveType = MV1_SAVETYPE_MESH;
	else if ( Options.Type == EModelSaveType::AnimOnly ) SaveType = MV1_SAVETYPE_ANIM;

	// 圧縮: 法線 8bit・座標 16bit・ウェイト 8bit なし・アニメーション 16bit（DxLib の既定）。しない: すべて 32bit
	const int Normal8 = Options.bCompress ? 1 : 0, Position16 = Options.bCompress ? 1 : 0, Anim16 = Options.bCompress ? 1 : 0;
	const int Result = MV1SaveModelToMV1File( Model, *Path, SaveType, -1, TRUE, Normal8, Position16, 0, Anim16 );
	if ( Result == -2 ) OutWarning = "the model has animations that are not used by any bone";
	return Result == 0 || Result == -2;
}

//-----------------------------------------------------------------------------
// フォント
//-----------------------------------------------------------------------------
namespace
{
	// 文字のなめらかさと、縁取りの有無から、DxLib のフォントの種類を決める
	int ToDxFontType( EFontAntialias Antialias, bool bEdge )
	{
		switch ( Antialias )
		{
		case EFontAntialias::None:
			return bEdge ? DX_FONTTYPE_EDGE : DX_FONTTYPE_NORMAL;
		case EFontAntialias::High4x4:
			return bEdge ? DX_FONTTYPE_ANTIALIASING_EDGE_4X4 : DX_FONTTYPE_ANTIALIASING_4X4;
		case EFontAntialias::High8x8:
			return bEdge ? DX_FONTTYPE_ANTIALIASING_EDGE_8X8 : DX_FONTTYPE_ANTIALIASING_8X8;
		case EFontAntialias::High16x16:
			return bEdge ? DX_FONTTYPE_ANTIALIASING_EDGE_16X16 : DX_FONTTYPE_ANTIALIASING_16X16;
		default:
			return bEdge ? DX_FONTTYPE_ANTIALIASING_EDGE : DX_FONTTYPE_ANTIALIASING;
		}
	}

	// 字間・行間の範囲（DxLib は範囲の外でも失敗を返さないので、ここで収める）
	constexpr int MinLetterSpacing = -256;
	constexpr int MaxLetterSpacing = 1024;
	constexpr int MaxLineHeight = 4096;
}

int32 CDxLibAssetBackend::MakeFont( const FFontDesc& Desc )
{
	const int FontType = ToDxFontType( Desc.Antialias, Desc.EdgeSize > 0 );
	const int Handle = CreateFontToHandle( Desc.Face.IsEmpty() ? nullptr : *Desc.Face, Desc.Size, Desc.Thickness, FontType, -1, Desc.EdgeSize > 0 ? Desc.EdgeSize : -1,
										   Desc.bItalic ? TRUE : FALSE );
	if ( Handle < 0 )
	{
		return Handle;
	}
	if ( Desc.LetterSpacing != 0 )
	{
		SetFontSpaceToHandle( NMath::Clamp( Desc.LetterSpacing, MinLetterSpacing, MaxLetterSpacing ), Handle );
	}
	if ( Desc.LineHeight >= 0 )
	{
		SetFontLineSpaceToHandle( NMath::Min( Desc.LineHeight, MaxLineHeight ), Handle );
	}
	return Handle;
}

void CDxLibAssetBackend::DeleteFont( int32 Font )
{
	DeleteFontToHandle( Font );
}

int32 CDxLibAssetBackend::MeasureText( int32 Font, const char* Text )
{
	if ( Text == nullptr ) return 0;
	const int Length = static_cast<int>( NCString::Length( Text ) );
	return Font >= 0 ? GetDrawStringWidthToHandle( Text, Length, Font ) : GetDrawStringWidth( Text, Length );
}

bool CDxLibAssetBackend::GetFontMetrics( int32 Font, FFontMetrics& OutMetrics )
{
	if ( Font < 0 || CheckFontHandleValid( Font ) == FALSE )
	{
		return false;
	}
	OutMetrics.Size = GetFontSizeToHandle( Font );
	OutMetrics.LineHeight = GetFontLineSpaceToHandle( Font );
	OutMetrics.Ascent = GetFontAscentToHandle( Font );
	OutMetrics.MaxWidth = GetFontMaxWidthToHandle( Font );
	OutMetrics.EdgeSize = GetFontEdgeSizeToHandle( Font );
	OutMetrics.LetterSpacing = GetFontSpaceToHandle( Font );
	// 1 行の文字の高さは、1 文字を測って得る（大きさに、上下の余白と縁取りの分が足される）
	int Width = 0;
	int Height = 0;
	int NumLines = 0;
	GetDrawStringSizeToHandle( &Width, &Height, &NumLines, "A", 1, Font );
	OutMetrics.GlyphHeight = Height;
	return OutMetrics.Size >= 0;
}

bool CDxLibAssetBackend::MeasureTextSize( int32 Font, const char* Text, FTextSize& OutSize )
{
	// 壊れた UTF-8（途中で切れた並びなど）を渡すと、DxLib は、文字列の終わりの先まで読む（実行のたびに結果が違い、まれに異常終了する）ので、渡さない
	if ( Text == nullptr || Font < 0 || CheckFontHandleValid( Font ) == FALSE || !FString( Text ).IsValidUtf8() )
	{
		return false;
	}
	int Width = 0;
	int Height = 0;
	int NumLines = 0;
	// 戻り値は、エラーのとき -1、そうでなければ幅（エラーコードではない）
	if ( GetDrawStringSizeToHandle( &Width, &Height, &NumLines, Text, static_cast<int>( NCString::Length( Text ) ), Font ) < 0 )
	{
		return false;
	}
	OutSize.Width = Width;
	OutSize.Height = Height;
	OutSize.NumLines = NumLines;
	return true;
}

bool CDxLibAssetBackend::GetTextGlyphs( int32 Font, const char* Text, TArray<FTextGlyph>& OutGlyphs )
{
	OutGlyphs.Reset();
	// MeasureTextSize と同じく、壊れた UTF-8 は、DxLib に渡さずに失敗にする（折り返しは、渡す前に、正しい形に直している）
	if ( Text == nullptr || Font < 0 || CheckFontHandleValid( Font ) == FALSE || !FString( Text ).IsValidUtf8() )
	{
		return false;
	}
	const int Length = static_cast<int>( NCString::Length( Text ) );
	if ( Length == 0 )
	{
		return true;
	}
	// バッファが無い（nullptr）と、文字の数だけが返る
	const int Count = GetDrawStringCharInfoToHandle( nullptr, 0, Text, Length, Font );
	if ( Count < 0 )
	{
		return false;
	}
	TArray<DRAWCHARINFO> Info;
	Info.SetNum( Count );
	if ( Count > 0 && GetDrawStringCharInfoToHandle( Info.GetData(), static_cast<size_t>( Count ) * sizeof( DRAWCHARINFO ), Text, Length, Font ) < 0 )
	{
		return false;
	}
	int32 Offset = 0;
	for ( int32 Index = 0; Index < Count && Offset < Length; ++Index )
	{
		FTextGlyph Glyph;
		Glyph.Offset = Offset;
		Glyph.Bytes = NMath::Clamp( static_cast<int32>( Info[Index].Bytes ), 1, Length - Offset );
		Glyph.X = NMath::RoundToInt( Info[Index].DrawX );
		Glyph.Y = NMath::RoundToInt( Info[Index].DrawY );
		Glyph.Width = NMath::RoundToInt( Info[Index].SizeX );
		OutGlyphs.Add( Glyph );
		Offset += Glyph.Bytes;
	}
	return true;
}

bool CDxLibAssetBackend::IsFontAvailable( const FString& Face )
{
	return !Face.IsEmpty() && CheckFontName( *Face ) != FALSE;
}

void CDxLibAssetBackend::GetFontNames( TArray<FString>& OutNames )
{
	OutNames.Reset();
	// DxLib は、名前を 64 バイトずつ区切って返す。バッファが無い（nullptr）と、名前の数だけが返る
	constexpr int NameBytes = 64;
	const int Count = EnumFontNameEx( nullptr, 0, DX_CHARSET_DEFAULT );
	if ( Count <= 0 )
	{
		return;
	}
	TArray<char> Buffer;
	Buffer.SetNum( Count * NameBytes, 0 );
	EnumFontNameEx( Buffer.GetData(), Count, DX_CHARSET_DEFAULT );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		char* Name = Buffer.GetData() + Index * NameBytes;
		Name[NameBytes - 1] = '\0';
		OutNames.Add( FString( Name ) );
	}
}

bool CDxLibAssetBackend::AddFontCharImage( int32 Font, const char* Character, int32 Image, int32 DrawX, int32 DrawY, int32 Advance )
{
	if ( Font < 0 || Character == nullptr || Image < 0 || CheckFontHandleValid( Font ) == FALSE )
	{
		return false;
	}
	return AddFontImageToHandle( Font, Character, Image, DrawX, DrawY, Advance ) == 0;
}

void CDxLibAssetBackend::RemoveFontCharImage( int32 Font, const char* Character )
{
	if ( Font >= 0 && Character != nullptr && CheckFontHandleValid( Font ) != FALSE )
	{
		SubFontImageToHandle( Font, Character );
	}
}

bool CDxLibAssetBackend::AddFontFile( const FString& Path )
{
	// パスは UTF-8 なので、A 系（システムのコードページ）ではなく W 系で渡す
	return AddFontResourceExW( Path.ToWide().GetData(), FR_PRIVATE, nullptr ) > 0;
}

void CDxLibAssetBackend::RemoveFontFile( const FString& Path )
{
	RemoveFontResourceExW( Path.ToWide().GetData(), FR_PRIVATE, nullptr );
}

//-----------------------------------------------------------------------------
// サウンド
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	SetCreateSoundDataType( Mode == ESoundLoadMode::Stream ? DX_SOUNDDATATYPE_FILE : DX_SOUNDDATATYPE_MEMNOPRESS );
	const int Handle = LoadSoundMem( *Path );
	SetCreateSoundDataType( DX_SOUNDDATATYPE_MEMNOPRESS );   // 既定に戻す（ほかの読み込みに影響させない）
	return Handle;
}

int32 CDxLibAssetBackend::DuplicateSound( int32 Source )
{
	return DuplicateSoundMem( Source );
}

void CDxLibAssetBackend::DeleteSound( int32 Sound )
{
	DeleteSoundMem( Sound );
}

float32 CDxLibAssetBackend::GetSoundLength( int32 Sound )
{
	const LONGLONG Milliseconds = GetSoundTotalTime( Sound );
	return Milliseconds > 0 ? static_cast<float32>( Milliseconds ) / 1000.0f : -1.0f;
}

//-----------------------------------------------------------------------------
// メモリ
//-----------------------------------------------------------------------------
FBackendMemoryInfo CDxLibAssetBackend::GetMemoryInfo()
{
	FBackendMemoryInfo Info;
	ULONGLONG Total = 0;
	ULONGLONG Used = 0;
	if ( GetVideoMemorySizeEx( &Total, &Used ) == 0 )
	{
		Info.VideoMemoryTotal = static_cast<int64>( Total );
		Info.VideoMemoryUsed = static_cast<int64>( Used );
	}

	PROCESS_MEMORY_COUNTERS Counters = {};
	Counters.cb = sizeof( Counters );
	if ( GetProcessMemoryInfo( GetCurrentProcess(), &Counters, sizeof( Counters ) ) ) Info.ProcessWorkingSet = static_cast<int64>( Counters.WorkingSetSize );

	Info.NumTextureHandles = GetHandleNum( DX_HANDLETYPE_GRAPH );
	Info.NumModelHandles = GetHandleNum( DX_HANDLETYPE_MODEL );
	Info.NumSoundHandles = GetHandleNum( DX_HANDLETYPE_SOUND );
	Info.NumFontHandles = GetHandleNum( DX_HANDLETYPE_FONT );
	return Info;
}

int64 CDxLibAssetBackend::EstimateModelBytes( int32 Model )
{
	// 頂点（1 つ 64 バイトとして）、フレームの行列、使っているテクスチャ（幅 x 高さ x 4）
	int64 Bytes = static_cast<int64>( MV1GetFrameNum( Model ) ) * 64;
	const int NumLists = MV1GetTriangleListNum( Model );
	for ( int i = 0; i < NumLists; ++i ) Bytes += static_cast<int64>( MV1GetTriangleListVertexNum( Model, i ) ) * 64;

	const int NumTextures = MV1GetTextureNum( Model );
	for ( int i = 0; i < NumTextures; ++i )
	{
		const int Graph = MV1GetTextureGraphHandle( Model, i );
		int Width = 0;
		int Height = 0;
		if ( Graph >= 0 && GetGraphSize( Graph, &Width, &Height ) == 0 ) Bytes += static_cast<int64>( Width ) * Height * 4;
	}
	return Bytes;
}

int64 CDxLibAssetBackend::EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode )
{
	// 展開後は 16bit ステレオ（1 サンプル 4 バイト）として見積もる。再生しながら読む音は、読み込み用の領域だけ
	if ( Mode == ESoundLoadMode::Stream ) return 2 * 64 * 1024;
	return static_cast<int64>( GetSoundTotalSample( Sound ) ) * 4;
}

//-----------------------------------------------------------------------------
// 非同期
//-----------------------------------------------------------------------------
bool CDxLibAssetBackend::IsLoading( int32 Handle )
{
	return CheckHandleASyncLoad( Handle ) == TRUE;
}

int32 CDxLibAssetBackend::GetNumLoading()
{
	return GetASyncLoadNum();
}
