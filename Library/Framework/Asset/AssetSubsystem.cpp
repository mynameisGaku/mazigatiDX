#include "AssetSubsystem.h"


#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Font/FontFile.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY( LogAsset )

REGISTER_SUBSYSTEM( CAssetSubsystem )

//=============================================================================
// CAssetSubsystem
//=============================================================================
CAssetSubsystem::CAssetSubsystem() = default;
CAssetSubsystem::~CAssetSubsystem() = default;

void CAssetSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Backend = Collection.InitializeDependency<IAssetBackend>();
	NAssetService::Attach( GetBackend() );
	LoadContext.SetBackend( GetBackend() );

	if ( Backend == nullptr )
	{
		LOG( LogAsset, Warning, "No asset backend is available. Assets cannot be loaded." );
		return;
	}
	LOG( LogAsset, Log, "Asset manager ready (root directory '%s')", *GetRootDirectory() );
}

void CAssetSubsystem::Deinitialize()
{
	NAssetService::FlushDeferred();
	TextureLoader.Clear();
	SpriteSheetLoader.Clear();
	ModelLoader.Clear();
	ModelAnimationLoader.Clear();
	WorldLoader.Clear();
	EffectLoader.Clear();
	BlackboardLoader.Clear();
	StateMachineLoader.Clear();
	BehaviorTreeLoader.Clear();
	SoundLoader.Clear();
	FontLoader.Clear();

	// これ以降に手放されるアセットは、何もしない（DxLib の終了処理が片付ける）
	NAssetService::SetBackend( nullptr );
	LoadContext.SetBackend( nullptr );
	Backend = nullptr;
}

IAssetBackend* CAssetSubsystem::GetBackend() const
{
	return BackendOverride != nullptr ? BackendOverride : Backend;
}

void CAssetSubsystem::SetBackendOverride( IAssetBackend* InBackend )
{
	NAssetService::FlushDeferred();
	BackendOverride = InBackend;
	NAssetService::SetBackend( GetBackend() );
	LoadContext.SetBackend( GetBackend() );
}

void CAssetSubsystem::Tick( float32 DeltaTime )
{
	PROFILE_SCOPE_CAT( "Asset", "Tick" );
	NAssetService::FlushDeferred();

	SpriteSheetLoader.Update();

	PruneTimer += DeltaTime;
	if ( PruneTimer >= 5.0f )
	{
		PruneTimer = 0.0f;
		ReleaseUnused();
	}

	// 集計: プロファイラーの記録中は毎フレーム（カウンターとして残す）、それ以外は 0.5 秒ごと（画面に出す用）。読み込みがあった直後は、早めに更新する
	if ( LoadRecorder.ConsumeLoadHappened() )
	{
		StatsTimer = 0.5f;
	}
	StatsTimer += DeltaTime;
	if ( NProfiler::GRecorder != nullptr || StatsTimer >= 0.5f )
	{
		StatsTimer = 0.0f;
		UpdateStats();
		RecordCounters();
	}
}

const FAssetStats& CAssetSubsystem::GetStats() const
{
	// 読み込みの累計（回数・共有・失敗・時間）は、読み込みを記録する人が数えている。読むたびに、最新の値を写す
	const FAssetLoadCounters& Counters = LoadRecorder.GetCounters();
	Stats.NumLoads = Counters.NumLoads;
	Stats.NumCacheHits = Counters.NumCacheHits;
	Stats.NumLoadFailures = Counters.NumLoadFailures;
	Stats.TotalLoadMs = Counters.TotalLoadMs;
	Stats.MaxLoadMs = Counters.MaxLoadMs;
	Stats.MaxLoadPath = Counters.MaxLoadPath;
	return Stats;
}

void CAssetSubsystem::UpdateStats()
{
	FAssetStats Fresh = GetStats();   // 起動してからの累計は引き継ぐ
	for ( FAssetTypeStats& Type : Fresh.Types )
	{
		Type = FAssetTypeStats();
	}
	Fresh.TotalCount = 0;
	Fresh.TotalMemoryBytes = 0;
	Fresh.TotalFileBytes = 0;
	Fresh.NumDeferredReleases = NAssetService::GetNumDeferredReleased();

	for ( const FAssetInfo& Info : GetLoadedAssets() )
	{
		FAssetTypeStats& Type = Fresh.Types[static_cast<int32>( Info.Type )];
		++Type.Count;
		if ( !Info.bReady )
		{
			++Type.NumNotReady;
		}
		Type.MemoryBytes += Info.MemoryBytes;
		Type.FileBytes += Info.FileBytes;
		Type.LoadMs += Info.LoadMs;
		++Fresh.TotalCount;
		Fresh.TotalMemoryBytes += Info.MemoryBytes;
		Fresh.TotalFileBytes += Info.FileBytes;
	}
	if ( Fresh.TotalMemoryBytes > Fresh.PeakMemoryBytes )
	{
		Fresh.PeakMemoryBytes = Fresh.TotalMemoryBytes;
	}

	if ( IAssetBackend* B = GetBackend() )
	{
		Fresh.NumLoading = B->GetNumLoading();
		Fresh.Backend = B->GetMemoryInfo();
	}
	Stats = Fresh;
}

