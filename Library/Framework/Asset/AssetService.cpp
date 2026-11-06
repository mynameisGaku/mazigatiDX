// SPDX-License-Identifier: MIT
#include "Library/Framework/Asset/Asset.h"

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"

//=============================================================================
// NAssetService（アセットがバックエンドを使う窓口）
//   アセットの実装（CTexture など）の破棄から呼ばれるので、グローバルな状態（バックエンドと、別スレッドで手放された資源の待ち行列）を持つ。
//   バックエンドの設定と、待ち行列の解放は、CAssetSubsystem が行う（Attach / SetBackend / FlushDeferred）。
//=============================================================================
namespace
{
	enum class EResource : uint8
	{
		Texture,
		Model,
		ModelMaterialGeometry,
		Font,
		Sound,
		VertexBuffer,
		IndexBuffer,
		ShadowMap,
		Shader,
	};

	struct FService
	{
		TAtomic<IAssetBackend*> Backend { nullptr };
		uint64 MainThread = 0;   // メインスレッドの識別子（NThread::GetCurrentThreadId）
		FMutex Mutex;
		TArray<TPair<EResource, int32>> Deferred;   // 別スレッドで手放されたもの（メインスレッドで解放する）
		TAtomic<int32> NumDeferredReleased { 0 };        // 上のうち、解放したものの累計（統計用）
		TMap<FString, int32> FontFileUsers;                  // メインスレッドだけ
	};

	FService& Service()
	{
		static FService Instance;
		return Instance;
	}

	void ReleaseNow( IAssetBackend& Backend, EResource Kind, int32 Handle )
	{
		switch ( Kind )
		{
		case EResource::Texture: Backend.DeleteTexture( Handle ); break;
		case EResource::Model: Backend.DeleteModel( Handle ); break;
		case EResource::ModelMaterialGeometry: Backend.DeleteModelMaterialGeometry( Handle ); break;
		case EResource::Font: Backend.DeleteFont( Handle ); break;
		case EResource::Sound: Backend.DeleteSound( Handle ); break;
		case EResource::VertexBuffer: Backend.DeleteVertexBuffer( Handle ); break;
		case EResource::IndexBuffer: Backend.DeleteIndexBuffer( Handle ); break;
		case EResource::ShadowMap: Backend.DeleteShadowMap( Handle ); break;
		case EResource::Shader: Backend.DeleteShader( Handle ); break;
		}
	}

	void Release( EResource Kind, int32 Handle )
	{
		if ( Handle < 0 )
		{
			return;
		}
		FService& S = Service();
		IAssetBackend* Backend = S.Backend.Load();
		if ( Backend == nullptr )
		{
			return;   // アプリの終了後。DxLib の終了処理がまとめて片付ける
		}

		if ( NThread::GetCurrentId() == S.MainThread )
		{
			ReleaseNow( *Backend, Kind, Handle );
			return;
		}
		FScopedLock Lock( S.Mutex );
		S.Deferred.Add( { Kind, Handle } );
	}

	void FlushDeferred()
	{
		FService& S = Service();
		IAssetBackend* Backend = S.Backend.Load();
		TArray<TPair<EResource, int32>> Local;
		{
			FScopedLock Lock( S.Mutex );
			Local = MoveTemp( S.Deferred );
		}
		if ( Backend == nullptr )
		{
			return;
		}
		for ( const auto& Entry : Local )
		{
			ReleaseNow( *Backend, Entry.Key, Entry.Value );
		}
		S.NumDeferredReleased.FetchAdd( Local.Num() );
	}

}

IAssetBackend* NAssetService::GetBackend() { return Service().Backend.Load(); }
bool NAssetService::IsMainThread() { return NThread::GetCurrentId() == Service().MainThread; }
void NAssetService::ReleaseTexture( int32 Handle ) { Release( EResource::Texture, Handle ); }
void NAssetService::ReleaseModel( int32 Handle ) { Release( EResource::Model, Handle ); }
void NAssetService::ReleaseModelMaterialGeometry( int32 Geometry ) { Release( EResource::ModelMaterialGeometry, Geometry ); }
void NAssetService::ReleaseFont( int32 Handle ) { Release( EResource::Font, Handle ); }
void NAssetService::ReleaseSound( int32 Handle ) { Release( EResource::Sound, Handle ); }
void NAssetService::ReleaseVertexBuffer( int32 Handle ) { Release( EResource::VertexBuffer, Handle ); }
void NAssetService::ReleaseIndexBuffer( int32 Handle ) { Release( EResource::IndexBuffer, Handle ); }
void NAssetService::ReleaseShadowMap( int32 Handle ) { Release( EResource::ShadowMap, Handle ); }
void NAssetService::ReleaseShader( int32 Handle ) { Release( EResource::Shader, Handle ); }

bool NAssetService::AddFontFile( const FString& Path )
{
	FService& S = Service();
	IAssetBackend* Backend = S.Backend.Load();
	if ( Backend == nullptr )
	{
		return false;
	}

	if ( int32* Users = S.FontFileUsers.Find( Path ) )
	{
		++*Users;
		return true;
	}
	if ( !Backend->AddFontFile( Path ) )
	{
		return false;
	}
	S.FontFileUsers.Add( Path, 1 );
	return true;
}

void NAssetService::RemoveFontFile( const FString& Path )
{
	FService& S = Service();
	int32* Users = S.FontFileUsers.Find( Path );
	if ( Users == nullptr )
	{
		return;
	}
	if ( --*Users > 0 )
	{
		return;
	}

	S.FontFileUsers.Remove( Path );
	if ( IAssetBackend* Backend = S.Backend.Load() )
	{
		Backend->RemoveFontFile( Path );
	}
}

void NAssetService::Attach( IAssetBackend* InBackend )
{
	FService& S = Service();
	S.MainThread = NThread::GetCurrentId();
	S.Backend.Store( InBackend );
}

void NAssetService::SetBackend( IAssetBackend* InBackend )
{
	Service().Backend.Store( InBackend );
}

void NAssetService::FlushDeferred()
{
	::FlushDeferred();
}

int32 NAssetService::GetNumDeferredReleased()
{
	return Service().NumDeferredReleased.Load();
}
