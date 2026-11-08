// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/UI/Markup/UIMarkupLoader.h"

//=============================================================================
// マークアップのファイル（.gxui）: アセットから読み込み、ファイルが変わったら作り直す（ホットリロード）
//   パスは、他のアセットと同じ（"UI/Settings.gxui"、エンジン組み込みは "Engine:/UI/..."）。
//   先頭に UTF-8 の BOM があっても読める。
//
//   CUIMarkupDocument Document;
//   Document.Load( Assets, "UI/Settings.gxui", Context, Bindings );
//   Parent->AddChild( Document.GetRoot() );
//   ...毎フレームか、数秒おきに...
//   if ( Document.ReloadIfChanged( Assets, Context, Bindings ) )
//   {
//       // 作り直した。Document.GetRoot() が新しい木になっている（古い木を親から外し、新しい木を付ける）
//   }
//
// 作り直しに失敗したとき（XML が壊れている途中など）は、古い木をそのまま残す（GetLastError() に理由）。
// 作り直しが成功したら、古い木の結び付けを外してから、新しい木を結ぶ（Bindings.Clear() を呼ぶため、
// 同じ Bindings に、他の木の結び付けを混ぜない。1 つの文書に 1 つの CUIBindingSet を使う）。
//=============================================================================
class CUIMarkupDocument
{
public:
	// 読み込む。成功したら true（GetRoot が使える）。失敗したら false（GetLastError に理由）
	bool Load( CAssetSubsystem& Assets, const FString& Path, FUIMarkupContext& Context, CUIBindingSet& Bindings );

	// ファイルの中身が前と違えば、作り直す。作り直して成功したときだけ true
	bool ReloadIfChanged( CAssetSubsystem& Assets, FUIMarkupContext& Context, CUIBindingSet& Bindings );

	const TSharedPtr<CUIWidget>& GetRoot() const { return Root; }
	const FString& GetPath() const { return Path; }
	const FString& GetLastError() const { return LastError; }
	const TArray<FString>& GetWarnings() const { return Warnings; }

	// ファイルを全部、文字列として読む（BOM は除く）。見つからない・読めないなら false
	static bool ReadText( CAssetSubsystem& Assets, const FString& Path, FString& OutText );

private:
	bool Build( const FString& Text, FUIMarkupContext& Context, CUIBindingSet& Bindings );

	FString Path;
	FString LoadedText;
	TSharedPtr<CUIWidget> Root;
	FString LastError;
	TArray<FString> Warnings;
};
