#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Asset/Import/ModelExporter.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Asset/ModelAnimationAsset.h"
#include "Library/Framework/Asset/Sound.h"
#include "Library/Framework/Asset/SpriteSheet.h"
#include "Library/Framework/Asset/EffectAsset.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

DECLARE_LOG_CATEGORY_EXTERN( LogAsset, Log )

// フォントの見た目の指定（大きさ以外）
struct FFontStyle
{
	int32 Thickness = 6;       // 太さ（0..9）
	int32 EdgeSize = 0;        // 0 より大きいと縁取り（ピクセル）
	bool bItalic = false;
	bool bAntialias = true;
};

// 読み込んでいるアセット 1 つの情報（デバッグ表示用）
struct FAssetInfo
{
	EAssetType Type = EAssetType::Texture;
	FString Path;
	FString Description;
	int32 RefCount = 0;   // 使っている TSharedPtr の数（マネージャー自身は数えない）
	bool bReady = true;
	int64 MemoryBytes = 0;   // 使っているメモリの推定
	int64 FileBytes = 0;     // ファイルの大きさ
	float32 LoadMs = 0.0f;   // 読み込みにかかった時間
};

// アセットの種類ごとの集計
struct FAssetTypeStats
{
	int32 Count = 0;
	int32 NumNotReady = 0;      // 非同期の読み込み中
	int64 MemoryBytes = 0;      // 推定
	int64 FileBytes = 0;
	float64 LoadMs = 0.0;       // 読み込みにかかった時間の合計
};

// アセット全体の集計（CAssetManager::GetStats）。メモリは推定（GetMemoryBytes）。Backend は実際の値
struct FAssetStats
{
	FAssetTypeStats Types[NumAssetTypes];   // EAssetType の順
	int32 TotalCount = 0;
	int64 TotalMemoryBytes = 0;
	int64 TotalFileBytes = 0;
	int64 PeakMemoryBytes = 0;  // 起動してからの最大

	// 起動してからの累計
	int32 NumLoads = 0;            // 実際に読み込んだ回数
	int32 NumCacheHits = 0;        // 読み込み済みのものを共有した回数
	int32 NumLoadFailures = 0;     // 失敗した回数
	float64 TotalLoadMs = 0.0;     // 読み込みにかかった時間の合計
	float32 MaxLoadMs = 0.0f;      // 1 回で一番長かった時間
	FString MaxLoadPath;           // その読み込みのパス
	int32 NumDeferredReleases = 0; // 別スレッドで手放されて、あとでまとめて解放した数
	int32 NumLoading = 0;          // 非同期で読み込み中の数（いま）

	FBackendMemoryInfo Backend;    // ビデオメモリ・プロセスのメモリ・ハンドルの数（実測）

	FORCEINLINE const FAssetTypeStats& Get( EAssetType Type ) const { return Types[static_cast<int32>( Type )]; }
};

//=============================================================================
// FAssetBundle
//   アセットをまとめて持っておく入れ物（シーンの OnEnter で読み込んで持ち、シーンの終わりで手放す、など）。
//   持っている間は、使っていなくても解放されない。
//
//   FAssetBundle Bundle;
//   Bundle.Add( Assets->LoadTexture( "Textures/Hero.png" ) );
//   Bundle.Add( Assets->LoadSound( "Sound/Jump.wav" ) );
//   ...
//   Bundle.Clear();     // 他で使われていなければ、ここで解放される
//=============================================================================
class FAssetBundle
{
public:
	template<typename T>
	const TSharedPtr<T>& Add( const TSharedPtr<T>& Asset )
	{
		if ( Asset.IsValid() ) Assets.Add( TSharedPtr<CAsset>( Asset ) );
		return Asset;
	}

	void Clear() { Assets.Empty(); }
	int32 Num() const { return Assets.Num(); }
	// すべて使える状態になったか（非同期の読み込みの完了待ちに）
	bool AreAllReady() const;

private:
	TArray<TSharedPtr<CAsset>> Assets;
};

