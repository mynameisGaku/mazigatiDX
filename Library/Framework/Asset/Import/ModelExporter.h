#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"

//=============================================================================
// モデルの .mv1 への変換（出力）
//
//   NModelExport::Export( Backend, "C:/Assets/Models/Robot.fbx", Options ) -> C:/Assets/Models/Robot.mv1
//
//   元が .fbx / .obj（取り込み）:  読む -> 左手系にする -> 一時的な .x に書く -> DxLib で読む -> .mv1 として保存 -> 一時ファイルを消す
//   元が .x / .pmd / .pmx / .mqo / .vmd（DxLib 対応）:  DxLib で読む -> .mv1 として保存
//
//   ・メッシュのないファイル（モーションだけ）は、アニメーションだけの .mv1 にする（SaveType = Auto）
//   ・FBX に埋め込まれた画像は、出力の隣の「<名前>.textures/」フォルダに取り出す
//   ・出力のほうが新しいときは、何もしない（bForce で作り直す）
//   ・DxLib を使うので、メインスレッドから呼ぶ。普通は CAssetManager::ExportModelAsMV1 を使う
//=============================================================================
struct FModelExportOptions
{
	// 出力先（絶対パス）。空なら、元と同じフォルダに、拡張子だけ .mv1
	FString OutputPath;
	// true: メッシュがなければアニメーション専用、あれば通常。false: SaveType の指定どおり
	bool bAutoSaveType = true;
	EModelSaveType SaveType = EModelSaveType::Normal;
	// 小さく保存する（法線 8bit・座標 16bit・アニメーション 16bit）。false なら精度を落とさない
	bool bCompress = true;
	// 出力が新しくても作り直す
	bool bForce = false;
	// FBX / OBJ のアニメーションを、毎秒何コマでサンプリングするか（DxLib での時間の単位 = コマ）
	float32 AnimFps = 30.0f;
};

struct FModelExportResult
{
	bool bSuccess = false;
	bool bSkippedUpToDate = false;     // 出力のほうが新しかったので、何もしなかった（bSuccess は true）
	bool bImported = false;            // FBX / OBJ から取り込んで作った
	FString SourcePath;                // 絶対パス
	FString OutputPath;
	EModelSaveType SavedType = EModelSaveType::Normal;
	int32 NumMeshes = 0;
	int32 NumAnimations = 0;
	int32 NumBones = 0;
	int64 OutputBytes = 0;
	float32 Milliseconds = 0.0f;
	FString Warning;                   // 成功でも伝えたいこと（画像が見つからない、など。複数なら改行区切り）
	FString Error;                     // 失敗の理由

	// 1 行の説明（ログ・画面用）
	FString GetSummary() const;
};

// .mv1 にできる元ファイル 1 つの状態（ScanModelSources）
enum class EModelExportStatus : uint8
{
	Missing,     // .mv1 がまだ無い
	Stale,       // .mv1 が元より古い
	UpToDate,    // 最新
};

struct FModelSourceInfo
{
	FString Path;              // ルートフォルダからの相対（区切りは '/'）
	FString OutputPath;        // 出力（.mv1）の、同じ形式のパス
	EModelFileKind Kind = EModelFileKind::Unknown;
	EModelExportStatus Status = EModelExportStatus::Missing;
	int64 SourceBytes = 0;
	int64 OutputBytes = 0;     // 無ければ 0
};

// まとめて出力した結果
struct FModelBatchExportResult
{
	TArray<FModelExportResult> Items;
	int32 NumExported = 0;     // 作った（作り直した）数
	int32 NumSkipped = 0;      // 最新だったので何もしなかった数
	int32 NumFailed = 0;

	void Add( const FModelExportResult& Result );
	// "3 exported, 5 up to date, 1 failed"
	FString GetSummary() const;
};

namespace NModelExport
{
	FModelExportResult Export( IAssetBackend& Backend, const FString& SourceFullPath, const FModelExportOptions& Options = FModelExportOptions() );

	// FromDirectory から見た ToPath の相対パス（区切りは '/'）。大文字小文字は区別しない。
	// ドライブが違うなど、相対にできないときは ToPath をそのまま返す
	FString MakeRelativePath( const FString& FromDirectory, const FString& ToPath );
}