void CAssetSubsystem::RecordCounters() const
{
	IProfiler* Recorder = NProfiler::GRecorder;
	if ( Recorder == nullptr )
	{
		return;
	}

	static const char* const CountNames[NumAssetTypes] = { "Asset.Count.Texture", "Asset.Count.SpriteSheet", "Asset.Count.Model", "Asset.Count.Sound", "Asset.Count.Font",
														   "Asset.Count.ModelAnimation", "Asset.Count.World", "Asset.Count.Effect", "Asset.Count.Blackboard", "Asset.Count.StateMachine", "Asset.Count.BehaviorTree" };
	static const char* const MemoryNames[NumAssetTypes] = { "Asset.Memory.Texture", "Asset.Memory.SpriteSheet", "Asset.Memory.Model", "Asset.Memory.Sound", "Asset.Memory.Font",
															"Asset.Memory.ModelAnimation", "Asset.Memory.World", "Asset.Memory.Effect", "Asset.Memory.Blackboard", "Asset.Memory.StateMachine", "Asset.Memory.BehaviorTree" };
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
	if ( Stats.Backend.ProcessWorkingSet > 0 )
	{
		PROFILE_COUNTER_BYTES( "Memory.WorkingSet", Stats.Backend.ProcessWorkingSet );
	}
	PROFILE_COUNTER( "Handle.Textures", Stats.Backend.NumTextureHandles );
	PROFILE_COUNTER( "Handle.Models", Stats.Backend.NumModelHandles );
	PROFILE_COUNTER( "Handle.Sounds", Stats.Backend.NumSoundHandles );
	PROFILE_COUNTER( "Handle.Fonts", Stats.Backend.NumFontHandles );
}

//-----------------------------------------------------------------------------
// パス（パスを解決する人 CAssetPathResolver に任せる）
//-----------------------------------------------------------------------------
void CAssetSubsystem::SetRootDirectory( const FString& Directory )
{
	PathResolver.SetRootDirectory( Directory );
}

FString CAssetSubsystem::GetRootDirectory() const
{
	return PathResolver.GetRootDirectory();
}

TOptional<FString> CAssetSubsystem::ResolvePath( const FString& Path ) const
{
	return PathResolver.ResolvePath( Path );
}

void CAssetSubsystem::ReleaseUnused()
{
	TextureLoader.Prune();
	SpriteSheetLoader.Prune();
	ModelLoader.Prune();
	ModelAnimationLoader.Prune();
	WorldLoader.Prune();
	EffectLoader.Prune();
	BlackboardLoader.Prune();
	StateMachineLoader.Prune();
	BehaviorTreeLoader.Prune();
	SoundLoader.Prune();
	FontLoader.Prune();
}

//-----------------------------------------------------------------------------
// 画像（画像を読み込む・作る人 CTextureLoader、CPU 側の画像を読み書きする人 CPixelImageCodec に任せる）
//-----------------------------------------------------------------------------
TSharedPtr<CTexture> CAssetSubsystem::LoadTexture( const FString& Path, bool bAsync )
{
	return TextureLoader.Load( Path, bAsync );
}

TSharedPtr<CTexture> CAssetSubsystem::CreateTextureRegion( const TSharedPtr<CTexture>& Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	return TextureLoader.CreateRegion( Source, X, Y, Width, Height );
}

TSharedPtr<CTexture> CAssetSubsystem::CreateTextureFromMemory( const FString& Name, const void* FileImage, usize Size )
{
	return TextureLoader.CreateFromMemory( Name, FileImage, Size );
}

TSharedPtr<CRenderTarget> CAssetSubsystem::CreateRenderTarget( const FString& Name, const FRenderTargetDesc& Desc )
{
	return TextureLoader.CreateRenderTarget( Name, Desc );
}

