#include "AssetManager.h"


#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/FontFile.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY( LogAsset )

REGISTER_SUBSYSTEM( CAssetManager )

const char* ToString( EAssetType Type )
{
	switch ( Type )
	{
	case EAssetType::Texture: return "Texture";
	case EAssetType::SpriteSheet: return "SpriteSheet";
	case EAssetType::Model: return "Model";
	case EAssetType::Sound: return "Sound";
	case EAssetType::Font: return "Font";
	case EAssetType::ModelAnimation: return "ModelAnimation";
	case EAssetType::World: return "World";
	case EAssetType::Effect: return "Effect";
	}
	return "?";
}

//=============================================================================
// NAssetService（アセットがバックエンドを使う窓口）
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
		}
	}

	void Release( EResource Kind, int32 Handle )
	{
		if ( Handle < 0 ) return;
		FService& S = Service();
		IAssetBackend* Backend = S.Backend.Load();
		if ( Backend == nullptr ) return;   // アプリの終了後。DxLib の終了処理がまとめて片付ける

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
		if ( Backend == nullptr ) return;
		for ( const auto& Entry : Local ) ReleaseNow( *Backend, Entry.Key, Entry.Value );
		S.NumDeferredReleased.FetchAdd( Local.Num() );
	}

	// 読み込みの計測（プロファイラーのサンプルに、パスを表示名として付ける）
	uint64 HashPath( const FString& Path )
	{
		uint64 Hash = 1469598103934665603ull;
		for ( const char Ch : Path )
		{
			Hash ^= static_cast<uint8>( Ch );
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	FProfileScope MakeLoadScope( const char* Name, const FString& Path )
	{
		IProfiler* Recorder = NProfiler::GRecorder;
		return FProfileScope( "Asset", "", Name, HashPath( Path ), Recorder != nullptr ? Recorder->InternString( *Path ) : 0u );
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

bool NAssetService::AddFontFile( const FString& Path )
{
	FService& S = Service();
	IAssetBackend* Backend = S.Backend.Load();
	if ( Backend == nullptr ) return false;

	if ( int32* Users = S.FontFileUsers.Find( Path ) )
	{
		++*Users;
		return true;
	}
	if ( !Backend->AddFontFile( Path ) ) return false;
	S.FontFileUsers.Add( Path, 1 );
	return true;
}

void NAssetService::RemoveFontFile( const FString& Path )
{
	FService& S = Service();
	int32* Users = S.FontFileUsers.Find( Path );
	if ( Users == nullptr ) return;
	if ( --*Users > 0 ) return;

	S.FontFileUsers.Remove( Path );
	if ( IAssetBackend* Backend = S.Backend.Load() ) Backend->RemoveFontFile( Path );
}

//=============================================================================
// FAssetBundle
//=============================================================================
bool FAssetBundle::AreAllReady() const
{
	for ( const TSharedPtr<CAsset>& Asset : Assets )
	{
		if ( !Asset->IsReady() ) return false;
	}
	return true;
}

//=============================================================================
// CAssetManager
//=============================================================================
CAssetManager::CAssetManager() = default;
CAssetManager::~CAssetManager() = default;

void CAssetManager::Initialize( FSubsystemCollection& Collection )
{
	Backend = Collection.InitializeDependency<IAssetBackend>();
	FService& S = Service();
	S.MainThread = NThread::GetCurrentId();
	S.Backend.Store( GetBackend() );

	if ( Backend == nullptr )
	{
		LOG( LogAsset, Warning, "No asset backend is available. Assets cannot be loaded." );
		return;
	}
	LOG( LogAsset, Log, "Asset manager ready (root directory '%s')", *GetRootDirectory() );
}

void CAssetManager::Deinitialize()
{
	FlushDeferred();
	PendingSheets.Empty();
	Textures.Empty();
	SpriteSheets.Empty();
	Models.Empty();
	ModelAnimations.Empty();
	Worlds.Empty();
	Effects.Empty();
	Sounds.Empty();
	Fonts.Empty();

	// これ以降に手放されるアセットは、何もしない（DxLib の終了処理が片付ける）
	Service().Backend.Store( nullptr );
	Backend = nullptr;
}

IAssetBackend* CAssetManager::GetBackend() const
{
	return BackendOverride != nullptr ? BackendOverride : Backend;
}

void CAssetManager::SetBackendOverride( IAssetBackend* InBackend )
{
	FlushDeferred();
	BackendOverride = InBackend;
	Service().Backend.Store( GetBackend() );
}

void CAssetManager::Tick( float32 DeltaTime )
{
	PROFILE_SCOPE_CAT( "Asset", "Tick" );
	FlushDeferred();

	// 画像の読み込みを待っていたスプライトシートのコマ割りを、読み込みが終わったら作る
	for ( int32 Index = PendingSheets.Num() - 1; Index >= 0; --Index )
	{
		const TSharedPtr<CSpriteSheet> Sheet = PendingSheets[Index].Pin();
		if ( !Sheet.IsValid() || Sheet->IsReady() ) PendingSheets.RemoveAtSwap( Index );
	}

	PruneTimer += DeltaTime;
	if ( PruneTimer >= 5.0f )
	{
		PruneTimer = 0.0f;
		ReleaseUnused();
	}

	// 集計: プロファイラーの記録中は毎フレーム（カウンターとして残す）、それ以外は 0.5 秒ごと（画面に出す用）
	StatsTimer += DeltaTime;
	if ( NProfiler::GRecorder != nullptr || StatsTimer >= 0.5f )
	{
		StatsTimer = 0.0f;
		UpdateStats();
		RecordCounters();
	}
}

void CAssetManager::RecordLoad( CAsset& Asset, const FString& FullPath, int64 StartTick )
{
	const float32 Ms = static_cast<float32>( NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - StartTick ) );
	const int64 FileBytes = FullPath.IsEmpty() ? 0 : NFileSystem::GetFileSize( FullPath );
	Asset.SetLoadInfo( FileBytes > 0 ? FileBytes : 0, Ms );

	++Stats.NumLoads;
	Stats.TotalLoadMs += Ms;
	if ( Ms > Stats.MaxLoadMs )
	{
		Stats.MaxLoadMs = Ms;
		Stats.MaxLoadPath = FullPath;
	}
	StatsTimer = 0.5f;
}

void CAssetManager::UpdateStats()
{
	FAssetStats Fresh = Stats;   // 起動してからの累計は引き継ぐ
	for ( FAssetTypeStats& Type : Fresh.Types ) Type = FAssetTypeStats();
	Fresh.TotalCount = 0;
	Fresh.TotalMemoryBytes = 0;
	Fresh.TotalFileBytes = 0;
	Fresh.NumDeferredReleases = Service().NumDeferredReleased.Load();

	for ( const FAssetInfo& Info : GetLoadedAssets() )
	{
		FAssetTypeStats& Type = Fresh.Types[static_cast<int32>( Info.Type )];
		++Type.Count;
		if ( !Info.bReady ) ++Type.NumNotReady;
		Type.MemoryBytes += Info.MemoryBytes;
		Type.FileBytes += Info.FileBytes;
		Type.LoadMs += Info.LoadMs;
		++Fresh.TotalCount;
		Fresh.TotalMemoryBytes += Info.MemoryBytes;
		Fresh.TotalFileBytes += Info.FileBytes;
	}
	if ( Fresh.TotalMemoryBytes > Fresh.PeakMemoryBytes ) Fresh.PeakMemoryBytes = Fresh.TotalMemoryBytes;

	if ( IAssetBackend* B = GetBackend() )
	{
		Fresh.NumLoading = B->GetNumLoading();
		Fresh.Backend = B->GetMemoryInfo();
	}
	Stats = Fresh;
}

void CAssetManager::RecordCounters() const
{
	IProfiler* Recorder = NProfiler::GRecorder;
	if ( Recorder == nullptr ) return;

	static const char* const CountNames[NumAssetTypes] = { "Asset.Count.Texture", "Asset.Count.SpriteSheet", "Asset.Count.Model", "Asset.Count.Sound", "Asset.Count.Font",
														   "Asset.Count.ModelAnimation", "Asset.Count.World", "Asset.Count.Effect" };
	static const char* const MemoryNames[NumAssetTypes] = { "Asset.Memory.Texture", "Asset.Memory.SpriteSheet", "Asset.Memory.Model", "Asset.Memory.Sound", "Asset.Memory.Font",
															"Asset.Memory.ModelAnimation", "Asset.Memory.World", "Asset.Memory.Effect" };
	for ( int32 i = 0; i < NumAssetTypes; ++i )
	{
		Recorder->RecordCounter( Recorder->InternLiteral( CountNames[i] ), Stats.Types[i].Count, EProfileCounterUnit::Count );
		Recorder->RecordCounter( Recorder->InternLiteral( MemoryNames[i] ), static_cast<float64>( Stats.Types[i].MemoryBytes ), EProfileCounterUnit::Bytes );
	}
	PROFILE_COUNTER( "Asset.Count.Total", Stats.TotalCount );
	PROFILE_COUNTER_BYTES( "Asset.Memory.Total", Stats.TotalMemoryBytes );
	PROFILE_COUNTER_BYTES( "Asset.Memory.Peak", Stats.PeakMemoryBytes );
	PROFILE_COUNTER_BYTES( "Asset.File.Total", Stats.TotalFileBytes );
	PROFILE_COUNTER( "Asset.Loading", Stats.NumLoading );
	PROFILE_COUNTER( "Asset.Loads", Stats.NumLoads );
	PROFILE_COUNTER( "Asset.CacheHits", Stats.NumCacheHits );
	PROFILE_COUNTER( "Asset.LoadFailures", Stats.NumLoadFailures );
	PROFILE_COUNTER( "Asset.DeferredReleases", Stats.NumDeferredReleases );
	PROFILE_COUNTER_MS( "Asset.LoadMs.Total", Stats.TotalLoadMs );

	// プラットフォームの実測値
	if ( Stats.Backend.VideoMemoryTotal > 0 )
	{
		PROFILE_COUNTER_BYTES( "Memory.VRAM.Used", Stats.Backend.VideoMemoryUsed );
		PROFILE_COUNTER_BYTES( "Memory.VRAM.Total", Stats.Backend.VideoMemoryTotal );
	}
	if ( Stats.Backend.ProcessWorkingSet > 0 ) PROFILE_COUNTER_BYTES( "Memory.WorkingSet", Stats.Backend.ProcessWorkingSet );
	PROFILE_COUNTER( "Handle.Textures", Stats.Backend.NumTextureHandles );
	PROFILE_COUNTER( "Handle.Models", Stats.Backend.NumModelHandles );
	PROFILE_COUNTER( "Handle.Sounds", Stats.Backend.NumSoundHandles );
	PROFILE_COUNTER( "Handle.Fonts", Stats.Backend.NumFontHandles );
}

//-----------------------------------------------------------------------------
// パス
//-----------------------------------------------------------------------------
void CAssetManager::SetRootDirectory( const FString& Directory )
{
	RootDirectoryName = Directory;
	ResolvedRoot.Reset();
}

FString CAssetManager::GetRootDirectory() const
{
	if ( !ResolvedRoot.IsSet() )
	{
		TOptional<FString> Found = NFileSystem::FindDirectory( RootDirectoryName );
		ResolvedRoot = Found.IsSet() ? NFileSystem::GetFullPath( Found.Get( FString() ) ) : FString();
	}
	const FString Root = ResolvedRoot.Get( FString() );
	return Root.IsEmpty() ? RootDirectoryName : Root;
}

TOptional<FString> CAssetManager::ResolvePath( const FString& Path ) const
{
	if ( Path.IsEmpty() ) return TOptional<FString>();

	// ルートフォルダを最優先、次に作業ディレクトリ・exe のフォルダ（と親）。絶対パスはそのまま
	if ( NFileSystem::FileExists( Path ) && ( Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" ) ) )
	{
		return NFileSystem::GetFullPath( Path );
	}

	const FString Root = GetRootDirectory();
	if ( !Root.IsEmpty() )
	{
		const FString Candidate = Root / Path;
		if ( NFileSystem::FileExists( Candidate ) ) return NFileSystem::GetFullPath( Candidate );
	}
	if ( TOptional<FString> Found = NFileSystem::FindFile( Path ); Found.IsSet() ) return NFileSystem::GetFullPath( Found.Get( FString() ) );
	return TOptional<FString>();
}

FString CAssetManager::MakeKey( const FString& FullPath )
{
	// Windows のパスは大文字小文字を区別せず、区切りも '\' と '/' の両方がある。同じファイルは同じキーにする
	FString Key = FullPath.Replace( "\\", "/" ).ToLower();
	return Key;
}

TOptional<FString> CAssetManager::PrepareLoad( const char* Kind, const FString& Path )
{
	if ( GetBackend() == nullptr )
	{
		LOG( LogAsset, Error, "Cannot load the %s '%s': no asset backend.", Kind, *Path );
		RecordFailure();
		return TOptional<FString>();
	}
	TOptional<FString> Full = ResolvePath( Path );
	if ( !Full.IsSet() )
	{
		LOG( LogAsset, Error, "Cannot load the %s '%s': the file was not found (root directory '%s').", Kind, *Path, *GetRootDirectory() );
		RecordFailure();
	}
	return Full;
}

template<typename T>
void CAssetManager::PruneCache( TCache<T>& Cache )
{
	for ( auto It = Cache.CreateIterator(); It; ++It )
	{
		if ( !It.Value().IsValid() ) It.RemoveCurrent();
	}
}

void CAssetManager::ReleaseUnused()
{
	PruneCache( Textures );
	PruneCache( SpriteSheets );
	PruneCache( Models );
	PruneCache( ModelAnimations );
	PruneCache( Worlds );
	PruneCache( Effects );
	PruneCache( Sounds );
	PruneCache( Fonts );
}

//-----------------------------------------------------------------------------
// 画像
//-----------------------------------------------------------------------------
TSharedPtr<CTexture> CAssetManager::LoadTexture( const FString& Path, bool bAsync )
{
	const TOptional<FString> Full = PrepareLoad( "texture", Path );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CTexture>* Found = Textures.Find( Key ) )
	{
		if ( TSharedPtr<CTexture> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadTexture", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = GetBackend()->LoadTexture( FullPath, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the texture '%s' (unsupported or broken image).", *FullPath );
		RecordFailure();
		return nullptr;
	}

	TSharedPtr<CTexture> Texture = MakeShared<CTexture>( FullPath, Handle, bAsync );
	RecordLoad( *Texture, FullPath, StartTick );
	Textures.Add( Key, Texture );
	LOG( LogAsset, Verbose, "Loaded the texture '%s'%s", *FullPath, bAsync ? " (async)" : "" );
	return Texture;
}

TSharedPtr<CTexture> CAssetManager::CreateTextureFromMemory( const FString& Name, const void* FileImage, usize Size )
{
	if ( GetBackend() == nullptr || FileImage == nullptr || Size == 0 )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from memory: %s.", *Name, GetBackend() == nullptr ? "no asset backend" : "no data" );
		return nullptr;
	}

	const FString Key = "memory:" + MakeKey( Name );
	if ( TWeakPtr<CTexture>* Found = Textures.Find( Key ) )
	{
		if ( TSharedPtr<CTexture> Existing = Found->Pin() ) return Existing;
	}

	const int32 Handle = GetBackend()->CreateTextureFromMemory( FileImage, Size );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the texture '%s' from memory (%d byte(s)).", *Name, static_cast<int32>( Size ) );
		return nullptr;
	}
	TSharedPtr<CTexture> Texture = MakeShared<CTexture>( Name, Handle, false );
	Textures.Add( Key, Texture );
	return Texture;
}

//-----------------------------------------------------------------------------
// スプライトシート
//-----------------------------------------------------------------------------
void CAssetManager::AddPendingSheet( const TSharedPtr<CSpriteSheet>& Sheet )
{
	if ( !Sheet->IsReady() ) PendingSheets.Add( Sheet );
}

TSharedPtr<CSpriteSheet> CAssetManager::CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FSpriteSheetGrid& Grid, const FString& Name )
{
	TSharedPtr<CSpriteSheet> Sheet = CreateSpriteSheet( Texture, Name );
	if ( Sheet.IsValid() )
	{
		Sheet->BuildGrid( Grid );
		AddPendingSheet( Sheet );
	}
	return Sheet;
}

