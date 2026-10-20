#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Platform/FileSystem.h"

//=============================================================================
// モデルのファイル形式
//
//   MV1        DxLib の形式（読み込みが速い。ゲームで使うのはこれ）
//   DxLib 対応  DxLib がそのまま読める、変換前の形式（.x / .pmd / .pmx / .mqo / .mqoz / .vmd）。
//              CAssetManager::ExportModelAsMV1 で .mv1 にできる
//   取り込み    DxLib は読めないが、このフレームワークが読んで .mv1 にできる形式（.fbx / .obj）。
//              CAssetManager::LoadModel( "Robot.fbx" ) は、同じ名前の .mv1 を（無ければ作って）読み込む
//=============================================================================
enum class EModelFileKind : uint8
{
	Unknown,
	MV1,
	DxLibNative,
	Imported,
};

namespace NModelFormat
{
	// 拡張子（大文字小文字は区別しない）で分類する
	EModelFileKind Classify( const FString& Path );

	// .mv1 そのもの
	FORCEINLINE bool IsMV1( const FString& Path ) { return Classify( Path ) == EModelFileKind::MV1; }
	// .mv1 に変換できる（DxLib 対応か、取り込み）
	bool CanConvertToMV1( const FString& Path );

	// 同じフォルダ・同じ名前で、拡張子だけ .mv1 にしたパス（"Models/Robot.fbx" -> "Models/Robot.mv1"）。拡張子が無ければ末尾に付ける
	FString GetMV1Path( const FString& SourcePath );

	// 拡張子の一覧（".fbx" のようにドットつき）
	const TArray<FString>& GetConvertibleExtensions();   // .mv1 にできるもの（DxLib 対応 + 取り込み）
	const TArray<FString>& GetAllModelExtensions();      // .mv1 も含めた、すべて

	// 変換の出力が最新か（出力の更新日時が、元と同じか新しい）。どちらかの日時が分からなければ false
	bool IsOutputUpToDate( const FFileTime& SourceTime, const FFileTime& OutputTime );
	// 日時を、大小比較できる数にする
	int64 ToSortKey( const FFileTime& Time );
}

//=============================================================================
// 骨の名前の照らし合わせ
//   DxLib のアニメーションは、名前が合う骨だけを動かす。モデルとアニメーションのファイルで骨の名前が合っているかを調べる
//   （モーション専用ファイルを、別のモデルに使うときの確認）。
//=============================================================================
struct FBoneMatch
{
	int32 NumAnimBones = 0;      // アニメーション側の骨の数（照らし合わせた数）
	int32 NumMatched = 0;        // そのうちモデルにもある骨の数
	TArray<FString> Missing;     // モデルに無かった骨の名前（最大 MaxMissingNames 個）

	// 一致の割合（0..1）。アニメーションの骨が 0 個なら 1（比べるものがない）
	float32 GetRatio() const { return NumAnimBones > 0 ? static_cast<float32>( NumMatched ) / static_cast<float32>( NumAnimBones ) : 1.0f; }
	// 動くはずの骨がほとんど無い（半分未満）。モデルに合わないアニメーションの目安
	bool IsPoor() const { return GetRatio() < 0.5f; }
};

namespace NModelCompat
{
	constexpr int32 MaxMissingNames = 8;

	// AnimBones の骨のうち、ModelBones にもある（名前が同じ。大文字小文字は区別する）骨を数える
	FBoneMatch MatchBones( const TArray<FString>& ModelBones, const TArray<FString>& AnimBones );
}