TSharedPtr<CDynamicTexture> CAssetSubsystem::CreateDynamicTexture( const FString& Name, const FPixelImage& Image )
{
	return TextureLoader.CreateDynamic( Name, Image );
}

FPixelImage CAssetSubsystem::LoadPixelImage( const FString& Path )
{
	return PixelImageCodec.Load( Path );
}

FPixelImage CAssetSubsystem::DecodePixelImage( const void* FileImage, usize Size )
{
	return PixelImageCodec.Decode( FileImage, Size );
}

bool CAssetSubsystem::SavePixelImage( const FPixelImage& Image, const FString& Path, const FImageSaveOptions& Options )
{
	return PixelImageCodec.Save( Image, Path, Options );
}

TArray<TSharedPtr<CTexture>> CAssetSubsystem::SplitTexture( const TSharedPtr<CTexture>& Source, const FSpriteSheetGrid& Grid )
{
	return TextureLoader.Split( Source, Grid );
}

TArray<TSharedPtr<CTexture>> CAssetSubsystem::LoadTextureGrid( const FString& Path, const FSpriteSheetGrid& Grid )
{
	GAME_THREAD_ONLY( TArray<TSharedPtr<CTexture>>() );
	return TextureLoader.Split( TextureLoader.Load( Path, false ), Grid );
}

//-----------------------------------------------------------------------------
// スプライトシート・モデル・サウンド・フォント（それぞれのローダーに任せる）
//-----------------------------------------------------------------------------
TSharedPtr<CSpriteSheet> CAssetSubsystem::CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FSpriteSheetGrid& Grid, const FString& Name )
{
	return SpriteSheetLoader.Create( Texture, Grid, Name );
}

TSharedPtr<CSpriteSheet> CAssetSubsystem::CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FString& Name )
{
	return SpriteSheetLoader.Create( Texture, Name );
}

TSharedPtr<CSpriteSheet> CAssetSubsystem::CreateSpriteSheet( const FSpriteSheetDefinition& Definition, const FString& Name, bool bAsync )
{
	return SpriteSheetLoader.Create( Definition, Name, bAsync );
}

bool CAssetSubsystem::SaveSpriteSheetDefinition( const FString& Path, const FSpriteSheetDefinition& Definition )
{
	return SpriteSheetLoader.Save( Path, Definition );
}

TSharedPtr<CSpriteSheet> CAssetSubsystem::LoadSpriteSheet( const FString& DefinitionPath, bool bAsync )
{
	return SpriteSheetLoader.Load( DefinitionPath, bAsync );
}

TSharedPtr<CModelAnimationAsset> CAssetSubsystem::CreateModelAnimations( const FModelAnimationDefinition& Definition, const FString& Name, bool bAsync )
{
	return ModelAnimationLoader.Create( Definition, Name, bAsync );
}

TSharedPtr<CModelAnimationAsset> CAssetSubsystem::LoadModelAnimations( const FString& DefinitionPath, bool bAsync )
{
	return ModelAnimationLoader.Load( DefinitionPath, bAsync );
}

bool CAssetSubsystem::SaveModelAnimationDefinition( const FString& Path, const FModelAnimationDefinition& Definition )
{
	return ModelAnimationLoader.Save( Path, Definition );
}

TSharedPtr<CModel> CAssetSubsystem::LoadModel( const FString& Path, bool bAsync )
{
	return ModelLoader.Load( Path, bAsync );
}

TSharedPtr<CSound> CAssetSubsystem::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	return SoundLoader.Load( Path, Mode, bAsync );
}

TSharedPtr<CFont> CAssetSubsystem::LoadFont( const FString& FontFilePath, int32 Size, const FFontStyle& Style )
{
	return FontLoader.LoadFile( FontFilePath, Size, Style );
}

TSharedPtr<CFont> CAssetSubsystem::LoadSystemFont( const FString& FaceName, int32 Size, const FFontStyle& Style )
{
	return FontLoader.LoadSystem( FaceName, Size, Style );
}

TSharedPtr<CFont> CAssetSubsystem::GetDefaultFont( int32 Size, const FFontStyle& Style )
{
	return FontLoader.LoadSystem( FString(), Size, Style );
}

bool CAssetSubsystem::IsFontAvailable( const FString& FaceName ) const
{
	return FontLoader.IsAvailable( FaceName );
}

TArray<FString> CAssetSubsystem::GetSystemFontNames() const
{
	return FontLoader.GetSystemFontNames();
}

