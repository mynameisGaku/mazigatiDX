// SPDX-License-Identifier: MIT
#include "TextureLoader.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

CTextureLoader::CTextureLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CTexture> CTextureLoader::Load( const FString& Path, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "texture", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CTexture> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadTexture", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = Context.GetTextureBackend()->LoadTexture( FullPath, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the texture '%s' (unsupported or broken image).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}

	TSharedPtr<CTexture> Texture = MakeShared<CTexture>( FullPath, Handle, bAsync );
	Context.GetRecorder().RecordLoad( *Texture, FullPath, StartTick );
	Cache.Add( Key, Texture );
	LOG( LogAsset, Verbose, "Loaded the texture '%s'%s", *FullPath, bAsync ? " (async)" : "" );
	return Texture;
}

TSharedPtr<CTexture> CTextureLoader::CreateFromMemory( const FString& Name, const void* FileImage, usize Size )
{
	GAME_THREAD_ONLY( nullptr );
	ITextureBackend* Backend = Context.GetTextureBackend();
	if ( Backend == nullptr || FileImage == nullptr || Size == 0 )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from memory: %s.", *Name, Backend == nullptr ? "no asset backend" : "no data" );
		return nullptr;
	}

	const FString Key = "memory:" + CAssetPathResolver::MakeKey( Name );
	if ( TSharedPtr<CTexture> Existing = Cache.Find( Key ) )
	{
		return Existing;
	}

	const int32 Handle = Backend->CreateTextureFromMemory( FileImage, Size );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the texture '%s' from memory (%d byte(s)).", *Name, static_cast<int32>( Size ) );
		return nullptr;
	}
	TSharedPtr<CTexture> Texture = MakeShared<CTexture>( Name, Handle, false );
	Cache.Add( Key, Texture );
	return Texture;
}

TSharedPtr<CRenderTarget> CTextureLoader::CreateRenderTarget( const FString& Name, const FRenderTargetDesc& Desc )
{
	GAME_THREAD_ONLY( nullptr );
	ITextureBackend* Backend = Context.GetTextureBackend();
	if ( Backend == nullptr )
	{
		LOG( LogAsset, Error, "Cannot create the render target '%s': no asset backend.", *Name );
		return nullptr;
	}
	if ( !Desc.IsValid() )
	{
		LOG( LogAsset, Error, "Cannot create the render target '%s': %d x %d (multisample %d, format %d, %d channel(s)) is not valid (the size must be 1 to %d, the multisample 0 to %d, the channels 4, or 1 / 2 for a float format).", *Name, Desc.Width, Desc.Height, Desc.MultiSamples, static_cast<int32>( Desc.Format ), Desc.Channels, FRenderTargetDesc::MaxSize, FRenderTargetDesc::MaxMultiSamples );
		return nullptr;
	}

	const FString Key = "target:" + CAssetPathResolver::MakeKey( Name );
	if ( TSharedPtr<CRenderTarget> Existing = DynamicCastSharedPtr<CRenderTarget>( Cache.Find( Key ) ) )
	{
		const FRenderTargetDesc& Old = Existing->GetDesc();
		if ( Old.Width == Desc.Width && Old.Height == Desc.Height && Old.bAlpha == Desc.bAlpha && Old.bDepth == Desc.bDepth && Old.MultiSamples == Desc.MultiSamples && Old.Format == Desc.Format && Old.Channels == Desc.Channels )
		{
			return Existing;
		}
		LOG( LogAsset, Warning, "The render target '%s' is already in use with another setting (%d x %d). A new one is created.", *Name, Old.Width, Old.Height );
	}

	const int32 Handle = Backend->CreateRenderTarget( Desc );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the render target '%s' (%d x %d).", *Name, Desc.Width, Desc.Height );
		return nullptr;
	}
	TSharedPtr<CRenderTarget> Target = MakeShared<CRenderTarget>( Name, Handle, Desc );
	Cache.Add( Key, Target );
	return Target;
}