TSharedPtr<CSpriteSheet> CAssetManager::CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FString& Name )
{
	if ( !Texture.IsValid() )
	{
		LOG( LogAsset, Error, "Cannot create the sprite sheet '%s': no texture was given.", *Name );
		return nullptr;
	}
	return MakeShared<CSpriteSheet>( Name.IsEmpty() ? Texture->GetPath() : Name, Texture );
}

TSharedPtr<CSpriteSheet> CAssetManager::CreateSpriteSheet( const FSpriteSheetDefinition& Definition, const FString& Name, bool bAsync )
{
	TSharedPtr<CTexture> Texture = LoadTexture( Definition.TexturePath, bAsync );
	if ( !Texture.IsValid() ) return nullptr;   // 原因は LoadTexture がログに出している

	TSharedPtr<CSpriteSheet> Sheet = MakeShared<CSpriteSheet>( Name.IsEmpty() ? Definition.TexturePath : Name, Texture );
	Sheet->SetDefinition( Definition );
	AddPendingSheet( Sheet );
	return Sheet;
}

TOptional<FString> CAssetManager::PrepareSavePath( const FString& Path )
{
	FString Full = Path;
	const bool bAbsolute = Path.Find( ":" ) != INDEX_NONE || Path.StartsWith( "/" ) || Path.StartsWith( "\\" );
	if ( !bAbsolute ) Full = GetRootDirectory() / Path;
	Full = Full.Replace( "\\", "/" );

	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 && !NFileSystem::EnsureDirectory( Full.Left( Slash ) ) )
	{
		LOG( LogAsset, Error, "Cannot save '%s': the folder could not be created.", *Full );
		return TOptional<FString>();
	}
	return NFileSystem::GetFullPath( Full );
}

