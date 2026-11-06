// SPDX-License-Identifier: MIT
#include "FontLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Font/FontFile.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

namespace
{
	FString FontSuffix( int32 Size, const FFontStyle& Style )
	{
		return FString::Printf( "|%d|%d|%d|%d|%d|%d|%d", Size, Style.Thickness, Style.EdgeSize, Style.bItalic ? 1 : 0, static_cast<int32>( Style.Antialias ), Style.LetterSpacing, Style.LineHeight );
	}

	FFontDesc MakeDesc( const FString& Face, int32 Size, const FFontStyle& Style )
	{
		FFontDesc Desc;
		Desc.Face = Face;
		Desc.Size = Size;
		Desc.Thickness = Style.Thickness;
		Desc.EdgeSize = Style.EdgeSize;
		Desc.bItalic = Style.bItalic;
		Desc.Antialias = Style.Antialias;
		Desc.LetterSpacing = Style.LetterSpacing;
		Desc.LineHeight = Style.LineHeight;
		return Desc;
	}
}

CFontLoader::CFontLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CFont> CFontLoader::Create( const FString& Key, const FFontDesc& Desc, const FString& FontFilePath )
{
	GAME_THREAD_ONLY( nullptr );
	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadFont", Desc.Face.IsEmpty() ? FString( "(default)" ) : Desc.Face ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	IAssetBackend* Backend = Context.GetBackend();
	const int32 Handle = Backend->MakeFont( Desc );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the font '%s' (size %d).", Desc.Face.IsEmpty() ? "(default)" : *Desc.Face, Desc.Size );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CFont> Font = MakeShared<CFont>( Key, Handle, Desc, FontFilePath );
	// 寸法は、作ったあとは変わらないので、ここで 1 度だけ測って、フォントに持たせる（描画システムの中から読めるように）
	FFontMetrics Metrics;
	if ( Backend->GetFontMetrics( Handle, Metrics ) )
	{
		Font->SetMetrics( Metrics );
	}
	Context.GetRecorder().RecordLoad( *Font, FontFilePath.IsEmpty() ? FString() : FontFilePath, StartTick );
	Cache.Add( Key, Font );
	LOG( LogAsset, Verbose, "Created the font '%s' (size %d)", Desc.Face.IsEmpty() ? "(default)" : *Desc.Face, Desc.Size );
	return Font;
}

TSharedPtr<CFont> CFontLoader::LoadFile( const FString& FontFilePath, int32 Size, const FFontStyle& Style )
{
	const TOptional<FString> Full = Context.PrepareLoad( "font", FontFilePath );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = "file:" + CAssetPathResolver::MakeKey( FullPath ) + FontSuffix( Size, Style );
	if ( TSharedPtr<CFont> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	FString Face;
	if ( !NFontFile::ReadFaceName( FullPath, Face ) )
	{
		LOG( LogAsset, Error, "Cannot read the font name from '%s' (is it a .ttf / .otf / .ttc file?).", *FullPath );
		return nullptr;
	}
	if ( !NAssetService::AddFontFile( FullPath ) )
	{
		LOG( LogAsset, Error, "Failed to register the font file '%s' with the system.", *FullPath );
		return nullptr;
	}

	TSharedPtr<CFont> Font = Create( Key, MakeDesc( Face, Size, Style ), FullPath );
	if ( !Font.IsValid() )
	{
		NAssetService::RemoveFontFile( FullPath );   // CFont に引き渡せなかった分の登録を戻す
	}
	return Font;
}

TSharedPtr<CFont> CFontLoader::LoadSystem( const FString& FaceName, int32 Size, const FFontStyle& Style )
{
	if ( Context.GetBackend() == nullptr )
	{
		LOG( LogAsset, Error, "Cannot create the font '%s': no asset backend.", *FaceName );
		return nullptr;
	}
	const FString Key = "face:" + FaceName.ToLower() + FontSuffix( Size, Style );
	if ( TSharedPtr<CFont> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}
	return Create( Key, MakeDesc( FaceName, Size, Style ), FString() );
}

bool CFontLoader::IsAvailable( const FString& FaceName ) const
{
	IAssetBackend* Backend = Context.GetBackend();
	return Backend != nullptr && !FaceName.IsEmpty() && Backend->IsFontAvailable( FaceName );
}

TArray<FString> CFontLoader::GetSystemFontNames() const
{
	TArray<FString> Names;
	if ( IAssetBackend* Backend = Context.GetBackend() )
	{
		Backend->GetFontNames( Names );
	}
	return Names;
}

void CFontLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CFontLoader::Prune()
{
	Cache.Prune();
}

void CFontLoader::Clear()
{
	Cache.Clear();
}