TSharedPtr<CDynamicTexture> CTextureLoader::CreateDynamic( const FString& Name, const FPixelImage& Image )
{
	GAME_THREAD_ONLY( nullptr );
	ITextureBackend* Backend = Context.GetTextureBackend();
	if ( Backend == nullptr )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from pixels: no asset backend.", *Name );
		return nullptr;
	}
	if ( !Image.IsValid() )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from pixels: the image is empty (the size must be 1 to %d).", *Name, FPixelImage::MaxSize );
		return nullptr;
	}

	// 同じ名前で、まだ使われているものがあれば共有する（中身は替えない。替えるときは CDynamicTexture::Update）
	const FString Key = "pixels:" + CAssetPathResolver::MakeKey( Name );
	if ( TSharedPtr<CDynamicTexture> Existing = DynamicCastSharedPtr<CDynamicTexture>( Cache.Find( Key ) ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = Backend->CreateTextureFromPixels( Image.GetWidth(), Image.GetHeight(), Image.GetData() );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the texture '%s' from pixels (%d x %d).", *Name, Image.GetWidth(), Image.GetHeight() );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CDynamicTexture> Texture = MakeShared<CDynamicTexture>( Name, Handle, Image.GetWidth(), Image.GetHeight() );
	Context.GetRecorder().RecordLoad( *Texture, FString(), StartTick );
	Cache.Add( Key, Texture );
	return Texture;
}

TSharedPtr<CTexture> CTextureLoader::CreateRegion( const TSharedPtr<CTexture>& Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	GAME_THREAD_ONLY( nullptr );
	ITextureBackend* Backend = Context.GetTextureBackend();
	if ( Backend == nullptr || !Source.IsValid() || !Source->IsReady() || Source->GetHandle() < 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 )
	{
		return nullptr;
	}
	// 元画像寸法を問い合わせ、64bit和で領域の外側を検査する。
	const int32 SourceWidth = Source->GetWidth();
	const int32 SourceHeight = Source->GetHeight();
	if ( SourceWidth <= 0 || SourceHeight <= 0 || static_cast<int64>( X ) + Width > SourceWidth || static_cast<int64>( Y ) + Height > SourceHeight )
	{
		return nullptr;
	}
	// 生存する親の識別子を含め、同じパスの別ハンドルを混同しないキー。
	const FString Key = CAssetPathResolver::MakeKey( FString::Printf( "region:%p:%d:%d:%d:%d", Source.Get(), X, Y, Width, Height ) );
	if ( TSharedPtr<CTexture> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}
	// 作成時間の集計開始時刻。
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = Backend->CreateTextureRegion( Source->GetHandle(), X, Y, Width, Height );
	if ( Handle < 0 )
	{
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	// 通常Quad用にbackendが返すbase画像UV。
	float32 UvBounds[4] = {};
	const bool bHasUvBounds = Backend->GetTextureUvBounds( Handle, UvBounds );
	if ( !bHasUvBounds || !NMath::IsFinite( UvBounds[0] ) || !NMath::IsFinite( UvBounds[1] ) || !NMath::IsFinite( UvBounds[2] ) || !NMath::IsFinite( UvBounds[3] ) || UvBounds[0] < 0.0f || UvBounds[1] < 0.0f || UvBounds[2] > 1.0f || UvBounds[3] > 1.0f || UvBounds[2] < UvBounds[0] || UvBounds[3] < UvBounds[1] )
	{
		Backend->DeleteTexture( Handle );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	// 派生ハンドルと親の寿命を一つの共有アセットへまとめる。
	// 派生物の由来と矩形を調査表示するpath。
	const FString Path = FString::Printf( "%s#region:%d,%d,%d,%d", *Source->GetPath(), X, Y, Width, Height );
	TSharedPtr<CTexture> Region = MakeShared<CTexture>( Path, Handle, Source, Width, Height, UvBounds );
	Context.GetRecorder().RecordLoad( *Region, FString(), StartTick );
	Cache.Add( Key, Region );
	return Region;
}

TArray<TSharedPtr<CTexture>> CTextureLoader::Split( const TSharedPtr<CTexture>& Source, const FSpriteSheetGrid& Grid )
{
	GAME_THREAD_ONLY( TArray<TSharedPtr<CTexture>>() );
	TArray<TSharedPtr<CTexture>> Pieces;
	if ( Context.GetTextureBackend() == nullptr || !Source.IsValid() || !Source->IsReady() || Source->GetHandle() < 0 )
	{
		return Pieces;
	}
	TArray<FSpriteFrame> Frames;
	CSpriteSheet::ComputeGridFrames( Grid, Source->GetWidth(), Source->GetHeight(), Frames );
	for ( const FSpriteFrame& Frame : Frames )
	{
		// 格子が画像の外へはみ出すコマは、nullptr（番号は、格子のコマの番号のまま）
		Pieces.Add( CreateRegion( Source, Frame.X, Frame.Y, Frame.Width, Frame.Height ) );
	}
	return Pieces;
}

void CTextureLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CTextureLoader::Prune()
{
	Cache.Prune();
}

void CTextureLoader::Clear()
{
	Cache.Clear();
}