bool CAssetManager::SaveSpriteSheetDefinition( const FString& Path, const FSpriteSheetDefinition& Definition )
{
	const TOptional<FString> Full = PrepareSavePath( Path );
	if ( !Full.IsSet() ) return false;

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the sprite sheet definition '%s'.", *Full.Get( FString() ) );
		return false;
	}
	SpriteSheets.Remove( MakeKey( Full.Get( FString() ) ) );   // 古い内容のキャッシュは捨てる
	LOG( LogAsset, Log, "Saved the sprite sheet definition '%s' (%d grid(s), %d frame(s), %d clip(s))", *Full.Get( FString() ), Definition.Grids.Num(), Definition.GetNumFrames(),
		 Definition.Clips.Num() );
	return true;
}

TSharedPtr<CSpriteSheet> CAssetManager::LoadSpriteSheet( const FString& DefinitionPath, bool bAsync )
{
	const TOptional<FString> Full = PrepareLoad( "sprite sheet", DefinitionPath );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CSpriteSheet>* Found = SpriteSheets.Find( Key ) )
	{
		if ( TSharedPtr<CSpriteSheet> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadSpriteSheet", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	if ( !Json.LoadFromFile( FullPath ) )
	{
		LOG( LogAsset, Error, "Failed to read the sprite sheet definition '%s' (not valid JSON?).", *FullPath );
		RecordFailure();
		return nullptr;
	}

	FSpriteSheetDefinition Definition;
	if ( !FSpriteSheetDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		LOG( LogAsset, Error, "The sprite sheet definition '%s' is not a JSON object.", *FullPath );
		RecordFailure();
		return nullptr;
	}
	if ( Definition.TexturePath.IsEmpty() )
	{
		LOG( LogAsset, Error, "The sprite sheet definition '%s' has no \"Texture\".", *FullPath );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CTexture> Texture = LoadTexture( Definition.TexturePath, bAsync );
	if ( !Texture.IsValid() ) return nullptr;   // 原因は LoadTexture がログに出している

	TSharedPtr<CSpriteSheet> Sheet = MakeShared<CSpriteSheet>( FullPath, Texture );
	Sheet->SetDefinition( Definition );
	RecordLoad( *Sheet, FullPath, StartTick );
	AddPendingSheet( Sheet );
	SpriteSheets.Add( Key, Sheet );
	LOG( LogAsset, Verbose, "Loaded the sprite sheet '%s' (%d frame(s), %d clip(s))", *FullPath, Sheet->GetNumFrames(), Sheet->GetNumClips() );
	return Sheet;
}

//-----------------------------------------------------------------------------
// モデルのアニメーションの定義
//-----------------------------------------------------------------------------
TSharedPtr<CModelAnimationAsset> CAssetManager::CreateModelAnimations( const FModelAnimationDefinition& Definition, const FString& Name, bool bAsync )
{
	// Model が空なら、モデルを持たないアニメーション集（モーション専用）
	TSharedPtr<CModel> Model;
	if ( !Definition.ModelPath.IsEmpty() )
	{
		Model = LoadModel( Definition.ModelPath, bAsync );
		if ( !Model.IsValid() ) return nullptr;   // 原因は LoadModel がログに出している
		if ( Model->IsReady() && Model->GetMeshCount() == 0 )
		{
			LOG( LogAsset, Warning, "The model '%s' of the animation set has no mesh (it only holds animations), so nothing will be drawn. Use it as a clip's Source instead.", *Definition.ModelPath );
		}
	}

	TArray<TSharedPtr<CModel>> Sources;
	for ( const FModelClipDef& Clip : Definition.Clips )
	{
		TSharedPtr<CModel> Source = Clip.SourcePath.IsEmpty() ? Model : LoadModel( Clip.SourcePath, bAsync );
		if ( !Source.IsValid() ) Source = Model;   // 読めなかったクリップは、モデル自身のアニメーションとして扱う（原因はログに出ている）
		Sources.Add( Source );
	}
	const FString DefaultName = Definition.ModelPath.IsEmpty() ? FString( "(animation set)" ) : Definition.ModelPath;
	return MakeShared<CModelAnimationAsset>( Name.IsEmpty() ? DefaultName : Name, Definition, Model, Sources );
}

TSharedPtr<CModelAnimationAsset> CAssetManager::LoadModelAnimations( const FString& DefinitionPath, bool bAsync )
{
	const TOptional<FString> Full = PrepareLoad( "model animation set", DefinitionPath );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CModelAnimationAsset>* Found = ModelAnimations.Find( Key ) )
	{
		if ( TSharedPtr<CModelAnimationAsset> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadModelAnimations", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FModelAnimationDefinition Definition;
	if ( !Json.LoadFromFile( FullPath ) || !FModelAnimationDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		LOG( LogAsset, Error, "Failed to read the model animation definition '%s' (not valid JSON?).", *FullPath );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CModelAnimationAsset> Asset = CreateModelAnimations( Definition, FullPath, bAsync );
	if ( !Asset.IsValid() )
	{
		RecordFailure();
		return nullptr;
	}
	RecordLoad( *Asset, FullPath, StartTick );
	ModelAnimations.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the model animation definition '%s' (%d clip(s))", *FullPath, Definition.Clips.Num() );
	return Asset;
}

bool CAssetManager::SaveModelAnimationDefinition( const FString& Path, const FModelAnimationDefinition& Definition )
{
	const TOptional<FString> Full = PrepareSavePath( Path );
	if ( !Full.IsSet() ) return false;

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the model animation definition '%s'.", *Full.Get( FString() ) );
		return false;
	}
	ModelAnimations.Remove( MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the model animation definition '%s' (%d clip(s))", *Full.Get( FString() ), Definition.Clips.Num() );
	return true;
}

//-----------------------------------------------------------------------------
// モデル・サウンド
//-----------------------------------------------------------------------------
TSharedPtr<CModel> CAssetManager::LoadModel( const FString& Path, bool bAsync )
{
	FString FullPath;
	if ( !ResolveModelRequest( Path, FullPath ) )
	{
		RecordFailure();
		return nullptr;
	}
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CModel>* Found = Models.Find( Key ) )
	{
		if ( TSharedPtr<CModel> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadModel", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	FString LoadPath;
	if ( !ResolveModelLoadPath( FullPath, LoadPath ) )
	{
		RecordFailure();
		return nullptr;
	}
	const int32 Handle = GetBackend()->LoadModel( LoadPath, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the model '%s' (unsupported or broken file).", *LoadPath );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CModel> Model = MakeShared<CModel>( FullPath, Handle, bAsync );
	RecordLoad( *Model, LoadPath, StartTick );
	Models.Add( Key, Model );
	LOG( LogAsset, Verbose, "Loaded the model '%s'%s%s", *FullPath, LoadPath != FullPath ? *( " (from " + LoadPath + ")" ) : "", bAsync ? " (async)" : "" );
	return Model;
}

TSharedPtr<CSound> CAssetManager::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	const TOptional<FString> Full = PrepareLoad( "sound", Path );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	// 読み込み方が違えば別のアセット（同じファイルを、効果音としても BGM としても使えるように）
	const FString Key = MakeKey( FullPath ) + ( Mode == ESoundLoadMode::Stream ? "|stream" : "|memory" );
	if ( TWeakPtr<CSound>* Found = Sounds.Find( Key ) )
	{
		if ( TSharedPtr<CSound> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadSound", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = GetBackend()->LoadSound( FullPath, Mode, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the sound '%s' (unsupported or broken file).", *FullPath );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CSound> Sound = MakeShared<CSound>( FullPath, Handle, Mode, bAsync );
	RecordLoad( *Sound, FullPath, StartTick );
	Sounds.Add( Key, Sound );
	LOG( LogAsset, Verbose, "Loaded the sound '%s' (%s)%s", *FullPath, Mode == ESoundLoadMode::Stream ? "stream" : "memory", bAsync ? " async" : "" );
	return Sound;
}

//-----------------------------------------------------------------------------
// フォント
//-----------------------------------------------------------------------------
TSharedPtr<CFont> CAssetManager::LoadFontInternal( const FString& Key, const FFontDesc& Desc, const FString& FontFilePath )
{
	PROFILE_SCOPE_LAZY( LoadScope, MakeLoadScope( "LoadFont", Desc.Face.IsEmpty() ? FString( "(default)" ) : Desc.Face ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = GetBackend()->MakeFont( Desc );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the font '%s' (size %d).", Desc.Face.IsEmpty() ? "(default)" : *Desc.Face, Desc.Size );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CFont> Font = MakeShared<CFont>( Key, Handle, Desc, FontFilePath );
	RecordLoad( *Font, FontFilePath.IsEmpty() ? FString() : FontFilePath, StartTick );
	Fonts.Add( Key, Font );
	LOG( LogAsset, Verbose, "Created the font '%s' (size %d)", Desc.Face.IsEmpty() ? "(default)" : *Desc.Face, Desc.Size );
	return Font;
}

namespace
{
	FString FontSuffix( int32 Size, const FFontStyle& Style )
	{
		return FString::Printf( "|%d|%d|%d|%d|%d", Size, Style.Thickness, Style.EdgeSize, Style.bItalic ? 1 : 0, Style.bAntialias ? 1 : 0 );
	}

	FFontDesc MakeDesc( const FString& Face, int32 Size, const FFontStyle& Style )
	{
		FFontDesc Desc;
		Desc.Face = Face;
		Desc.Size = Size;
		Desc.Thickness = Style.Thickness;
		Desc.EdgeSize = Style.EdgeSize;
		Desc.bItalic = Style.bItalic;
		Desc.bAntialias = Style.bAntialias;
		return Desc;
	}
}

TSharedPtr<CFont> CAssetManager::LoadFont( const FString& FontFilePath, int32 Size, const FFontStyle& Style )
{
	const TOptional<FString> Full = PrepareLoad( "font", FontFilePath );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = "file:" + MakeKey( FullPath ) + FontSuffix( Size, Style );
	if ( TWeakPtr<CFont>* Found = Fonts.Find( Key ) )
	{
		if ( TSharedPtr<CFont> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
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

	TSharedPtr<CFont> Font = LoadFontInternal( Key, MakeDesc( Face, Size, Style ), FullPath );
	if ( !Font.IsValid() ) NAssetService::RemoveFontFile( FullPath );   // CFont に引き渡せなかった分の登録を戻す
	return Font;
}

TSharedPtr<CFont> CAssetManager::LoadSystemFont( const FString& FaceName, int32 Size, const FFontStyle& Style )
{
	if ( GetBackend() == nullptr )
	{
		LOG( LogAsset, Error, "Cannot create the font '%s': no asset backend.", *FaceName );
		return nullptr;
	}
	const FString Key = "face:" + FaceName.ToLower() + FontSuffix( Size, Style );
	if ( TWeakPtr<CFont>* Found = Fonts.Find( Key ) )
	{
		if ( TSharedPtr<CFont> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}
	return LoadFontInternal( Key, MakeDesc( FaceName, Size, Style ), FString() );
}

TSharedPtr<CFont> CAssetManager::GetDefaultFont( int32 Size, const FFontStyle& Style )
{
	return LoadSystemFont( FString(), Size, Style );
}

//-----------------------------------------------------------------------------
// 情報
//-----------------------------------------------------------------------------
namespace
{
	template<typename T>
	void CollectInfos( const TMap<FString, TWeakPtr<T>>& Cache, TArray<FAssetInfo>& Out )
	{
		for ( auto It = Cache.CreateConstIterator(); It; ++It )
		{
			const TSharedPtr<T> Asset = It.Value().Pin();
			if ( !Asset.IsValid() ) continue;

			FAssetInfo Info;
			Info.Type = Asset->GetAssetType();
			Info.Path = Asset->GetPath();
			Info.Description = Asset->Describe();
			Info.RefCount = Asset.GetSharedReferenceCount() - 1;   // 調べるために作った分を除く
			Info.bReady = Asset->IsReady();
			Info.MemoryBytes = Asset->GetMemoryBytes();
			Info.FileBytes = Asset->GetFileBytes();
			Info.LoadMs = Asset->GetLoadMs();
			Out.Add( Info );
		}
	}
}

TArray<FAssetInfo> CAssetManager::GetLoadedAssets() const
{
	TArray<FAssetInfo> Infos;
	CollectInfos( Textures, Infos );
	CollectInfos( SpriteSheets, Infos );
	CollectInfos( Models, Infos );
	CollectInfos( ModelAnimations, Infos );
	CollectInfos( Worlds, Infos );
	CollectInfos( Effects, Infos );
	CollectInfos( Sounds, Infos );
	CollectInfos( Fonts, Infos );
	Infos.Sort( []( const FAssetInfo& A, const FAssetInfo& B ) { return A.Type != B.Type ? A.Type < B.Type : A.Path < B.Path; } );
	return Infos;
}

int32 CAssetManager::GetNumLoading() const
{
	IAssetBackend* B = GetBackend();
	return B != nullptr ? B->GetNumLoading() : 0;
}

void CAssetManager::DumpToLog() const
{
	const auto Bytes = []( int64 Value ) { return NProfilerAnalyzer::FormatCounterValue( static_cast<float64>( Value ), EProfileCounterUnit::Bytes ); };

	LOG( LogAsset, Display, "Assets: %d loaded, ~%s in memory (peak ~%s), %s on disk. Loads: %d (%.1f ms total, slowest %.1f ms '%s'), %d shared, %d failed, %d released later from other threads, %d loading now.",
		 Stats.TotalCount, *Bytes( Stats.TotalMemoryBytes ), *Bytes( Stats.PeakMemoryBytes ), *Bytes( Stats.TotalFileBytes ), Stats.NumLoads, Stats.TotalLoadMs, Stats.MaxLoadMs,
		 *Stats.MaxLoadPath, Stats.NumCacheHits, Stats.NumLoadFailures, Stats.NumDeferredReleases, Stats.NumLoading );
	if ( Stats.Backend.VideoMemoryTotal > 0 )
	{
		LOG( LogAsset, Display, "  Video memory %s / %s, process memory %s; handles: %d graph, %d model, %d sound, %d font", *Bytes( Stats.Backend.VideoMemoryUsed ),
			 *Bytes( Stats.Backend.VideoMemoryTotal ), *Bytes( Stats.Backend.ProcessWorkingSet ), Stats.Backend.NumTextureHandles, Stats.Backend.NumModelHandles,
			 Stats.Backend.NumSoundHandles, Stats.Backend.NumFontHandles );
	}
	for ( int32 i = 0; i < NumAssetTypes; ++i )
	{
		const FAssetTypeStats& Type = Stats.Types[i];
		if ( Type.Count > 0 ) LOG( LogAsset, Display, "  %-12s %3d asset(s), ~%s", ToString( static_cast<EAssetType>( i ) ), Type.Count, *Bytes( Type.MemoryBytes ) );
	}
	for ( const FAssetInfo& Info : GetLoadedAssets() )
	{
		LOG( LogAsset, Display, "    [%s] %s  refs %d  ~%s  file %s  load %.2f ms  %s", ToString( Info.Type ), *Info.Path, Info.RefCount, *Bytes( Info.MemoryBytes ),
			 *Bytes( Info.FileBytes ), Info.LoadMs, *Info.Description );
	}
}