//-----------------------------------------------------------------------------
// 情報
//-----------------------------------------------------------------------------
TArray<FAssetInfo> CAssetSubsystem::GetLoadedAssets() const
{
	TArray<FAssetInfo> Infos;
	TextureLoader.CollectInfos( Infos );
	SpriteSheetLoader.CollectInfos( Infos );
	ModelLoader.CollectInfos( Infos );
	ModelAnimationLoader.CollectInfos( Infos );
	WorldLoader.CollectInfos( Infos );
	EffectLoader.CollectInfos( Infos );
	BlackboardLoader.CollectInfos( Infos );
	StateMachineLoader.CollectInfos( Infos );
	BehaviorTreeLoader.CollectInfos( Infos );
	SoundLoader.CollectInfos( Infos );
	FontLoader.CollectInfos( Infos );
	Infos.Sort( []( const FAssetInfo& A, const FAssetInfo& B ) { return A.Type != B.Type ? A.Type < B.Type : A.Path < B.Path; } );
	return Infos;
}

int32 CAssetSubsystem::GetNumLoading() const
{
	IAssetBackend* B = GetBackend();
	return B != nullptr ? B->GetNumLoading() : 0;
}

void CAssetSubsystem::DumpToLog() const
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
		if ( Type.Count > 0 )
		{
			LOG( LogAsset, Display, "  %-12s %3d asset(s), ~%s", ToString( static_cast<EAssetType>( i ) ), Type.Count, *Bytes( Type.MemoryBytes ) );
		}
	}
	for ( const FAssetInfo& Info : GetLoadedAssets() )
	{
		LOG( LogAsset, Display, "    [%s] %s  refs %d  ~%s  file %s  load %.2f ms  %s", ToString( Info.Type ), *Info.Path, Info.RefCount, *Bytes( Info.MemoryBytes ),
			 *Bytes( Info.FileBytes ), Info.LoadMs, *Info.Description );
	}
}

//-----------------------------------------------------------------------------
// ワールド・エフェクト・モデルの取り込み（それぞれのローダー / CModelImporter に任せる）
//-----------------------------------------------------------------------------
TSharedPtr<CWorldAsset> CAssetSubsystem::LoadWorld( const FString& Path )
{
	return WorldLoader.Load( Path );
}

bool CAssetSubsystem::SaveWorld( const FString& Path, const FWorldDefinition& Definition )
{
	return WorldLoader.Save( Path, Definition );
}

TSharedPtr<CEffectAsset> CAssetSubsystem::LoadEffect( const FString& Path )
{
	return EffectLoader.Load( Path );
}

bool CAssetSubsystem::SaveEffect( const FString& Path, const FParticleEffectDefinition& Definition )
{
	return EffectLoader.Save( Path, Definition );
}

TSharedPtr<CBlackboardAsset> CAssetSubsystem::LoadBlackboard( const FString& Path )
{
	return BlackboardLoader.Load( Path );
}

bool CAssetSubsystem::SaveBlackboard( const FString& Path, const FBlackboardDefinition& Definition )
{
	return BlackboardLoader.Save( Path, Definition );
}

TSharedPtr<CStateMachineAsset> CAssetSubsystem::LoadStateMachine( const FString& Path )
{
	return StateMachineLoader.Load( Path );
}

bool CAssetSubsystem::SaveStateMachine( const FString& Path, const FStateMachineDefinition& Definition )
{
	return StateMachineLoader.Save( Path, Definition );
}

TSharedPtr<CBehaviorTreeAsset> CAssetSubsystem::LoadBehaviorTree( const FString& Path )
{
	return BehaviorTreeLoader.Load( Path );
}

bool CAssetSubsystem::SaveBehaviorTree( const FString& Path, const FBehaviorTreeDefinition& Definition )
{
	return BehaviorTreeLoader.Save( Path, Definition );
}

FModelExportResult CAssetSubsystem::ExportModelAsMV1( const FString& SourcePath, const FModelExportOptions& Options )
{
	return ModelImporter.ExportAsMV1( SourcePath, Options );
}

TArray<FModelSourceInfo> CAssetSubsystem::ScanModelSources() const
{
	return ModelImporter.ScanSources();
}

FModelBatchExportResult CAssetSubsystem::ExportAllModels( bool bForce, const FModelExportOptions& Options )
{
	return ModelImporter.ExportAll( bForce, Options );
}

FModelBatchExportResult CAssetSubsystem::ExportModelsUsedBy( const FModelAnimationDefinition& Definition, bool bForce )
{
	return ModelImporter.ExportUsedBy( Definition, bForce );
}