//=============================================================================
// CAssetManager
//   アセット（画像・スプライトシート・3D モデル・サウンド・フォント）の読み込みと管理を行う（アプリで 1 つ、自動で生成される）。
//
//   CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
//   TSharedPtr<CTexture>     Hero  = Assets->LoadTexture( "Textures/Hero.png" );
//   TSharedPtr<CSpriteSheet> Sheet = Assets->LoadSpriteSheet( "Sprites/Hero.sheet.json" );
//   TSharedPtr<CModel>       Robot = Assets->LoadModel( "Models/Robot.fbx" );   // .fbx / .obj は、同じ名前の .mv1 を（無ければ作って）読む
//   TSharedPtr<CSound>       Jump  = Assets->LoadSound( "Sound/Jump.wav" );
//   TSharedPtr<CSound>       Theme = Assets->LoadSound( "Sound/Theme.ogg", ESoundLoadMode::Stream );
//   TSharedPtr<CFont>        Title = Assets->LoadFont( "Fonts/NotoSansJP-Bold.ttf", 48 );
//
//   - パスは、ルートフォルダ（既定は "Assets"。exe やプロジェクトの近くから自動で探す）からの相対パス。
//     絶対パスも使える。ルートに無ければ、作業ディレクトリ・exe のフォルダ（と、その親）も探す
//   - 同じファイルを何度読み込んでも 1 つを共有する（2 回目からは読み込みなし）。使う人が全員手放すと自動で解放される
//   - 失敗（ファイルが無い・読めない）したら、原因をログ（Error）に出して nullptr を返す。アニメーターやオーディオは
//     nullptr を渡されても落ちず、何もしない
//   - 読み込み・解放は、メインスレッドで行う（呼び出しも、メインスレッドから）
//   - 非同期: LoadTexture( Path, true ) などは、読み込みの完了を待たずに返す。IsReady() が true になるまで使わない
//
//   フォルダ構成の例:
//     Assets/Textures/  Assets/Sprites/  Assets/Models/  Assets/Sound/  Assets/Fonts/
//=============================================================================
class CAssetManager : public CAppSubsystem, public ITickable
{
public:
	CAssetManager();
	~CAssetManager() override;

	//-------------------------------------------------------------------------
	// CAppSubsystem / ITickable
	//-------------------------------------------------------------------------
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	// 別スレッドで手放されたアセットの解放と、非同期で読み込んだスプライトシートの準備を、メインスレッドで行う
	void Tick( float32 DeltaTime ) override;

	//-------------------------------------------------------------------------
	// パス
	//-------------------------------------------------------------------------
	// アセットのルートフォルダ。相対パスなら、作業ディレクトリ・exe のフォルダ・その親から探す
	void SetRootDirectory( const FString& Directory );
	FString GetRootDirectory() const;
	// アセットのパスを、実在するファイルの絶対パスにする。見つからなければ空
	TOptional<FString> ResolvePath( const FString& Path ) const;

	//-------------------------------------------------------------------------
	// 画像
	//-------------------------------------------------------------------------
	TSharedPtr<CTexture> LoadTexture( const FString& Path, bool bAsync = false );
	// 元画像の矩形を親と共有する派生テクスチャを返す。同じ親と矩形は再利用する。
	TSharedPtr<CTexture> CreateTextureRegion( const TSharedPtr<CTexture>& Source, int32 X, int32 Y, int32 Width, int32 Height );
	// ファイルの中身（BMP / PNG など）から作る。Name が同じなら共有する
	TSharedPtr<CTexture> CreateTextureFromMemory( const FString& Name, const void* FileImage, usize Size );

