// SPDX-License-Identifier: NOASSERTION

#include "AssetManager.h"

#include <cmath>

#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"

TSharedPtr<CTexture> CAssetManager::CreateTextureRegion( const TSharedPtr<CTexture>& Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	if ( GetBackend() == nullptr || !Source.IsValid() || !Source->IsReady() || Source->GetHandle() < 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 ) return nullptr;
	// 元画像寸法を問い合わせ、64bit和で領域の外側を検査する。
	const int32 SourceWidth = Source->GetWidth();
	const int32 SourceHeight = Source->GetHeight();
	if ( SourceWidth <= 0 || SourceHeight <= 0 || static_cast<int64>( X ) + Width > SourceWidth || static_cast<int64>( Y ) + Height > SourceHeight ) return nullptr;
	// 生存する親の識別子を含め、同じパスの別ハンドルを混同しないキー。
	const FString Key = MakeKey( FString::Printf( "region:%p:%d:%d:%d:%d", Source.Get(), X, Y, Width, Height ) );
	if ( TWeakPtr<CTexture>* Found = Textures.Find( Key ) )
	{
		if ( TSharedPtr<CTexture> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}
	// 作成時間の集計開始時刻。
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = GetBackend()->CreateTextureRegion( Source->GetHandle(), X, Y, Width, Height );
	if ( Handle < 0 ) { RecordFailure(); return nullptr; }
	// 通常Quad用にbackendが返すbase画像UV。
	float32 UvBounds[4] = {};
	const bool bHasUvBounds = GetBackend()->GetTextureUvBounds( Handle, UvBounds );
	if ( !bHasUvBounds || !NMath::IsFinite( UvBounds[0] ) || !NMath::IsFinite( UvBounds[1] ) || !NMath::IsFinite( UvBounds[2] ) || !NMath::IsFinite( UvBounds[3] ) || UvBounds[0] < 0.0f || UvBounds[1] < 0.0f || UvBounds[2] > 1.0f || UvBounds[3] > 1.0f || UvBounds[2] < UvBounds[0] || UvBounds[3] < UvBounds[1] )
	{
		GetBackend()->DeleteTexture( Handle );
		RecordFailure();
		return nullptr;
	}
	// 派生ハンドルと親の寿命を一つの共有アセットへまとめる。
	// 派生物の由来と矩形を調査表示するpath。
	const FString Path = FString::Printf( "%s#region:%d,%d,%d,%d", *Source->GetPath(), X, Y, Width, Height );
	TSharedPtr<CTexture> Region = MakeShared<CTexture>( Path, Handle, Source, Width, Height, UvBounds );
	RecordLoad( *Region, FString(), StartTick );
	Textures.Add( Key, Region );
	return Region;
}