	//-------------------------------------------------------------------------
	// スプライトシート
	//-------------------------------------------------------------------------
	// 定義ファイル（JSON。書式は SpriteSheet.cpp の先頭を参照）から。画像も一緒に読み込む
	TSharedPtr<CSpriteSheet> LoadSpriteSheet( const FString& DefinitionPath, bool bAsync = false );
	// 画像から作る（コマ割りは Grid。クリップは AddClip で足す）。共有されず、呼ぶたびに新しいものが作られる
	TSharedPtr<CSpriteSheet> CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FSpriteSheetGrid& Grid, const FString& Name = FString() );
	// コマを自分で足す（AddFrame）ための、空のスプライトシート
	TSharedPtr<CSpriteSheet> CreateSpriteSheet( const TSharedPtr<CTexture>& Texture, const FString& Name = FString() );
	// 定義（FSpriteSheetDefinition）から作る。画像は Definition.TexturePath から読み込む。共有されず、呼ぶたびに新しいものが作られる（エディタのプレビュー用）
	TSharedPtr<CSpriteSheet> CreateSpriteSheet( const FSpriteSheetDefinition& Definition, const FString& Name = FString(), bool bAsync = false );
	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の LoadSpriteSheet は新しい内容を読む
	bool SaveSpriteSheetDefinition( const FString& Path, const FSpriteSheetDefinition& Definition );

	//-------------------------------------------------------------------------
	// 3D モデル（アニメーションだけのファイルも同じ）
	//-------------------------------------------------------------------------
	// .mv1 / .x / .pmd / .pmx / .mqo など DxLib が読める形式と、.fbx / .obj（このフレームワークが .mv1 に変換して読む）。
	//   ・.fbx / .obj: 同じ名前の .mv1 が最新ならそれを読む。無い・古ければ、自動で .mv1 を作って（元と同じフォルダ）読む（SetAutoImportModels）。
	//     元のファイルが無くて .mv1 だけがある（配布版）ときも、.mv1 を読む
	//   ・DxLib が読める形式: 最新の .mv1 が隣にあれば、そちらを読む（読み込みが速い。SetPreferConvertedModels）
	//   ・メッシュの無い、アニメーションだけのファイルも読める（CModel::IsAnimationOnly）。画面には出せず、別のモデルのアニメーションの元になる
	TSharedPtr<CModel> LoadModel( const FString& Path, bool bAsync = false );

	//-------------------------------------------------------------------------
	// モデルのアニメーションの定義（独自のデータアセット。書式は ModelAnimationAsset.h を参照）
	//-------------------------------------------------------------------------
	// 定義ファイル（JSON）から。モデルと、クリップごとのアニメーションのファイルも一緒に読み込む
	TSharedPtr<CModelAnimationAsset> LoadModelAnimations( const FString& DefinitionPath, bool bAsync = false );
	// 定義（FModelAnimationDefinition）から作る。共有されず、呼ぶたびに新しいものが作られる（エディタのプレビュー用）
	TSharedPtr<CModelAnimationAsset> CreateModelAnimations( const FModelAnimationDefinition& Definition, const FString& Name = FString(), bool bAsync = false );
	bool SaveModelAnimationDefinition( const FString& Path, const FModelAnimationDefinition& Definition );

	//-------------------------------------------------------------------------
	// ワールド（.world。置き物の配置の定義。書式は WorldDefinition.cpp の先頭、使い方は WorldSpawner.h を参照）
	//-------------------------------------------------------------------------
	// 定義ファイル（JSON）から。中のアセット（モデル・画像・音）は、シーンに置くとき（NWorld::Spawn）に読み込まれる
	TSharedPtr<CWorldAsset> LoadWorld( const FString& Path );
	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の LoadWorld は新しい内容を読む
	bool SaveWorld( const FString& Path, const FWorldDefinition& Definition );

	//-------------------------------------------------------------------------
	// パーティクルのエフェクト（.effect.json。書式は ParticleEffect.cpp の先頭、再生は EffectBackend.h を参照）
	//-------------------------------------------------------------------------
	// 定義ファイル（JSON）から。中の画像は、再生を始めるとき（ParticleEffectBackend）に読み込まれる
	TSharedPtr<CEffectAsset> LoadEffect( const FString& Path );
	// 定義を JSON ファイルに保存する（相対パスはルートフォルダの下。フォルダは作る）。そのパスのキャッシュは捨てるので、次の LoadEffect は新しい内容を読む
	bool SaveEffect( const FString& Path, const FParticleEffectDefinition& Definition );

	//-------------------------------------------------------------------------
	// 取り込みと .mv1 への出力（FBX / OBJ / .x / .pmx ... -> .mv1）
	//-------------------------------------------------------------------------
	// 元のファイルを .mv1 にして保存する（既定は同じフォルダ・同じ名前）。出力が新しければ何もしない（Options.bForce で作り直す）。
	// メッシュの無いファイル（モーションだけ）は、アニメーションだけの .mv1 になる。失敗の理由は結果とログに出る
	FModelExportResult ExportModelAsMV1( const FString& SourcePath, const FModelExportOptions& Options = FModelExportOptions() );
	// ルートフォルダの下の、.mv1 にできるファイルと、出力が最新かどうかの一覧
	TArray<FModelSourceInfo> ScanModelSources() const;
	// 一覧のうち、.mv1 が無い・古いもの（bForce なら全部）を .mv1 にする
	FModelBatchExportResult ExportAllModels( bool bForce = false, const FModelExportOptions& Options = FModelExportOptions() );
	// 定義が使っているモデルのファイル（Model / PreviewModel / 各クリップの Source）のうち、.mv1 にできるものを .mv1 にする。
	// エディタの保存のときに、一緒に呼ぶ（アセット出力）
	FModelBatchExportResult ExportModelsUsedBy( const FModelAnimationDefinition& Definition, bool bForce = false );
	// LoadModel で、.fbx / .obj の .mv1 が無い・古いときに、自動で作る（既定 true）。false なら、あるものを（古くても警告つきで）読み、無ければ失敗
	FORCEINLINE void SetAutoImportModels( bool bEnable ) { bAutoImportModels = bEnable; }
	FORCEINLINE bool GetAutoImportModels() const { return bAutoImportModels; }
	// LoadModel で、DxLib が読める形式（.x / .pmx など）にも、最新の .mv1 が隣にあれば、そちらを読む（既定 true）
	FORCEINLINE void SetPreferConvertedModels( bool bEnable ) { bPreferConvertedModels = bEnable; }
	FORCEINLINE bool GetPreferConvertedModels() const { return bPreferConvertedModels; }

	//-------------------------------------------------------------------------
	// サウンド
	//-------------------------------------------------------------------------
	TSharedPtr<CSound> LoadSound( const FString& Path, ESoundLoadMode Mode = ESoundLoadMode::Memory, bool bAsync = false );

	//-------------------------------------------------------------------------
	// フォント（大きさ・スタイルごとに別のフォントになる。同じ指定なら共有）
	//-------------------------------------------------------------------------
	// フォントファイル（.ttf / .otf）から。ファイルは、このアプリの間だけ使えるフォントとして登録される
	TSharedPtr<CFont> LoadFont( const FString& FontFilePath, int32 Size, const FFontStyle& Style = FFontStyle() );
	// 入っているフォント名（"Meiryo" / "MS Gothic" など）から
	TSharedPtr<CFont> LoadSystemFont( const FString& FaceName, int32 Size, const FFontStyle& Style = FFontStyle() );
	// 既定のフォント
	TSharedPtr<CFont> GetDefaultFont( int32 Size = 16, const FFontStyle& Style = FFontStyle() );

	//-------------------------------------------------------------------------
	// 情報・管理
	//-------------------------------------------------------------------------
	// 読み込んでいる（まだ誰かが使っている）アセット
	TArray<FAssetInfo> GetLoadedAssets() const;
	// 集計（種類ごとの個数・メモリ・読み込み時間、全体の累計、ビデオメモリなど）。UpdateStats の時点のもの。
	// Tick が、プロファイラーの記録中は毎フレーム、それ以外は 0.5 秒ごとに UpdateStats する
	FORCEINLINE const FAssetStats& GetStats() const { return Stats; }
	void UpdateStats();
	// 読み込んでいるアセットの一覧と集計を、ログ（Display）に出す
	void DumpToLog() const;
	// 読み込み中のアセットの数（非同期）
	int32 GetNumLoading() const;
	// 誰も使っていないアセットの記録を片付ける（解放自体は、最後の参照が消えた時点で済んでいる）
	void ReleaseUnused();

	// 確認用: バックエンドを差し替える（nullptr で元に戻す）
	void SetBackendOverride( IAssetBackend* InBackend );
	IAssetBackend* GetBackend() const;

private:
	template<typename T>
	using TCache = TMap<FString, TWeakPtr<T>>;

	// 読み込む前の共通の確認（バックエンドがあるか・ファイルがあるか）。通れば実在する絶対パスを返す
	TOptional<FString> PrepareLoad( const char* Kind, const FString& Path );
	static FString MakeKey( const FString& FullPath );
	TSharedPtr<CFont> LoadFontInternal( const FString& Key, const FFontDesc& Desc, const FString& FontFilePath );
	void AddPendingSheet( const TSharedPtr<CSpriteSheet>& Sheet );
	// 保存先の絶対パス（相対ならルートフォルダの下）。フォルダも作る。失敗したら空
	TOptional<FString> PrepareSavePath( const FString& Path );
	// LoadModel の下ごしらえ。ユーザーが指したファイルの絶対パス（キャッシュのキー）と、DxLib に実際に読ませるファイルを決める。失敗したら理由をログに出して false
	bool ResolveModelRequest( const FString& Path, FString& OutFullPath ) const;
	bool ResolveModelLoadPath( const FString& FullPath, FString& OutLoadPath );
	// 読み込みの結果を記録する（読み込みにかかった時間・ファイルの大きさ・累計）
	void RecordLoad( CAsset& Asset, const FString& FullPath, int64 StartTick );
	void RecordCacheHit() { ++Stats.NumCacheHits; }
	void RecordFailure() { ++Stats.NumLoadFailures; }
	// プロファイラーが記録中なら、集計をカウンターとして記録する
	void RecordCounters() const;

	template<typename T>
	static void PruneCache( TCache<T>& Cache );

	IAssetBackend* Backend = nullptr;
	IAssetBackend* BackendOverride = nullptr;
	bool bAutoImportModels = true;
	bool bPreferConvertedModels = true;
	FString RootDirectoryName = "Assets";
	mutable TOptional<FString> ResolvedRoot;

	TCache<CTexture> Textures;
	TCache<CSpriteSheet> SpriteSheets;
	TCache<CModel> Models;
	TCache<CModelAnimationAsset> ModelAnimations;
	TCache<CWorldAsset> Worlds;
	TCache<CEffectAsset> Effects;
	TCache<CSound> Sounds;
	TCache<CFont> Fonts;
	TArray<TWeakPtr<CSpriteSheet>> PendingSheets;
	float32 PruneTimer = 0.0f;
	float32 StatsTimer = 0.5f;
	FAssetStats Stats;
};
